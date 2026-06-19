# BLE 数据层 SDK 协议规范 v1

> 日期：2026-06-08
> 范围：设备端数据层协议、Payload 编解码、BLE GATT UUID、设备收发包流程
> 适用平台：微信小程序 / STM32 / ESP32 / MicroPython 模拟设备 / 桌面调试工具

---

## 1. 协议目标

本 SDK 只定义“字节如何组包、命令如何分配、Payload 如何解释”，不负责扫描、连接、重试或上位机业务界面。

协议升级、版本交换、兼容阻断和发布文档要求见 `compat_policy.md`。版本兼容判断使用 `sdk_compat` 模块，codec 只负责字节编解码。

核心约束：

1. **小包优先**：Dashboard 高频包压到 14 字节 Payload，配合帧开销后总长 20 字节。
2. **跨语言一致**：发布 SDK 与 App / 上位机实现使用同一套 CMD、UUID、字段顺序和单位。
3. **不做旧协议兼容**：项目尚未发布，旧字段、旧命令、DIS 读取方式不再保留。
4. **设备数据不魔法兜底**：设备信息、设置参数、自检数据必须由设备真实返回；设备端不要伪造默认业务数据。
5. **数组二步解析**：批量记录先读取 `size`，再按 item 解包，避免 MCU 侧为了猜容量分配过大数组。

---

## 2. BLE GATT

设备必须暴露自定义主服务，并通过 TX/RX 两个特征收发 SDK 帧。

| 用途 | 128-bit UUID | 16-bit 兼容 UUID | 属性 |
| --- | --- | --- | --- |
| 主服务 | `5868A23F-877F-53F3-B2B6-E8D4FDF32F75` | `A23F` | Primary Service |
| TX 特征（设备 -> 上位机） | `373CDA7C-832F-5510-8CFC-F5A8E11BADDE` | `DA7C` | Notify |
| RX 特征（上位机 -> 设备） | `A5255200-72A1-57AD-9306-1A99E8CFEE4B` | `5200` | Write / Write Without Response |

128-bit UUID 是正式 SDK 通道；16-bit UUID 仅用于外置 BLE 模块兼容。上位机每次连接后都必须通过 `getServices` / `getCharacteristics` 重新发现，并使用 SDK matcher 同时匹配 128-bit 与 16-bit 兼容 UUID，不持久化 GATT 通道。

设备信息不再通过标准 Device Information Service 读取，统一使用 `CMD_DEVICE_GET_INFO`。

---

## 3. 广播与扫描响应

广播包使用标准 AD Structure。上位机通过主服务 UUID 判断是否为目标设备。

### 3.1 ADV

| 项 | 建议值 | 说明 |
| --- | --- | --- |
| Flags | `0x06` | General Discoverable + BR/EDR Not Supported |
| Complete Local Name | `Weld_Control` 或设备名 | 名称最长按 SDK 限制截断 |
| Appearance | `0x14C0` | 当前默认值 |

示例：`sdk_adv_build_flags(0x06) + sdk_adv_build_name("Weld_Control") + appearance`

### 3.2 SCAN_RSP

| 项 | 值 |
| --- | --- |
| Incomplete 128-bit Service UUIDs | `5868A23F-877F-53F3-B2B6-E8D4FDF32F75` |
| 16-bit 兼容 Service UUIDs | `A23F`，仅外置 BLE 模块兼容设备使用 |

注意：ADV 和 SCAN_RSP 单包均受 BLE Legacy Advertising 31 字节限制。设备名过长时必须截断或放弃部分广播字段。

---

## 4. SDK 帧格式

协议版本：`SDK_PROTOCOL_VERSION = 0x1`

所有多字节整数均为 **小端序**。线协议单帧 Payload 长度字段为 `u16`，理论范围 `0-65535`；具体设备/SDK 实现可以根据内存设置更小的 `SDK_MAX_PAYLOAD`。

| 偏移 | 长度 | 字段 | 说明 |
| --- | ---: | --- | --- |
| 0 | 1 | `SOF` | 固定 `0xAA` |
| 1 | 2 | `LEN_LE` | Payload 长度，`u16 little-endian` |
| 3 | 1 | `CMD` | 完整命令字；应答命令通常为请求命令 `| 0x80` |
| 4 | 1 | `SEQ` | 请求序列号，用于匹配应答 |
| 5 | N | `PAYLOAD` | 载荷，长度为 `LEN` |
| 5+N | 1 | `CRC8` | 覆盖 `SOF` 到 Payload 末尾 |

帧固定开销为 6 字节：`SOF + LEN_LE16 + CMD + SEQ + CRC8`。

### 4.1 传输层切块

BLE 默认 20 字节写入/Notify 只是链路层切块。发送方应先调用 `sdk_encode()` 得到完整 SDK frame，再把 frame bytes 按 BLE MTU/模块限制切成多段发送。

同一连接、同一方向的 SDK frame bytes 必须串行发送；一个完整 frame 的 chunk 未全部发送完成前，不允许插入其他 frame 的 chunk。

如果 OTA、日志等业务需要在大数据传输中插入高优先级命令，应由业务层拆成多个独立 CMD 包，并在完整 frame 边界调度，不再引入 SDK frame 分片标志。

### 4.2 CRC8

CRC8 校验覆盖除 CRC 字节自身以外的整帧内容：

```text
CRC8(SOF, LEN_LO, LEN_HI, CMD, SEQ, PAYLOAD...)
```

CRC 错误时接收方直接丢弃该帧，不进入业务处理。

### 4.3 SEQ 与应答匹配

上位机发起请求时生成 `SEQ`，设备应答时原样带回同一 `SEQ`。协议层用 `CMD + SEQ` 匹配请求/应答。

业务主动上报，例如 `CMD_DASHBOARD_COMPACT`，不要求上位机应答，也不依赖请求 `SEQ`。

### 4.4 统一 ACK Payload

除 Dashboard 主动上报外，所有请求应答 Payload 都使用统一结果壳：

```text
sdk_result_t:
  status: u8
  code  : u8
  data  : bytes
```

| 字段 | 说明 |
| --- | --- |
| `status` | 统一流程状态。上位机先看它决定成功、失败、忙、鉴权失效等流程。 |
| `code` | 当前 CMD 的业务码。`0` 表示无错误；非 0 用于生成具体错误文案。 |
| `data` | 固定 data 实体。成功返回实体时放实体；无返回值时为空。失败不把数字直接塞进 data。 |

`status` 定义：

| 值 | 常量 | 说明 |
| ---: | --- | --- |
| 0 | `SDK_RESULT_STATUS_RESERVED` | 保留/未初始化，收到必须视为协议错误 |
| 1 | `SDK_RESULT_STATUS_OK` | 成功 |
| 2 | `SDK_RESULT_STATUS_FAIL` | 普通失败 |
| 3 | `SDK_RESULT_STATUS_BUSY` | 设备忙 |
| 4 | `SDK_RESULT_STATUS_AUTH_INVALID` | 鉴权失效，上位机应清 token 并提示重新配对 |
| 5 | `SDK_RESULT_STATUS_INVALID_PARAM` | 参数非法 |
| 6 | `SDK_RESULT_STATUS_NOT_SUPPORTED` | 不支持 |
| 7 | `SDK_RESULT_STATUS_DEVICE_ERROR` | 设备内部异常 |
| 8 | `SDK_RESULT_STATUS_PENDING` | 请求已受理，等待后续最终应答 |

