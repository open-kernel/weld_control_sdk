# AGENTS.md

## Purpose

本文件面向第三方开发者和他们使用的 AI 编程助手。目标是指导你把 WeldControl BLE SDK 集成到自己的 App、桌面工具、MCU 固件或 MicroPython 设备项目中。

如果你正在使用 AI 辅助接入，请先让 AI 阅读：

- `README.md`
- `docs/protocol_spec_v1.0.md`
- `docs/compat_policy.md`
- 本文件
- 与目标平台对应的 SDK 目录和 `examples/` 示例

## What This SDK Provides

SDK 只提供数据层能力：

- BLE 自定义服务 UUID 和 TX/RX 特征 UUID
- SDK 帧编码与解码
- CRC、SEQ、payload 长度校验
- CMD 常量
- `sdk_result_t` 统一应答壳
- 设备信息、Dashboard、Settings、Self Check、OTA 等 payload 编解码
- C、MicroPython 设备端实现
- 设备端参考示例

SDK 不提供：

- 蓝牙扫描实现
- 蓝牙连接管理
- GATT 服务注册的完整平台封装
- UI 页面
- 数据库存储
- 重试队列和业务状态机

这些需要你在自己的项目里实现一层 transport / adapter，然后把收到的原始字节交给 SDK 解码，把 SDK 编码出的字节写到 BLE RX 特征或串口。

## Choose The Correct SDK

| 目标项目 | 推荐目录 | 说明 |
| --- | --- | --- |
| STM32、ESP-IDF、RTOS、裸机 C 工程 | `c/` | 编译 `sdk_codec.c`、`sdk_compat.c`、`feature_mask.c` 和 `payloads/*.c` |
| MicroPython 设备 | `micropython/` | 复制 `.py` 文件和 `payloads/` 到设备文件系统 |
| 设备端完整参考 | `examples/micropython_device/` | 展示 BLE IRQ、GATT、命令派发和模拟业务 |
| ESP-IDF 设备测试 | `examples/esp_idf_device_test/` | 展示 C SDK 与 ESP-IDF / 外置 BLE 模块结合 |

## BLE Contract

设备必须暴露 SDK 自定义主服务，并通过 TX/RX 两个特征收发 SDK 帧：

| 用途 | UUID |
| --- | --- |
| 主服务 | `5868A23F-877F-53F3-B2B6-E8D4FDF32F75` |
| TX 特征，设备到上位机 Notify | `373CDA7C-832F-5510-8CFC-F5A8E11BADDE` |
| RX 特征，上位机到设备 Write / Write Without Response | `A5255200-72A1-57AD-9306-1A99E8CFEE4B` |

外置 BLE 模块可使用 16-bit 兼容 UUID：

| 用途 | UUID |
| --- | --- |
| 主服务 | `A23F` |
| TX 特征 | `DA7C` |
| RX 特征 | `5200` |

App / 上位机每次连接后都应重新执行服务和特征发现，不要持久化 GATT handle。可以持久化设备记录、序列号、token 等业务数据，但不要把一次连接发现到的 service / characteristic handle 当成下次连接的固定值。

## Integration Flow For App Or Host

第三方 App / 上位机推荐流程：

1. 扫描包含 SDK Service UUID 的设备。
2. 连接设备。
3. 调用平台 BLE API 获取 services 和 characteristics。
4. 按 SDK UUID 常量匹配 TX Notify 和 RX Write 特征。
5. 开启 TX Notify。
6. 为当前物理连接创建一个 SDK device context。
7. 发请求时构造 `sdk_packet_t`，调用 `sdk_encode()` 得到 frame bytes。
8. 将 frame bytes 按平台限制写入 RX 特征。
9. 收到 Notify 后把原始 bytes 喂给 `sdk_decode()`。
10. 解出 packet 后用 `cmd + seq` 匹配请求应答。
11. 对 ACK payload 先解析 `sdk_result_t`，再根据 CMD 解析 `result.data`。
12. 页面退出、连接断开或鉴权失效时清理 pending request 和连接态。

典型调用形态：

