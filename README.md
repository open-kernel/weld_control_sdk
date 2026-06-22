# WeldControl BLE SDK

本目录是 WeldControl 开放 SDK 发布源，用于设备端接入 WeldControl BLE 数据协议。

SDK 只定义 BLE 自定义服务、帧格式、CMD、payload 编解码、协议兼容策略和设备端示例，不包含 App 页面、蓝牙扫描连接管理、数据库存储或业务 UI。

## 快速开始

1. 先阅读协议规范：`docs/protocol_spec_v1.0.md`。
2. 根据设备平台选择 SDK：
   - C / MCU / ESP-IDF：使用 `c/`。
   - MicroPython：使用 `micropython/`。
3. 参考示例：
   - MicroPython 设备模拟：`examples/micropython_device/main.py`。
   - ESP-IDF 设备测试工程：`examples/esp_idf_device_test/`。
4. 集成后至少实现设备信息、配对、鉴权、Dashboard start/stop 和当前设置读取。
5. OTA 固件文件需要 WCFW 头部，生成方式见 `scripts/add_wcfw_header.md`。
6. 外挂蓝牙模块选择：标准BLE蓝牙透传模块都可以用，传输速度不限(数据结构做了优化 多慢的速度都能非常流畅通信)。模块最好支持自定义服务UUID 128位或者16位的都行，如果不能自定义UUID 至少可以自定义蓝牙广播名称，不然程序没法识别到设备，一般模块都支持但要留意。模块优先使用可以外接天线的更灵活，已随机购买模块测试情况：

| 模块                 | 测试评价                                                                                                                                                    |
|--------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|
| 大熊智能DX32-UM1AT     | 信号最好，稳定有屏蔽罩，外接天线，2.95元，但是广播包里面塞了一些无用信息且不能自定义完整广播包，服务列表有Device Info包 里面是他们公司和模块型号信息，还有一些其他不知道干什么用的服务定义，不太干净清爽我不太喜欢但是信号确实不错还有屏蔽罩，有问题客服解答少让看文档（文档基本能解决大部分问题）。 |
| 度云物联M01（9AS）（从机版本） | 信号中等，无屏蔽罩，外接天线，2.8元，关播可以完全自定义，服务列表有一个不知道干什么的服务，整体比较清爽，完整自定义广播包我很喜欢就是没屏蔽罩型号没有大熊好，客服回复慢但是都回答了。                                                            |
| 妙享科技MX-01PS        | 信号较弱（在这三个中比较，pcb天线可能也是一个因素），无屏蔽罩，PCB天线，2.97元，广播部分可自定义和大熊差不多不能完全自定义，  服务列表有一个用来OTA升级蓝牙模块本身固件的服务和一个不知道干什么的服务，整体还好，客服好一点会给技术对接。                            |

服务包广播包那些东西，一般可以通过找商家定制固件解决(可能加钱要批量，我没问)，价格不涨不介意额外信息推荐大熊的，反正不影响使用。以上评价仅代表个人使用体验观点，不具备采购建议，仅供参考。

7. 可使用对应的上位机小程序扫描、配对和测试设备：

<img src="assets/weldcontrol_miniprogram_qr.jpg" alt="WeldControl 上位机小程序二维码" width="220">

## Transport Adapter 必做项

SDK 不直接管理 BLE 传输层。第三方项目需要在 SDK 外实现 transport / adapter：

1. 每个物理连接创建独立的 SDK device context。
2. RX 收到原始字节后循环调用 `sdk_decode()`，并按 `consumed` 推进输入缓冲。
3. BLE 默认 20 字节写入/Notify 限制属于链路层切块；发送方应先编码完整 SDK frame，再按 MTU 或模块限制切 raw bytes。
4. 同一连接、同一方向的 SDK frame 必须串行发送；一个 frame 的 chunk 未发完前，不允许插入其他 frame 的 chunk。
5. 可使用 `sdk_frame_chunk` helper 统一处理 frame chunk 迭代，避免各平台重复手写边界切片。
6. `sdk_decode()` 只负责帧解码和 CRC 校验，不会代替业务层做鉴权、限幅或功能支持判断。
7. transport adapter 必须处理未完成帧的恢复：物理连接断开、外置模块会话重建，或 `sdk_decode()` 长时间停留在 `NEED_MORE` 时，应调用 `sdk_device_reset_parser()` 丢弃旧半帧，避免 OTA abort 后的残留 `CMD_OTA_DATA` 吞掉后续命令。

## 目录结构

| 目录 | 用途 |
| --- | --- |
| `c/` | C99 SDK 源码，供 MCU / ESP-IDF 等固件集成 |
| `micropython/` | MicroPython SDK 源码，供设备模拟和 MicroPython 固件集成 |
| `docs/` | 协议规范、兼容策略和对接文档 |
| `scripts/` | SDK 辅助脚本，例如 WCFW 固件打包 |
| `examples/` | 设备端集成示例，不属于核心 SDK 源码 |