当前业务码按模块内语义分配，不做全局唯一编号。`code=0` 固定表示 `SDK_RESULT_CODE_COMMON_NONE`；各业务类别从 1 开始连续编号，同一个数值可在不同 CMD/模块下复用。

| 模块 | 编号规则 |
| --- | --- |
| 通用 | `0` 表示无错误，`1` 表示未知错误，`2` 表示协议版本不兼容 |
| 配对 | 从 `1` 开始连续 |
| 鉴权 | 从 `1` 开始连续 |
| 设置 | 从 `1` 开始连续 |
| Dashboard / 控制 | 从 `1` 开始连续 |
| OTA | 使用 OTA 模块自己的 code 常量 |

SDK 实现中使用统一长命名常量，三端名称保持一致，例如 `SDK_RESULT_CODE_COMMON_NONE`、`SDK_RESULT_CODE_PAIR_WAIT_CONFIRM`、`SDK_RESULT_CODE_AUTH_INVALID_TOKEN`。`sdk_result_t.code` 是 1 字节模块内业务码槽位，必须结合当前 ACK/CMD 解释。

Dashboard 高频上报 `CMD_DASHBOARD_COMPACT`、点焊记录、运行日志不套 `sdk_result_t`，保持极简 payload。

### 4.5 收发包示例

以下示例均使用当前 v1 帧格式：

```text
SOF LEN_LO LEN_HI CMD SEQ PAYLOAD... CRC8
```

#### 4.5.1 空 Payload 请求

上位机发送 `CMD_SETTINGS_READ_CURRENT = 0x21`，读取设备当前设置。假设 `SEQ = 0x12`，Payload 为空：

```text
AA 00 00 21 12 EB
```

字段拆解：

| 字节 | 含义 |
| --- | --- |
| `AA` | SOF |
| `00 00` | Payload 长度 0，little-endian |
| `21` | `CMD_SETTINGS_READ_CURRENT` |
| `12` | SEQ |
| `EB` | CRC8 |

设备返回 `CMD_SETTINGS_READ_CURRENT_ACK = 0xA1`，必须带回相同 `SEQ = 0x12`。ACK Payload 先放 2 字节 `sdk_result_t` 头，再放固定 data。成功时 `data = settings_current_t`：

```text
Payload:
01 00 03 0A 0F 64 00 7D 00 00 00 18 01 02 AC 0D E0 2E B0 04 E8 03 D0 07 E8 03 88 13
```

Payload 拆解：

| 字节 | 字段 | 值 |
| --- | --- | --- |
| `01` | `status` | `SDK_RESULT_STATUS_OK` |
| `00` | `code` | `SDK_RESULT_CODE_COMMON_NONE` |
| `03` | `profile_id` | 3 |
| `0A 0F` | `target_voltage_mv` | 3850mV |
| `64 00` | `target_current_a10` | 100 = 10.0A |
| `7D 00` | `preheat_pulse_ms10` | 125 = 12.5ms |
| `00 00` | `cool_time_ms10` | 0 = 0ms |
| `18 01` | `main_pulse_ms10` | 280 = 28.0ms |
| `02` | `trigger_mode` | 2 = 自动 |
| `AC 0D` | `auto_delay_ms` | 3500ms |
| `E0 2E` | `target_voltage_mv_max` | 12000mV = 12.0V |
| `B0 04` | `target_current_a10_max` | 1200 = 120.0A |
| `E8 03` | `preheat_pulse_ms10_max` | 1000 = 100.0ms |
| `D0 07` | `cool_time_ms10_max` | 2000 = 200.0ms |
| `E8 03` | `main_pulse_ms10_max` | 1000 = 100.0ms |
| `88 13` | `auto_delay_ms_max` | 5000ms |

#### 4.5.2 带 Payload 请求

上位机应用一个设置页参数，发送 `CMD_SETTINGS_PROFILE = 0x40`。假设 `SEQ = 0x20`，Payload 为 `settings_apply_profile_t` 14 字节，不包含 `sdk_result_t` 头，也不包含 `limits_max`：

```text
AA 0E 40 20 00 03 0A 0F 64 00 7D 00 00 00 18 01 02 AC 0D 97
```

设备处理成功后返回 `CMD_SETTINGS_PROFILE_ACK = 0xC0`，Payload 为 `sdk_result_t`，成功时 status/code 为 `01 00`：

```text
Payload:
01 00
```

业务层必须等 ACK 返回并确认 `status = SDK_RESULT_STATUS_OK` 后，才能提示“操作成功”。空 ACK 或 `status=0` 必须视为协议错误。

#### 4.5.3 SEQ 处理

`SEQ` 为 1 字节，范围 `0x00-0xFF`。请求方生成 `SEQ`，设备端处理请求时必须原样带回：

```text
Request: CMD=0x21, SEQ=0x12
ACK    : CMD=0xA1, SEQ=0x12
```

设备端主动上报，例如 `CMD_DASHBOARD_COMPACT`，可以使用设备本地递增 `SEQ`。主动上报不需要上位机返回 ACK。

#### 4.5.4 设备端接收流程

设备端 RX 特征收到数据后，按字节流喂给 `sdk_decode`。一段 BLE 写入中可能包含半帧、一帧或多帧，必须循环解析：

```py
offset = 0
while offset < len(rx_data):
    result = sdk_decode(sdk_device, rx_data[offset:])
    if result['kind'] == 'packet':
        handle_packet(conn_handle, result['packet'])
        offset += result['consumed']
    elif result['kind'] == 'need-more':
        break
    else:
        offset += result['consumed']
```

设备端处理规则：

| 情况 | 处理方式 |
| --- | --- |
| `packet` | 进入业务 `handle_packet` |
| `need-more` | 等待下一次 RX 数据 |
| `error` | 丢弃错误字节，继续寻找下一帧 |
| CRC 错误 | 不进入业务，不回 ACK |

`need-more` 不能无限等待。调用方应在 transport adapter 或连接会话层记录未完成帧的等待时间；物理连接断开、外置 BLE 模块透明传输会话重建，或等待超过设备配置的半帧超时时，应调用 `sdk_device_reset_parser()` 清理当前 `sdk_device_t` 的解析状态。超时时间不是 wire protocol 字段，应按链路特性配置；ESP-IDF demo 当前使用 1000ms 作为保守值。

这个恢复逻辑对 OTA 尤其重要：用户连续执行开始更新、取消更新、重新开始时，`CMD_OTA_ABORT_ACK` 之后仍可能有已经写入链路的 `CMD_OTA_DATA` 残留半帧。如果 decoder 一直等待该半帧补齐，后续 `CMD_OTA_START`、`CMD_DEVICE_AUTH`、`CMD_DEVICE_RESET` 等完整命令会被吞进旧 payload。设备端必须能丢弃过期半帧并恢复到等待 `SDK_SOF` 的状态。

#### 4.5.5 设备端命令派发与 ACK

解出完整包后按 `CMD` 派发。`sdk_decode()` 只在完整 frame 通过 CRC 后返回 `packet`：