1. 创建当前连接的 SDK device context。
2. 构造请求 packet：`cmd`、`seq`、`payload`。
3. 调用目标语言 SDK 的 `sdk_encode` 得到 frame bytes。
4. 使用 `sdk_frame_chunk` helper 将 frame bytes 按 MTU/模块限制切成 chunk。
5. 将 chunk 顺序写入 BLE RX 特征。
6. 收到 TX Notify bytes 后调用 `sdk_decode`。
7. 如果解出 packet，使用 `cmd + seq` 匹配请求。
8. 对 ACK payload 先解析 `sdk_result_t`，成功后再解析 `result.data` 中的业务实体。

Transport / adapter 必须额外处理链路层切块：

- BLE 默认 20 字节写入或 Notify 是链路层切块；每个 chunk 仍然只是同一 SDK frame 的原始字节流片段。
- 同一连接、同一方向的 SDK frame 必须串行发送；一个 frame 的 chunk 未发完前，不允许插入其他 frame 的 chunk。
- `sdk_frame_chunk_iter()` 处理已有 frame bytes；`sdk_frame_chunk_with_encode_iter()` 先调用 `sdk_encode()` 再迭代 chunk。
- `sdk_decode()` 不会代替业务层完成鉴权、限幅、功能支持或字段合法性判断。

## Integration Flow For Device Firmware

第三方设备端推荐流程：

1. 注册 SDK 主服务。
2. 注册 TX Notify 特征和 RX Write 特征。
3. 广播 SDK Service UUID。
4. 连接建立时创建 SDK device context。
5. RX 特征收到 bytes 后调用 `sdk_decode()`。
6. 解出 packet 后进入命令派发。
7. 先判断是否需要鉴权；未鉴权时只允许 `SDK_AUTH_FREE_COMMANDS` 里的命令。
8. 处理业务 payload。
9. 使用原请求 `seq` 返回 ACK。
10. ACK payload 必须使用 `sdk_result_t`。
11. Dashboard 高频上报、点焊记录、日志等事件包按协议直接上报，不套 `sdk_result_t`。

MicroPython 示例入口：

- `examples/micropython_device/main.py`

该示例展示了：

- BLE 初始化和广播
- TX/RX GATT 注册
- IRQ 中接收写入
- `sdk_decode()` 循环解析
- CMD 派发
- 配对、鉴权、设备信息、Dashboard、Settings、自检、OTA 和控制命令的模拟响应

C / ESP-IDF 示例入口：

- `examples/esp_idf_device_test/`

该示例展示了：

- C SDK 编译接入
- ESP-IDF 工程结构
- NimBLE / 外置 BLE 模块测试模式
- BOOT 按键配对确认流程
- token 持久化和模拟业务 ACK

## C Project Integration

将以下 C 源文件加入你的工程：

```text
c/sdk_codec.c
c/sdk_compat.c
c/feature_mask.c
c/payloads/device_info.c
c/payloads/dashboard.c
c/payloads/ota.c
c/payloads/settings.c
```

添加 include path：

```text
c/
c/payloads/
```

需要链路层切块 helper 时，包含头文件 `c/sdk_frame_chunk.h`。

C 典型调用形态：

```c
#include "sdk_codec.h"
#include "payloads/device_info.h"

sdk_device_t *dev = sdk_device_create(NULL, "device");

sdk_packet_t pkt = {
    .cmd = CMD_DEVICE_GET_INFO_ACK,
    .seq = request_seq,
};

uint8_t data[DEVICE_INFO_FIXED_SIZE];
device_info_t info = {0};
/* 填充 info 字段 */
device_info_pack(&info, data, sizeof(data));

uint8_t result[SDK_RESULT_HEADER_SIZE + DEVICE_INFO_FIXED_SIZE];
uint16_t result_len = sdk_result_pack(
    SDK_RESULT_STATUS_OK,
    SDK_RESULT_CODE_COMMON_NONE,
    data,
    DEVICE_INFO_FIXED_SIZE,
    result,
    sizeof(result)
);

memcpy(pkt.payload, result, result_len);
pkt.payload_len = result_len;

uint8_t frame[SDK_FRAME_OVERHEAD + SDK_MAX_PAYLOAD];
uint16_t frame_len = sdk_encode(dev, &pkt, frame, sizeof(frame));
/* 将 frame[0..frame_len) 通过 TX Notify 或串口发出 */
```

在 MCU 上如果不能使用 `malloc/free`，可在编译前定义 `SDK_MALLOC` 和 `SDK_FREE`，或避免动态创建，把 context 生命周期固定在连接对象中。

## MicroPython Project Integration