## 核心协议入口

设备必须暴露 SDK 自定义主服务，并通过 TX/RX 两个特征收发 SDK 帧。

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

上位机软件通过扫描这些UUID来识别设备，如果无法识别到广播中声明的服务UUID，兜底机制是通过广播中的蓝牙名称【WeldControl】识别。
## 核心文件

- 协议规范：`docs/protocol_spec_v1.0.md`
- 兼容策略：`docs/compat_policy.md`
- 第三方 AI 集成指南：`AGENTS.md`
- 示例硬件和 demo 注意事项：`examples/README.md`
- 固件打包工具说明：`scripts/add_wcfw_header.md`
- MicroPython 设备模拟：`examples/micropython_device/main.py`
- ESP-IDF 设备测试工程：`examples/esp_idf_device_test/`

## 厂商 ID 使用

`company_id` 是 WeldControl SDK 内部使用的 16-bit 厂商标识，用于设备信息和 OTA 固件归属校验。它不是蓝牙组织分配的 Company Identifier，但在同一上位机生态内必须避免重复。

当前已占用：

| ID | 用途 |
| --- | --- |
| `0xAAAA` | SDK 示例和 demo 默认值，仅用于联调，不可正式产品沿用 |

第三方设备接入前，请先检查本仓库的厂商 ID 登记楼，建议 GitHub 和 Gitee 两边都看一下，避免和其他厂商或项目冲突：

- [GitHub Issue #1](https://github.com/open-kernel/weld_control_sdk/issues/1)
- [Gitee Issue IJW16G](https://gitee.com/open-kernel/weld_control_sdk/issues/IJW16G)

建议在常用平台的对应 Issue 回复：

- 厂商或项目名称
- 计划使用的 `company_id` 或 ID 范围
- 固件类型，例如 `ble` / `uart`
- 联系方式或维护人

正式固件、设备信息包和 WCFW 固件头中的 `company_id` 必须保持一致，否则上位机会拒绝 OTA 或提示厂商不匹配。

## OTA 固件打包

上位机选择的 OTA 文件需要带 64 字节 WCFW 头部，用于展示版本、厂商、大小和 CRC 信息。设备真正接收的是 header 后面的 payload 数据。

OTA 测试时需要覆盖“开始更新 -> 取消 -> 重新开始”的连续操作。`CMD_OTA_ABORT` 成功后，传输层仍可能收到已经在路上的残留 `CMD_OTA_DATA` 半帧；设备端应先清理 OTA 业务状态，再依赖 transport adapter 的 parser reset / 半帧超时恢复，保证后续 `CMD_OTA_START`、鉴权、设置读取和重置命令不会被旧半帧阻塞。

给已有固件添加 WCFW 头部：

```sh
python3 scripts/add_wcfw_header.py \
  --input firmware.bin \
  --company-id 0xAAAA \
  --type ble \
  --version 1.2.3 \
  --build-id 0x20260616
```

生成测试用 payload：

```sh
python3 scripts/add_wcfw_header.py --mock --company-id 0xAAAA --version 1.2.3 --payload-size 65536
```

完整参数见 `scripts/add_wcfw_header.md`。

## 兼容约束

不建议第三方开发者直接修改 SDK 协议实现。遇到问题时优先提交 issue 或 PR，并附上设备端日志、上位机日志、协议版本和复现步骤。

禁止私自改变已发布数据包格式、CMD 编号、字段 offset、字段长度、字段单位和字段语义。否则可能导致设备无法被上位机识别、无法连接、鉴权失败或控制命令被错误解析。

## 发布约束

发布 SDK 时应包含：

- `c/`
- `micropython/`
- `docs/`
- `scripts/`
- `examples/`
- 本 `README.md`
- `AGENTS.md`

发布 SDK 时不应包含：

- IDE 配置，例如 `.idea/`
- Python 缓存，例如 `__pycache__/`
- 本地虚拟环境，例如 `.venv/`
- 构建产物，例如 `build/`、`cmake-build-debug/`
- ESP-IDF 本地构建产物，例如 `build_open_sdk/`
- 系统临时文件，例如 `.DS_Store`

## 维护要求

1. 协议常量、CMD、payload 字段或兼容策略变化必须由 SDK 维护者统一发布。
2. 已发布协议字段只能尾部追加，不能修改既有字段的 offset、长度、单位和语义。
3. 示例代码可以模拟业务动作，但协议状态、ACK、错误码和 payload 格式必须与核心 SDK 保持一致。
4. `examples/` 只能放完整示例工程或示例入口；核心 SDK 不依赖 `examples/`。