```py
def handle_packet(conn_handle, pkt):
    if pkt.cmd == CMD_SETTINGS_READ_CURRENT:
        # current_settings 包含 profile_id、current_profile 和 limits_max
        payload = settings_current_t.pack(current_settings)
        result = sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload)
        send_packet_async(conn_handle, CMD_SETTINGS_READ_CURRENT_ACK, pkt.seq, result)
```

设备端回包要点：

1. ACK 的 `SEQ` 必须等于请求 `pkt.seq`。
2. ACK 的 `CMD` 使用对应应答命令，例如 `0x21 -> 0xA1`。
3. Payload 大于 BLE 单次写入/Notify 限制时，只拆传输层 chunk，不拆 SDK frame。
4. 所有请求应答 Payload 必须使用 `sdk_result_t = status + code + data`。
5. 未鉴权或业务失败时，也应返回对应 ACK，并在 `status/code` 中说明原因。

#### 4.5.6 设备端发送 ACK

设备端发送 ACK 的基本流程：

```py
def send_packet(conn_handle, cmd, seq, payload):
    pkt = sdk_packet_t(cmd=cmd, seq=seq, payload=payload)
    for chunk in sdk_frame_chunk_with_encode_iter(sdk_device, pkt, 20):
        ble.gatts_notify(conn_handle, tx_handle, chunk['data'])
```

示例：设备收到 `CMD_SETTINGS_PROFILE = 0x40`，`SEQ = 0x20`，应用成功后返回：

```text
AA 02 00 C0 20 01 00 D6
```

其中 `C0` 是 `CMD_SETTINGS_PROFILE_ACK`，`20` 是请求原 SEQ，Payload `01 00` 表示 `status=OK, code=NONE`。

#### 4.5.7 设备主动上报

设备主动上报不需要等待上位机请求，也不需要上位机 ACK。Dashboard 高频上报示例：

```py
seq = tick & 0xFF
payload = dashboard_compact_t.pack(compact_fields)
send_packet_async(conn_handle, CMD_DASHBOARD_COMPACT, seq, payload)
```

主动上报建议：

1. 高频实时包不排队堆积；如果上一包还没发完，可以丢弃旧实时数据，只保留最新状态。
2. 点焊记录和日志可以批量发送，Payload 使用 `size + item[]` 格式。
3. 设备断开连接或收到 `CMD_DASHBOARD_STOP` 后，应停止主动上报任务。

#### 4.5.8 大 Payload 发送规则

发送大 Payload 时仍然只编码成一个 SDK frame。假设发送 `CMD_SETTINGS_READ_SELF_CHECK_ACK = 0xA2`，`SEQ = 0x33`，总 Payload 为 260 字节，frame 头部为：

```text
AA 04 01 A2 33 ...
```

其中 `04 01` 是 Payload 长度 `260` 的 little-endian 表示。随后是 260 字节 Payload 和 1 字节 CRC8。

BLE 层按 20 字节或协商 MTU 把这条完整 frame bytes 切成多个 chunk 发送：

```text
chunk 1: frame[0:20]
chunk 2: frame[20:40]
...
```

接收方只把 chunk bytes 顺序喂给 `sdk_decode()`；只有完整 frame 收齐并通过 CRC 后才进入业务派发。chunk 本身没有 `cmd/seq` 身份，不能和其他 frame 的 chunk 交错发送。

---

## 5. 命令分配

命令按功能区分段。请求命令使用低半区，应答命令通常为请求命令 `| 0x80`。

### 5.1 系统 / 设备 / 配对 / 鉴权（`0x00-0x0F`）

| 请求 | 应答 | 名称 | Payload |
| --- | --- | --- | --- |
| `0x00` | `0x80` | `CMD_PING` / `CMD_PONG` | 应答 `sdk_result_t`，data 为空 |
| `0x01` | `0x81` | `CMD_DEVICE_GET_INFO` / `CMD_DEVICE_GET_INFO_ACK` | 应答 `sdk_result_t`，成功 data 为 `device_info_t` |
| `0x02` | `0x82` | `CMD_DEVICE_RESET` / `CMD_DEVICE_RESET_ACK` | 应答 `sdk_result_t`，data 为空 |
| `0x03` | `0x83` | `CMD_DEVICE_PAIR_REQUEST` / `CMD_DEVICE_PAIR_REQUEST_ACK` | 请求 `pair_request_t`，成功 data 为 `pair_token_t` |
| `0x04` | `0x84` | `CMD_DEVICE_PAIR_UNPAIR` / `CMD_DEVICE_PAIR_UNPAIR_ACK` | 应答 `sdk_result_t`，data 为空 |
| `0x05` | `0x85` | `CMD_DEVICE_AUTH` / `CMD_DEVICE_AUTH_ACK` | 请求 `auth_request_t`，应答 `sdk_result_t` |
| — | `0x86` | `CMD_DEVICE_AUTH_REQUIRED` | 应答 `sdk_result_t`，`status=AUTH_INVALID`，data 为被拒绝的原请求 CMD |

设备端免鉴权请求白名单定义在各语言的 `sdk_commands` 文件中，当前为：`CMD_PING`、`CMD_DEVICE_GET_INFO`、`CMD_DEVICE_PAIR_REQUEST`、`CMD_DEVICE_AUTH`。
设备收到白名单之外的请求且当前连接未鉴权时，应返回 `CMD_DEVICE_AUTH_REQUIRED`，随后断开连接；上位机收到该包后应清除本地 token 并提示重新配对。

### 5.2 OTA（`0x10-0x1F`）

| 请求 | 应答 | 名称 | Payload |
| --- | --- | --- | --- |
| `0x10` | `0x90` | `CMD_OTA_START` / `CMD_OTA_START_ACK` | 请求 `ota_start_t`，应答 `sdk_result_t`，data 为 `ota_ack_data_t` |
| `0x11` | `0x91` | `CMD_OTA_DATA` / `CMD_OTA_DATA_ACK` | 请求 `ota_data_hdr_t + data`，应答 `sdk_result_t`，data 为 `ota_ack_data_t` |
| `0x12` | `0x92` | `CMD_OTA_VERIFY` / `CMD_OTA_VERIFY_ACK` | 请求 `ota_verify_t`，应答 `sdk_result_t`，data 为 `ota_ack_data_t` |
| `0x13` | `0x93` | `CMD_OTA_ABORT` / `CMD_OTA_ABORT_ACK` | 应答 `sdk_result_t`，data 为 `ota_ack_data_t` |

### 5.3 读取（`0x20-0x3F`）

| 请求 | 应答 | 名称 | Payload |
| --- | --- | --- | --- |
| `0x20` | `0xA0` | `CMD_READ_DASHBOARD_INIT` / `CMD_READ_DASHBOARD_INIT_ACK` | 请求 `dashboard_init_request_t`，应答 `sdk_result_t`，成功 data 为 `dashboard_init_t` |
| `0x21` | `0xA1` | `CMD_SETTINGS_READ_CURRENT` / `CMD_SETTINGS_READ_CURRENT_ACK` | 应答 `sdk_result_t`，成功 data 为 `settings_current_t` |
| `0x22` | `0xA2` | `CMD_SETTINGS_READ_SELF_CHECK` / `CMD_SETTINGS_READ_SELF_CHECK_ACK` | 应答 `sdk_result_t`，成功 data 为 `settings_self_check_t` |