复制核心文件到设备文件系统：

```sh
mpremote cp micropython/sdk_codec.py :
mpremote cp micropython/sdk_commands.py :
mpremote cp micropython/sdk_compat.py :
mpremote cp micropython/sdk_version.py :
mpremote cp micropython/sdk_frame_chunk.py :
mpremote cp micropython/feature_mask.py :
mpremote mkdir :payloads
mpremote cp micropython/payloads/*.py :payloads/
```

参考 `examples/micropython_device/main.py` 实现：

- 广播构建
- GATT 服务注册
- IRQ 回调
- token 存储
- CMD 派发
- ACK 发送

## Required Commands To Implement First

第三方设备端最小可用接入建议先实现：

| CMD | 用途 |
| --- | --- |
| `CMD_DEVICE_GET_INFO` | App 扫描添加和版本兼容判断 |
| `CMD_DEVICE_PAIR_REQUEST` | 首次配对 |
| `CMD_DEVICE_AUTH` | 已配对设备重连鉴权 |
| `CMD_READ_DASHBOARD_INIT` | 进入 Dashboard 的基础信息 |
| `CMD_DASHBOARD_START` / `CMD_DASHBOARD_STOP` | 实时数据流开始和停止 |
| `CMD_SETTINGS_READ_CURRENT` | 读取当前设置 |
| `CMD_SETTINGS_PROFILE` | 应用设置参数 |

`CMD_DEVICE_PAIR_REQUEST` 的 payload 使用 `pair_request_t`：

```text
client_id: u64 little-endian
name_len : u8
name     : UTF-8 bytes，最多 36 bytes
```

`client_id` 是上位机安装实例 ID，不是设备 MAC；设备端只应把它当作配对来源标识。

如果暂时不支持 OTA、自检、安全放电、充电控制等功能，应返回：

```text
status = SDK_RESULT_STATUS_NOT_SUPPORTED
code   = SDK_RESULT_CODE_COMMON_NONE 或对应业务 code
data   = empty
```

不要静默丢包，也不要返回空 ACK。

## Result And Error Handling

所有请求 ACK 的 payload 都应先解析为：

```text
sdk_result_t:
  status: u8
  code  : u8
  data  : bytes
```

约定：

- `SDK_RESULT_STATUS_OK`：业务成功，按当前 ACK 的固定实体解析 `data`。
- `SDK_RESULT_STATUS_PENDING`：设备已受理，请保持等待，后续仍用相同 CMD/SEQ 或协议规定的最终 ACK 完成。
- `SDK_RESULT_STATUS_AUTH_INVALID`：token 失效，上位机应删除本地 token 并引导重新配对。
- `SDK_RESULT_STATUS_NOT_SUPPORTED`：设备不支持该功能，上位机应隐藏或禁用入口。
- `SDK_RESULT_STATUS_FAIL` / `DEVICE_ERROR`：设备处理失败，应展示真实失败原因。

空 payload、`status = 0`、CRC 错、SEQ 不匹配都应视为协议错误。

## Version And Compatibility

设备必须在 `device_info_t` 和 `dashboard_init_t` 返回：

- `protocol_version`
- `protocol_min_version`

上位机判断：

```text
SDK_MIN_PROTOCOL_VERSION <= device.protocol_version <= SDK_PROTOCOL_VERSION
```

如果：

```text
device.protocol_min_version > SDK_PROTOCOL_VERSION
```

说明当前上位机过旧，应提示用户升级上位机软件后再连接。

不要用 `feature_mask` 判断协议兼容。`feature_mask` 只表示设备支持哪些功能。

## Payload Evolution Rules

如果你要基于 SDK 做扩展：

1. 已发布字段不能改 offset、长度、单位和语义。
2. 新字段只能追加到 payload 尾部。
3. 不能修改 `SDK_TOKEN_LEN`。
4. 不能复用旧 CMD 表达新语义。
5. 破坏旧语义时新增 CMD 或新 payload 实体。
6. 解包时只读取已知前缀，不要用 payload 长度猜协议版本。

推荐解包结构：

```text
if data.length < SIZE_V1:
  return null

fields = read_v1_fields(data)

if data.length >= SIZE_V2:
  read_v2_tail_fields(data, fields)

return fields
```

## Transport Rules