### 5.4 写入 / 控制（`0x40-0x5F`）

| 请求 | 应答 | 名称 | Payload |
| --- | --- | --- | --- |
| `0x40` | `0xC0` | `CMD_SETTINGS_PROFILE` / `CMD_SETTINGS_PROFILE_ACK` | 请求 `settings_apply_profile_t`，应答 `sdk_result_t` |
| `0x41` | `0xC1` | `CMD_SETTINGS_RESET` / `CMD_SETTINGS_RESET_ACK` | 请求 `settings_reset_t`，应答 `sdk_result_t` |
| `0x42` | `0xC2` | `CMD_DASHBOARD_START` / `CMD_DASHBOARD_START_ACK` | 应答 `sdk_result_t` |
| `0x43` | `0xC3` | `CMD_DASHBOARD_STOP` / `CMD_DASHBOARD_STOP_ACK` | 应答 `sdk_result_t` |
| `0x44` | `0xC4` | `CMD_MANUAL_TRIGGER` / `CMD_MANUAL_TRIGGER_ACK` | 应答 `sdk_result_t` |
| `0x45` | `0xC5` | `CMD_SAFE_DISCHARGE` / `CMD_SAFE_DISCHARGE_ACK` | 应答 `sdk_result_t` |
| `0x46` | `0xC6` | `CMD_SAFE_DISCHARGE_STOP` / `CMD_SAFE_DISCHARGE_STOP_ACK` | 应答 `sdk_result_t` |
| `0x47` | `0xC7` | `CMD_CHARGE_START` / `CMD_CHARGE_START_ACK` | 应答 `sdk_result_t` |
| `0x48` | `0xC8` | `CMD_CHARGE_PAUSE` / `CMD_CHARGE_PAUSE_ACK` | 应答 `sdk_result_t` |

手动触发、安全放电、充电控制必须由设备返回 ACK；上位机应以设备 ACK 作为操作结果依据。

### 5.5 设备主动上报（`0x60-0x6F`）

| CMD | 名称 | Payload |
| --- | --- | --- |
| `0x60` | `CMD_DASHBOARD_COMPACT` | `dashboard_compact_t` |
| `0x61` | `CMD_DASHBOARD_WELD_RECORDS` | `dashboard_weld_records_t` |
| `0x62` | `CMD_DASHBOARD_LOGS` | `dashboard_logs_t` |

### 5.6 预留（`0x70-0x7F`）

当前保留，不分配业务命令。

---

## 6. 设备信息 Payload

### 6.1 `device_info_t`

用于添加设备、固件更新参数、能力集合展示。固定头 29 字节，后接三个 UTF-8 变长字符串。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `company_id` | 厂商 ID |
| 2 | `u16` | `product_id` | 产品/型号 ID |
| 4 | `u32` | `feature_mask` | 功能集合 bitmask |
| 8 | `u16` | `protocol_version` | SDK 协议版本，当前为 `0x1` |
| 10 | `u16` | `protocol_min_version` | 设备要求的最低 App 协议版本 |
| 12 | `u16` | `ota_max_kb` | 最大固件大小，单位 KB |
| 14 | `u16` | `ota_chunk_max` | 推荐 OTA chunk size，单位 byte |
| 16 | `u8` | `firmware_major` | 固件版本 major |
| 17 | `u8` | `firmware_minor` | 固件版本 minor |
| 18 | `u8` | `firmware_patch` | 固件版本 patch |
| 19 | `u32` | `firmware_build_id` | 固件 build id |
| 23 | `u8` | `hardware_major` | 硬件版本 major |
| 24 | `u8` | `hardware_minor` | 硬件版本 minor |
| 25 | `u8` | `hardware_patch` | 硬件版本 patch |
| 26 | `u8` | `manufacturer_len` | 厂商字符串长度 |
| 27 | `u8` | `model_len` | 型号字符串长度 |
| 28 | `u8` | `serial_len` | 序列号字符串长度 |
| 29 | `bytes` | `manufacturer` | UTF-8 |
| 29+M | `bytes` | `model` | UTF-8 |
| 29+M+N | `bytes` | `serial` | UTF-8 |

约束：

- `serial_len` 必须大于 0；设备不允许返回空序列号。
- `manufacturer`、`model`、`serial` 都应返回真实有效值，不能依赖上位机兜底。
- 固件更新能力依赖 `firmware_*`、`firmware_build_id`、`ota_max_kb`、`ota_chunk_max`。
- SDK 的 pack/unpack 只保证 wire 结构和长度安全；serial 非空、厂商 ID 分配、枚举范围、限幅和功能支持等业务合法性必须在设备业务层进入 ACK 前校验。

### 6.2 设备端完整收发示例

设备端所有请求都走同一条链路；下面以 `CMD_DEVICE_GET_INFO` 为完整样例，其他命令按同一位置扩展即可：

```text
物理连接建立 -> 创建 sdk_device_t -> RX 字节流 -> sdk_decode -> CMD 派发 -> 业务 Payload 打包 -> ACK/Notify 回包
```

ACK 必须使用请求包原 `SEQ`。

#### 6.2.1 MicroPython 示例

```py
def on_rx_write(self, conn_handle, rx_data):
    sdk_dev = self.get_sdk_device(conn_handle)
    offset = 0
    while offset < len(rx_data):
        result = sdk_decode(sdk_dev, rx_data[offset:])
        if result['kind'] == 'packet':
            self.handle_packet(conn_handle, result['packet'])
            offset += result['consumed']
        elif result['kind'] == 'need-more':
            break
        else:
            offset += result['consumed']

def handle_packet(self, conn_handle, pkt):
    if pkt.cmd == CMD_DEVICE_GET_INFO:
        asyncio.create_task(self.handle_device_get_info(conn_handle, pkt.seq))
    else:
        ...

async def handle_device_get_info(self, conn_handle, seq):
    data = device_info_t.pack({
        'company_id': self.company_id,
        'product_id': self.product_id,
        'feature_mask': self.feature_mask,
        'protocol_version': SDK_PROTOCOL_VERSION,
        'protocol_min_version': SDK_MIN_PROTOCOL_VERSION,
        'ota_max_kb': SDK_OTA_MAX_FW_SIZE // 1024,
        'ota_chunk_max': SDK_OTA_CHUNK_MAX,
        'firmware_major': self.firmware_version[0],
        'firmware_minor': self.firmware_version[1],
        'firmware_patch': self.firmware_version[2],
        'firmware_build_id': self.firmware_build_id,
        'hardware_major': self.hardware_version[0],
        'hardware_minor': self.hardware_version[1],
        'hardware_patch': self.hardware_version[2],
        'manufacturer': self.manufacturer_name,
        'model': self.model_number,
        'serial': self.serial_number,
    })
    payload = sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, data)
    await self.send_packet_async(conn_handle, CMD_DEVICE_GET_INFO_ACK, seq, payload)

async def send_packet_async(self, conn_handle, cmd, seq, payload):
    pkt = sdk_packet_t(cmd=cmd, seq=seq, payload=payload)
    sdk_dev = self.get_sdk_device(conn_handle)
    for chunk in sdk_frame_chunk_with_encode_iter(sdk_dev, pkt, SDK_NOTIFY_CHUNK_SIZE):
        self.ble.gatts_notify(conn_handle, self.tx, chunk['data'])
        await asyncio.sleep_ms(5)
```

注意：

1. `seq` 必须使用请求包 `pkt.seq`，不能重新生成。
2. `serial` 必须非空。
3. `send_packet_async` 只编码一个完整 SDK frame；BLE 层按 `SDK_NOTIFY_CHUNK_SIZE` 做链路层切块。

#### 6.2.2 C 示例

```c
#include <string.h>

#include "sdk_commands.h"
#include "sdk_codec.h"
#include "sdk_frame_chunk.h"
#include "payloads/device_info.h"

#define COMPANY_ID 0xAAAA
#define PRODUCT_ID 0x0001
#define FEATURE_MASK 0x0000000F
#define FIRMWARE_BUILD_ID 20260606u
#define NOTIFY_CHUNK_SIZE 20u

static const char kManufacturer[] = "WeldControl";
static const char kModel[] = "WC-01";
static const char kSerial[] = "348518467572";

static sdk_device_t *g_sdk = NULL;

/* 由具体 BLE 协议栈实现：通过 TX 特征 notify 一段字节。 */
extern bool ble_notify_chunk(uint16_t conn_handle,
                             const uint8_t *data,
                             uint16_t len);

void on_ble_connected(uint16_t conn_handle, const uint8_t peer_mac[6]) {
    (void)conn_handle;
    g_sdk = sdk_device_create(peer_mac, "host");
}

void on_ble_disconnected(uint16_t conn_handle) {
    (void)conn_handle;
    sdk_device_destroy(g_sdk);
    g_sdk = NULL;
}

static bool send_packet(uint16_t conn_handle,
                        uint8_t cmd,
                        uint8_t seq,
                        const uint8_t *payload,
                        uint16_t payload_len) {
    uint8_t frame[SDK_FRAME_OVERHEAD + SDK_MAX_PAYLOAD];
    sdk_packet_t pkt;

    if (!g_sdk) return false;
    if (payload_len > SDK_MAX_PAYLOAD) return false;

    pkt.cmd = cmd;
    pkt.seq = seq;
    pkt.payload_len = payload_len;
    if (payload_len > 0) {
        memcpy(pkt.payload, payload, payload_len);
    }

    sdk_frame_chunk_with_encode_iter_t iter = sdk_frame_chunk_with_encode_iter(
        g_sdk, &pkt, frame, sizeof(frame), NOTIFY_CHUNK_SIZE);
    if (!iter.ok) return false;

    sdk_frame_chunk_t chunk;
    while (sdk_frame_chunk_with_encode_next(&iter, &chunk)) {
        if (!ble_notify_chunk(conn_handle, chunk.data, chunk.len)) return false;
    }
    return true;
}

static bool handle_device_get_info(uint16_t conn_handle, uint8_t seq) {
    uint8_t data[128];
    uint16_t data_len = 0;
    uint8_t payload[SDK_RESULT_HEADER_SIZE + sizeof(data)];
    uint16_t payload_len = 0;

    device_info_t info = {
        .company_id = COMPANY_ID,
        .product_id = PRODUCT_ID,
        .feature_mask = FEATURE_MASK,
        .protocol_version = SDK_PROTOCOL_VERSION,
        .protocol_min_version = SDK_MIN_PROTOCOL_VERSION,
        .ota_max_kb = SDK_OTA_MAX_FW_SIZE / 1024,
        .ota_chunk_max = SDK_OTA_CHUNK_MAX,
        .firmware_major = 1,
        .firmware_minor = 0,
        .firmware_patch = 3,
        .firmware_build_id = FIRMWARE_BUILD_ID,
        .hardware_major = 1,
        .hardware_minor = 0,
        .hardware_patch = 0,
        .manufacturer = kManufacturer,
        .manufacturer_len = (uint8_t)strlen(kManufacturer),
        .model = kModel,
        .model_len = (uint8_t)strlen(kModel),
        .serial = kSerial,
        .serial_len = (uint8_t)strlen(kSerial),
    };

    if (info.serial_len == 0) {
        payload_len = sdk_result_pack(SDK_RESULT_STATUS_DEVICE_ERROR,
                                      SDK_RESULT_CODE_COMMON_UNKNOWN,
                                      NULL,
                                      0,
                                      payload,
                                      sizeof(payload));
        if (payload_len == 0) return false;
        return send_packet(conn_handle, CMD_DEVICE_GET_INFO_ACK, seq, payload, payload_len);
    }

    if (!device_info_pack(&info, data, sizeof(data), &data_len)) {
        payload_len = sdk_result_pack(SDK_RESULT_STATUS_DEVICE_ERROR,
                                      SDK_RESULT_CODE_COMMON_UNKNOWN,
                                      NULL,
                                      0,
                                      payload,
                                      sizeof(payload));
        if (payload_len == 0) return false;
        return send_packet(conn_handle, CMD_DEVICE_GET_INFO_ACK, seq, payload, payload_len);
    }

    payload_len = sdk_result_pack(SDK_RESULT_STATUS_OK,
                                  SDK_RESULT_CODE_COMMON_NONE,
                                  data,
                                  data_len,
                                  payload,
                                  sizeof(payload));
    if (payload_len == 0) return false;
    return send_packet(conn_handle,
                       CMD_DEVICE_GET_INFO_ACK,
                       seq,
                       payload,
                       payload_len);
}

static void handle_packet(uint16_t conn_handle, sdk_packet_t *pkt) {
    switch (pkt->cmd) {
    case CMD_DEVICE_GET_INFO:
        (void)handle_device_get_info(conn_handle, pkt->seq);
        break;
    default:
        /* ... */
        break;
    }
}

void on_rx_write(uint16_t conn_handle, const uint8_t *data, uint16_t len) {
    reset_stale_sdk_parser_if_needed();

    uint16_t offset = 0;
    while (offset < len) {
        sdk_decode_result_t result = sdk_decode(g_sdk, data + offset, len - offset);
        if (result.kind == SDK_DECODE_PACKET) {
            clear_partial_frame_timer();
            handle_packet(conn_handle, &result.packet);
            offset += result.consumed;
        } else if (result.kind == SDK_DECODE_NEED_MORE) {
            start_partial_frame_timer_if_needed();
            break;
        } else {
            clear_partial_frame_timer();
            offset += result.consumed;
        }
    }
}
```

上例中的 `reset_stale_sdk_parser_if_needed()`、`start_partial_frame_timer_if_needed()` 和 `clear_partial_frame_timer()` 由具体 transport adapter 实现；核心要求是在半帧过期时调用 `sdk_device_reset_parser(g_sdk)`。

设备端实现要点：

1. `device_info_pack` 会写入固定头和三个变长字符串，调用前需设置字符串指针与长度。
2. `payload` 缓冲区必须能容纳 `DEVICE_INFO_FIXED_SIZE + manufacturer_len + model_len + serial_len`。
3. `CMD_DEVICE_GET_INFO_ACK` 必须使用请求原 `seq`。
4. 不返回空 `serial`；上位机会把空序列号视为无效设备信息。
5. C 示例里的 `ble_notify_chunk` 由具体 BLE 协议栈实现；`sdk_decode` / `sdk_encode` / `device_info_pack` 使用当前 C SDK API。

---

## 7. 配对与鉴权 Payload