- 每个物理连接必须有独立的 SDK device context。
- 收到的数据可能拆包、粘包或包含错误字节，必须循环调用 `sdk_decode()` 并按 `consumed` 推进输入缓冲。
- BLE 20 字节链路切块只切完整 SDK frame 的 raw bytes，不引入 SDK frame 分片状态。
- 同一连接、同一方向的 frame bytes 必须串行发送，不能交错发送不同 frame 的 chunk。
- 发送时保留请求 `seq`，ACK 必须原样带回。
- 上位机请求匹配使用 `cmd + seq`，不要只靠 `seq`。
- BLE 单次写入通常按 20 字节处理；如协商更大 MTU，也要保证对端支持。
- 设备主动上报不需要上位机 ACK。

## SDK Validation Boundary

SDK 的 pack/unpack 主要保证 wire 结构、长度、小端序和基础空指针安全。以下业务合法性仍由接入项目负责：

- `device_info_t.serial_len` 必须大于 0，且 manufacturer/model/serial 应是真实有效值。
- `trigger_mode`、ESR 质量、故障日志类型、Dashboard 状态码等枚举值需要按业务约束校验。
- Settings 参数限幅、OTA 大小和厂商 ID 匹配、控制命令是否支持，必须在业务层判断。
- 鉴权白名单和 token 状态由设备端命令派发层处理；SDK 不会自动拒绝未鉴权命令。
- 不支持的功能必须返回 `SDK_RESULT_STATUS_NOT_SUPPORTED`，不能静默丢包或返回空 ACK。

## Do Not Do This

- 不要把 SDK 当成完整 BLE 框架；它不是扫描/连接库。
- 不要缓存 GATT handle 跨连接复用。
- 不要返回空 ACK 表示成功。
- 不要把业务错误码塞进 `data[0]`。
- 不要只实现 Dashboard 上报而不实现 `CMD_DASHBOARD_START/STOP`。
- 不要伪造空 serial number；设备序列号必须稳定不变避免更新重置发生变化也不要直接使用敏感ID例如直接裸CPUID等避免和自己的固件激活使用的唯一标识一致，可以是hash加盐后的CPUID。
- 不要用固件版本代替协议版本。
- 不要让不同物理连接共用同一个 parser context。

## Validation

第三方项目至少做以下验证：

1. 能扫描到 SDK Service UUID。
2. 能发现 TX/RX 特征。
3. `CMD_DEVICE_GET_INFO` 返回 `SDK_RESULT_STATUS_OK` 和可解析的 `device_info_t`。
4. 配对流程能覆盖未进入配对模式、等待确认、确认成功、拒绝和超时。
5. 已配对重连时 `CMD_DEVICE_AUTH` 能成功。
6. token 失效时能返回 `SDK_RESULT_STATUS_AUTH_INVALID`。
7. Dashboard start 后有实时上报，stop 后停止上报。
8. Settings read/apply 能使用协议实体 pack/unpack。
9. 不支持的功能返回 `SDK_RESULT_STATUS_NOT_SUPPORTED`。
10. 断开连接后清理 context，重连创建新 context。

如果你修改了 SDK 源码，再运行对应平台的语法/编译检查：

以下命令默认从 SDK 根目录运行，也就是包含 `c/`、`micropython/`、`examples/` 的目录。

```sh
python3 -B -c "from pathlib import Path; files=[*Path('micropython').glob('*.py'), *Path('micropython/payloads').glob('*.py'), Path('examples/micropython_device/main.py')]; [compile(p.read_text(), str(p), 'exec') for p in files]"
```

```sh
cc -std=c99 -Ic -Ic/payloads -fsyntax-only \
  c/sdk_codec.c \
  c/sdk_compat.c \
  c/feature_mask.c \
  c/payloads/device_info.c \
  c/payloads/dashboard.c \
  c/payloads/ota.c \
  c/payloads/settings.c \
  c/main.c
```

## Expected AI Output

当 AI 帮你接入 SDK 后，最终说明应包含：

- 使用了哪套 SDK：C / MicroPython。
- 哪些文件被复制、引用或编译进你的项目。
- BLE transport 如何映射到 TX/RX。
- 已实现哪些 CMD。
- 未实现的 CMD 如何返回 `NOT_SUPPORTED`。
- token、设备序列号和协议版本如何保存或返回。
- 已执行的验证项。
- 仍需真机验证的路径。