### 7.1 `pair_request_t`，`9 + name_len` 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u64` | `client_id` | 上位机侧 ID，小端序 |
| 8 | `u8` | `name_len` | 上位机名称 UTF-8 字节数 |
| 9 | `u8[name_len]` | `name` | 上位机名称，UTF-8，不带 `\0` |

`client_id` 是 64 位无符号整数，wire 上固定 8 字节。上位机应保证同一安装实例内稳定，不要求设备把它当作 MAC 地址处理。

`name_len` 最大为 `36` 字节，按 12 个常见中文 UTF-8 字符预留。设备端不得按 NUL 结尾字符串读取，应按 `name_len` 截取。

包含协议帧头和 CRC 后，`CMD_DEVICE_PAIR_REQUEST` 总长度为 `15 + name_len` 字节。例如名称 `WeldControl` 的 `name_len=11`，整包为 26 字节。最大名称长度 36 字节时，payload 为 45 字节，整包为 51 字节。

### 7.2 `pair_token_t`，12 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8[12]` | `token` | 配对成功后返回的随机 Token |

配对 ACK 外层始终使用 `sdk_result_t`。配对是多阶段应答：

1. 上位机发送 `CMD_DEVICE_PAIR_REQUEST`。
2. 设备若不在配对模式，立即回复 `status=FAIL, code=SDK_RESULT_CODE_PAIR_NOT_IN_PAIRING_MODE, data=空`。
3. 设备若已进入配对模式并受理请求，立即回复 `status=PENDING, code=SDK_RESULT_CODE_PAIR_WAIT_CONFIRM, data=空`，表示正在等待物理按键确认。
4. 用户确认、拒绝或等待超时后，设备必须继续使用同一个 `CMD_DEVICE_PAIR_REQUEST_ACK` 和同一个 `SEQ` 回复最终结果。
5. 最终成功时 `status=OK, code=NONE, data=pair_token_t`；最终失败时 `status=FAIL`，`code` 使用配对业务码，`data=空`。

上位机收到 `PENDING` 不能结束请求监听；只有 `OK`、`FAIL`、`AUTH_INVALID` 等终态才结束本次配对请求。

配对失败或等待确认 ACK 为 `sdk_result_t` 空 data，payload 2 字节，整包 8 字节。配对成功 ACK 为 `sdk_result_t + pair_token_t`，payload 14 字节，整包 20 字节。

配对业务码：

| 值 | 常量 | 说明 |
| ---: | --- | --- |
| 1 | `SDK_RESULT_CODE_PAIR_REJECTED` | 用户/设备拒绝配对 |
| 2 | `SDK_RESULT_CODE_PAIR_NOT_IN_PAIRING_MODE` | 设备不在配对模式 |
| 3 | `SDK_RESULT_CODE_PAIR_WAIT_CONFIRM` | 配对请求已受理，等待设备物理确认 |
| 4 | `SDK_RESULT_CODE_PAIR_CONFIRM_TIMEOUT` | 等待设备物理确认超时 |

### 7.3 `auth_request_t`，12 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8[12]` | `token` | 首次配对成功后保存的 token |

包含协议帧头和 CRC 后，`CMD_DEVICE_AUTH` 请求整包为 18 字节；`CMD_DEVICE_AUTH_ACK` 空 data 应答整包为 8 字节。

### 7.4 鉴权 ACK

鉴权 ACK 外层使用 `sdk_result_t`。Token 通过时 `status=OK, code=NONE, data=empty`；Token 无效或设备清空绑定信息时 `status=AUTH_INVALID, code=SDK_RESULT_CODE_AUTH_INVALID_TOKEN, data=empty`。

未鉴权访问业务命令时，设备发送 `CMD_DEVICE_AUTH_REQUIRED`，Payload 仍为 `sdk_result_t`：`status=AUTH_INVALID, code=SDK_RESULT_CODE_AUTH_REJECTED_COMMAND, data=被拒绝的 CMD(1字节)`。

设备返回 `status=AUTH_INVALID, code=SDK_RESULT_CODE_AUTH_INVALID_TOKEN` 时，表示当前 token 已无效或不被设备信任。上位机应清除本地保存的 token，并提示用户重新配对；连接超时、蓝牙断开等传输错误不应自动清 token。

---

## 8. Dashboard Payload

Dashboard 实时流启动前，上位机先读取初始化数据，再发送 `CMD_DASHBOARD_START` 开启实时上报；停止实时流时发送 `CMD_DASHBOARD_STOP`。

### 8.1 `dashboard_init_request_t`，11 字节

App 读取 Dashboard 初始化数据时必须携带自身协议和版本信息，设备端可据此选择兼容响应或返回不支持。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `app_protocol_version` | App 当前使用的 SDK 协议版本 |
| 2 | `u16` | `app_min_protocol_version` | App 可解析的最低设备协议版本 |
| 4 | `u8` | `app_version_major` | App 版本 major |
| 5 | `u8` | `app_version_minor` | App 版本 minor |
| 6 | `u8` | `app_version_patch` | App 版本 patch |
| 7 | `u32` | `app_build_id` | App 构建号；未知时填 0 |

### 8.2 `dashboard_init_t`，27 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `setting_voltage_max_mv` | 充电电压上限，同时作为电压环和波形电压轴上限，单位 mV |
| 2 | `u32` | `stat_welds_total` | 设备累计点焊次数 |
| 6 | `u32` | `stat_welds_task` | 本次任务点焊次数 |
| 10 | `u16` | `setting_weld_pre_ms10` | 预热脉冲时间，单位 0.1ms |
| 12 | `u16` | `setting_weld_cooling_ms10` | 冷却时间，单位 0.1ms |
| 14 | `u16` | `setting_weld_main_ms10` | 主脉冲时间，单位 0.1ms |
| 16 | `u16` | `protocol_version` | 设备当前 SDK 协议版本 |
| 18 | `u8` | `firmware_major` | 固件版本 major |
| 19 | `u8` | `firmware_minor` | 固件版本 minor |
| 20 | `u8` | `firmware_patch` | 固件版本 patch |
| 21 | `u32` | `firmware_build_id` | 固件 build id |
| 25 | `u16` | `protocol_min_version` | 设备要求的最低 App 协议版本 |

### 8.3 `dashboard_compact_t`，14 字节

该包用于高频实时上报。Payload 14 字节，完整 SDK 帧 20 字节。

| 偏移 | 类型 | Wire 字段 | 解包字段 | 说明 |
| ---: | --- | --- | --- | --- |
| 0 | `u16` | `voltage_mv` | `voltage_mv` | 当前储能电压，单位 mV |
| 2 | `u16` | `weld_current_a` | `weld_current_a` | 点焊电流，单位 A |
| 4 | `u16` | `charge_current_ma` | `charge_current_ma` | 充电电流，单位 mA |
| 6 | `u16` | `est_time_full_sec` | `est_time_full_sec` | 预计充满剩余时间，单位秒 |
| 8 | `i16` | `temperature_capacitor_c10` | `temperature_capacitor_c10` | 电容温度，单位 0.1 摄氏度 |
| 10 | `i16` | `temperature_mos_c10` | `temperature_mos_c10` | MOS 温度，单位 0.1 摄氏度 |
| 12 | `u8` | `status_flags` | `machine_status` / `charge_mode_code` | 低 4 bit 为设备状态，高 4 bit 为充电模式 |
| 13 | `u8` | `flags` | `discharge_status` / `undefined_status` | 低 4 bit 为放电状态，高 4 bit 预留 |

`status_flags`：

```text
bit0-3: machine_status
bit4-7: charge_mode_code
```

`flags`：

```text
bit0-3: discharge_status
bit4-7: undefined_status
```

充电模式：

| 值 | 名称 | 建议显示文案 |
| ---: | --- | --- |
| 0 | `UNKNOWN` | 未知状态 |
| 1 | `STANDBY` | 待机状态 |
| 2 | `CONSTANT_CURRENT` | 恒流阶段 |
| 3 | `CONSTANT_VOLTAGE` | 恒压阶段 |
| 4 | `PRECHARGE` | 预充阶段 |
| 5 | `FLOAT` | 浮充阶段 |
| 6 | `PAUSED` | 暂停状态 |
| 7 | `FAULT` | 故障状态 |
| 8 | `BALANCE_FAULT` | 均衡异常 |
| 9 | `INPUT_FAULT` | 输入异常 |
| 10 | `OUTPUT_FAULT` | 输出异常 |
| 11 | `PROTECTION_MODE` | 保护模式 |

### 8.4 `dashboard_weld_records_t`

批量点焊记录。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | `size` | 记录数量 |
| 1 + 6*i | `u16` | `id` | 点焊记录递增 ID |
| 3 + 6*i | `u16` | `peak_current_a` | 峰值电流，单位 A |
| 5 + 6*i | `u16` | `post_voltage_mv` | 点焊后的最新电压，单位 mV |

总大小：`1 + 6 * size` 字节。

### 8.5 `dashboard_logs_t`

Dashboard 运行日志。上位机可按 `code` 映射本地文案。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | `size` | 日志数量 |
| 1 + 9*i | `u16` | `id` | 日志递增 ID |
| 3 + 9*i | `u8` | `level` | 日志等级码 |
| 4 + 9*i | `u32` | `time_sec` | 设备启动秒或 Unix 秒 |
| 8 + 9*i | `u16` | `code` | 日志码 |

总大小：`1 + 9 * size` 字节。

日志码：

| 值 | 名称 | 说明 |
| ---: | --- | --- |
| 1 | `DASHBOARD_LOG_CODE_SYSTEM_READY` | 系统就绪 |
| 2 | `DASHBOARD_LOG_CODE_CHARGE_STARTED` | 开始充电 |
| 3 | `DASHBOARD_LOG_CODE_CHARGE_STOPPED` | 停止充电 |
| 4 | `DASHBOARD_LOG_CODE_WELD_TRIGGERED` | 点焊已触发 |
| 5 | `DASHBOARD_LOG_CODE_WELD_COMPLETE` | 点焊完成 |
| 6 | `DASHBOARD_LOG_CODE_TEMPERATURE_WARN` | 温度警告 |
| 7 | `DASHBOARD_LOG_CODE_TEMPERATURE_ERROR` | 温度异常 |
| 8 | `DASHBOARD_LOG_CODE_BLE_CONNECTED` | BLE 已连接 |
| 9 | `DASHBOARD_LOG_CODE_BLE_DISCONNECTED` | BLE 已断开 |
| 10 | `DASHBOARD_LOG_CODE_CONFIG_UPDATED` | 参数已更新 |

---

## 9. 设置页 Payload

设置参数中，设备只保存当前生效参数和当前 `profile_id`；完整预设列表可由上位机本地维护。

`profile_id` 范围为 1-10；0 表示未知。设备收到上位机提交的 `profile_id` 后，应随当前生效参数一并保存。

### 9.1 枚举

触发模式：

| 值 | 名称 | 说明 |
| ---: | --- | --- |
| 0 | `UNSET` | 未选择/未知 |
| 1 | `MANUAL` | 手动触发 |
| 2 | `AUTO` | 自动触发 |

ESR 质量：

| 值 | 名称 | 建议显示文案 |
| ---: | --- | --- |
| 0 | `UNKNOWN` | 未知 |
| 1 | `EXCELLENT` | 优良 |
| 2 | `GOOD` | 良好 |
| 3 | `HEALTHY` | 健康 |
| 4 | `POOR` | 较差 |
| 5 | `BAD` | 很差 |

故障日志类型：

| 值 | 名称 |
| ---: | --- |
| 0 | `NORMAL` |
| 1 | `WARN` |
| 2 | `ERROR` |

### 9.2 `settings_runtime_profile_t`，13 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `target_voltage_mv` | 目标储能电压，单位 mV |
| 2 | `u16` | `target_current_a10` | 目标输出电流，单位 0.1A |
| 4 | `u16` | `preheat_pulse_ms10` | 预热脉冲时间，单位 0.1ms |
| 6 | `u16` | `cool_time_ms10` | 冷却时间，单位 0.1ms |
| 8 | `u16` | `main_pulse_ms10` | 主脉冲时间，单位 0.1ms |
| 10 | `u8` | `trigger_mode` | 0 未选择，1 手动，2 自动 |
| 11 | `u16` | `auto_delay_ms` | 自动触发延迟，单位 ms |

### 9.3 `settings_current_t`，26 字节

设备 -> 上位机，读取当前生效参数与设备侧 settings 动态上限。`limits_max` 只返回上限值；最小值、步进和小数位仍由上位机的 UI 规则定义。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | `profile_id` | 当前生效预设 ID，1-10；0 表示未知 |
| 1 | `settings_runtime_profile_t` | `current_profile` | 当前生效参数 |
| 14 | `u16` | `target_voltage_mv_max` | 充电目标电压上限，单位 mV |
| 16 | `u16` | `target_current_a10_max` | 充电电流上限，单位 0.1A |
| 18 | `u16` | `preheat_pulse_ms10_max` | 预热脉冲时间上限，单位 0.1ms |
| 20 | `u16` | `cool_time_ms10_max` | 冷却时间上限，单位 0.1ms |
| 22 | `u16` | `main_pulse_ms10_max` | 主脉冲时间上限，单位 0.1ms |
| 24 | `u16` | `auto_delay_ms_max` | 自动触发延迟上限，单位 ms |

### 9.4 `settings_apply_profile_t`，14 字节

上位机 -> 设备，提交并立即应用一个参数槽。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | `profile_id` | 要应用的预设 ID，1-10 |
| 1 | `settings_runtime_profile_t` | `profile` | 要应用的参数 |

### 9.5 `settings_reset_t`，1 字节

上位机 -> 设备，恢复出厂设置选项。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | `flags` | bit0=`SETTINGS_RESET_FLAG_CLEAR_TOKENS`，同时清除设备已保存配对 token |

如果 `flags & 0x01 != 0`，设备在返回 `CMD_SETTINGS_RESET_ACK` 后应清除持久化 token 和内存中的当前连接 token，并主动断开连接；上位机收到成功 ACK 后也必须清除本机 token，并返回设备列表/首页重新配对。

### 9.6 `settings_self_check_t`

设备自检模块包含 ESR 数组和设备历史故障日志。该模块不随设置页首包自动读取，用户点击设备信息底部的自检入口后单独请求。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | `esr_size` | ESR 测量数量 |
| 1 | `u16[esr_size]` | `esrs_mohm10` | ESR 数组，单位 0.1mΩ |
| 1+2N | `u8` | `esr_quality` | ESR 质量枚举 |
| 2+2N | `u8` | `fault_log_size` | 故障日志数量 |
| 3+2N | `settings_fault_log_t[]` | `fault_logs` | 历史故障日志 |

### 9.7 `settings_fault_log_t`，167 字节

故障日志用于设备历史提醒，标题和消息由设备返回，不做 code 化。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `id` | 故障日志 ID |
| 2 | `u8` | `type` | 0 正常，1 警告，2 错误 |
| 3 | `u32` | `time_sec` | Unix 秒 |
| 7 | `char[32]` | `title` | UTF-8，零填充 |
| 39 | `char[128]` | `message` | UTF-8，零填充 |

---

## 10. OTA Payload 与固件文件

### 10.1 OTA 业务码

| 值 | 名称 | 说明 |
| ---: | --- | --- |
| 0 | `OTA_CODE_NONE` | 无错误 |
| 100 | `OTA_CODE_SIZE` | 固件大小超限 |
| 101 | `OTA_CODE_SEQ` | 分片序号错误 |
| 102 | `OTA_CODE_CRC` | CRC 校验失败 |
| 103 | `OTA_CODE_BUSY` | 设备忙 |
| 104 | `OTA_CODE_COMPANY` | 厂商 ID 不匹配 |
| 105 | `OTA_CODE_TYPE` | 固件类型不匹配 |

OTA ACK 外层 `sdk_result_t.status` 表示成功/失败/忙，`code` 表示上述 OTA 业务码，`data` 固定为 `ota_ack_data_t`。

### 10.2 `ota_start_t`，14 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `company_id` | 厂商 ID |
| 2 | `u8` | `fw_type` | 固件类型 |
| 3 | `u8[3]` | `fw_ver` | major/minor/patch |
| 6 | `u32` | `fw_size` | Payload 固件大小 |
| 10 | `u16` | `fw_crc16` | Payload CRC16 |
| 12 | `u16` | `chunk_size` | 分片大小 |

### 10.3 `ota_data_hdr_t + data`

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `chunk_index` | 分片序号，从 0 开始 |
| 2 | `u16` | `data_len` | 本片固件数据长度 |
| 4 | `bytes` | `data` | 固件 payload 数据 |

### 10.4 `ota_verify_t`，6 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u32` | `fw_size` | Payload 固件大小 |
| 4 | `u16` | `fw_crc16` | Payload CRC16 |

### 10.5 `ota_ack_data_t`，2 字节

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | `next_chunk` | 设备期望的下一片序号 |

### 10.6 WCFW 固件文件格式

上位机选择的固件文件使用 WCFW 头部。上位机解析头部用于展示和校验，真正发送给设备的是 **payload 数据**，不是完整文件。

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `char[4]` | `magic` | 固定 `WCFW` |
| 4 | `u16` | `header_size` | 当前为 64 |
| 6 | `u16` | `company_id` | 厂商 ID |
| 8 | `u8` | `fw_type` | 固件类型 |
| 9 | `u8[3]` | `fw_ver` | major/minor/patch |
| 12 | `u32` | `build_id` | 构建 ID |
| 16 | `u32` | `payload_size` | Payload 大小 |
| 20 | `u16` | `payload_crc16` | Payload CRC16 |
| 22 | `bytes` | reserved | 保留到 64 字节 |
| 64 | `bytes` | payload | 实际发送的固件内容 |

选择文件后，上位机必须解析头部。解析失败时文件无效，不能用文件大小临时兜底。

### 10.7 OTA 流程

```text
1. 上位机读取 device_info_t，得到协议兼容范围、ota_max_kb、ota_chunk_max、当前固件版本。
2. 上位机解析 WCFW 头部并校验 company_id、payload_size、payload_crc16。
3. 上位机发送 CMD_OTA_START(ota_start_t)。
4. 设备 ACK 返回 sdk_result_t，status=OK 且 data=ota_ack_data_t 后进入接收状态。
5. 上位机按 chunk_index 发送 CMD_OTA_DATA(ota_data_hdr_t + data)。
6. 设备每片 ACK，next_chunk 指向期望的下一片。
7. 上位机全部发送后发送 CMD_OTA_VERIFY(ota_verify_t)。
8. 设备校验成功后 ACK。
9. 上位机按业务需要发送 CMD_DEVICE_RESET。
```

用户取消更新时，上位机发送 `CMD_OTA_ABORT`。设备端也必须实现 OTA 超时清理，避免残留升级状态影响后续业务请求。

收到 `CMD_OTA_ABORT` 后，设备端应立即清理 OTA 业务状态并返回 `CMD_OTA_ABORT_ACK`。ABORT 成功不表示链路中已经没有旧数据；发送端或 BLE 模块可能仍送达已经排队的 `CMD_OTA_DATA` 字节。设备端接收链路必须按 4.5.4 的半帧超时 / parser reset 规则恢复，否则反复开始和取消 OTA 时，残留半帧可能阻塞后续所有控制命令。

---

## 11. 设备端实现约定

### 11.1 C / MCU

- 热路径避免动态内存。
- 批量数组使用 `peek_size` / `unpack_item` 这类二步解析思想，先读数量，再逐项解析。
- 对超过当前缓存能力的数组请求直接拒绝或截断，不能写越界。

### 11.2 MicroPython

- `sdk_definition/open_sdk/examples/micropython_device/main.py` 是当前 MicroPython 模拟设备参考实现。
- 模拟设备可先打 log 确认收到手动触发、安全放电、充电控制、OTA 等命令，再逐步补真实业务。

---

## 12. 常见错误与处理

| 场景 | 处理 |
| --- | --- |
| 主服务 UUID 未暴露 | 上位机无法识别目标设备，设备端需检查 GATT 注册 |
| `device_info_t` 编码失败 | 设备端不得返回空包，应返回结构完整的设备信息 |
| `serial` 为空 | 设备信息无效，设备端必须提供非空序列号 |
| CRC8 错误 | 丢弃帧，不进入业务 |
| 请求超时 | 设备端应检查命令派发、ACK 回包和 Notify 发送节奏 |
| OTA 文件头解析失败 | 重置选择状态，要求用户重新选择 |
| OTA 中途取消 | 收到 `CMD_OTA_ABORT` 后，设备清理 OTA 状态 |

---

## 13. 版本快照

当前最终快照：

| 项 | 值 |
| --- | --- |
| 协议版本 | `0x1` |
| SOF | `0xAA` |
| LEN | 2 字节 little-endian |
| CMD | 1 字节完整命令 |
| CRC | CRC8 |
| 线协议单帧最大 Payload | 65535 字节 |
| 默认 SDK 实现最大 Payload | 1024 字节，可按设备内存调整 |
| 帧固定开销 | 6 字节 |
| Dashboard 初始化请求 Payload | 11 字节 |
| Dashboard 初始化请求完整帧 | 17 字节 |
| Dashboard 初始化响应 data | 27 字节 |
| Dashboard 初始化 ACK 完整帧 | 35 字节 |
| 高频 Dashboard Payload | 14 字节 |
| 高频 Dashboard 完整帧 | 20 字节 |
| 设备信息读取 | `CMD_DEVICE_GET_INFO` |
| DIS | 不使用 |
