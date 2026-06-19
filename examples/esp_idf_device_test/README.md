# ESP-IDF C SDK 设备测试工程

该工程用于在 ESP32-S3 上验证 `c/` 中的 C 语言 SDK、BLE GATT 链路和上位机协议交互。

硬件图片、测试流程、业务流程和通用注意事项见 `../README.md`。

## 工程范围

| 项目 | 内容 |
| --- | --- |
| 芯片目标 | `esp32s3` |
| 默认运行模式 | `WELD_RUNTIME_MODULE2_TRANSPORT` |
| BLE 链路 | 外置 BLE 模块 2 透明传输，或切换到 ESP-IDF NimBLE GATT Server |
| SDK 来源 | 直接编译 `../../c`，不复制协议实现 |
| 配置入口 | `sdkconfig.defaults` |

该工程是协议联调 demo，不是量产固件模板。OTA、手动触发、安全放电和充电控制均为模拟业务，不会写 flash 或控制真实功率硬件。

## 当前测试硬件

当前测试板为 ESP32-S3 双 Type-C 开发板，带 CH343P USB 转串口、BOOT / RST 按键和板载 WS2812 RGB LED。

| 项目 | 当前示例配置 |
| --- | --- |
| BOOT 按键 | `WELD_BOOT_BUTTON_GPIO`，当前代码为 GPIO9；其他板卡可能是 GPIO0 或其他引脚 |
| WS2812 RGB LED | `WELD_STATUS_LED_GPIO`，当前代码为 GPIO48 |
| 外置 BLE 模块 2 UART | ESP TX=GPIO35，ESP RX=GPIO36，115200 8N1 |
| 外置 BLE 模块 3 测试 | 使用 GPIO47 / GPIO48；GPIO48 与板载 RGB LED 冲突，切换模式前需确认没有同时占用 |

更换开发板时，必须先核对 BOOT、RGB LED、UART、RST 和外置 BLE 模块接线，再修改 `main/main.c` 或对应模块测试文件中的 GPIO。

## 构建与烧录

进入工程目录：

```sh
cd examples/esp_idf_device_test
```

激活 ESP-IDF 环境：

```sh
. ~/.espressif/tools/activate_idf_v6.0.1.sh
```

构建：

```sh
idf.py set-target esp32s3
idf.py --ccache build
```

烧录：

```sh
idf.py --ccache -p /dev/cu.usbmodem14201 -b 460800 flash
```

监视串口：

```sh
idf.py -p /dev/cu.usbmodem14201 -b 115200 monitor
```

如果串口不是 `/dev/cu.usbmodem14201`，替换成实际的 `/dev/cu.*` 设备。

## 运行模式

`main/main.c` 顶部的 `s_runtime_mode` 控制启动分支。

| 模式 | 说明 |
| --- | --- |
| `WELD_RUNTIME_MODULE2_TRANSPORT` | 当前默认模式。ESP32-S3 通过 UART1 连接外置 BLE 模块 2，模块负责 BLE 广播、GATT 服务和 Notify / Write，ESP 负责 SDK 协议编解码和模拟业务 |
| `WELD_RUNTIME_MODULE_AT_TEST` | 外置 BLE 模块 AT 初始化测试，只用于验证模块命令和 UUID 配置 |
| `WELD_RUNTIME_NIMBLE` | ESP32-S3 内置 NimBLE GATT Server 模拟设备 |

## 外置 BLE 模块 2

模块 2 透明传输模式使用 `main/ble_module_transport.c`。

默认配置：

```text
AT+UUIDS=A23F
AT+UUIDN=DA7C
AT+UUIDW=5200
AT+NAME=WeldControl
AT+TXPOWER=0
AT+REBOOT=1
```

重启后会查询 `AT+TXPOWER?` 和 3 个 UUID。进入透明传输后，上位机写 RX UUID 的数据会从模块 UART 到 ESP；ESP 编码后的 SDK 帧按 20 字节块写回 UART，由模块通过 TX UUID Notify 发给上位机。

### 接收链路恢复

透明传输模式下，UART 每次读到的数据不保证刚好等于一个完整 SDK frame。上位机连续执行“开始 OTA -> 取消 OTA -> 重新开始 OTA”时，设备已经返回 `CMD_OTA_ABORT_ACK` 后，模块仍可能继续送达已经排队的 `CMD_OTA_DATA` 残留字节。如果这段数据只包含半帧，`sdk_decode()` 会停留在 `SDK_DECODE_NEED_MORE`。

本 demo 在 `main/main.c` 中做了两层恢复：

1. 外置模块会话 reset 和 NimBLE disconnect 时调用 `sdk_device_reset_parser()`。
2. RX 链路记录未完成帧的等待时间，超过 `WELD_DECODE_PARTIAL_TIMEOUT_US` 后调用 `sdk_device_reset_parser()`，当前默认值为 1000ms。

测试时可以反复点击开始更新和取消更新。预期日志是：设备先返回 `CMD_OTA_ABORT_ACK`，如果之后收到残留半帧，下一条命令到来前会打印 `SDK parser partial frame timeout` 和 `SDK parser reset: partial-frame-timeout`，随后新的 `CMD_OTA_START`、设置读取或重置命令应继续正常解析。

## AT 测试模式

`WELD_RUNTIME_MODULE_AT_TEST` 只用于检查外置 BLE 模块命令，不是常规协议联调入口。

默认接线和命令模板在 `main/ble_module_at_test.c`：

| 模块 | 默认接线 | 用途 |
| --- | --- | --- |
| 模块 1 | ESP TX=GPIO42，ESP RX=GPIO41，RST=GPIO40 | 早期 AT 命令兼容测试 |
| 模块 2 | ESP TX=GPIO35，ESP RX=GPIO36 | SDK 16-bit UUID 和透明传输测试 |
| 模块 3 | ESP TX=GPIO48，ESP RX=GPIO47 | 额外模块命令测试；GPIO48 与 RGB LED 冲突 |

AT 测试分支复用 `UART1`，不占用 `UART0` 控制台，避免日志串进 BLE 模块 AT 口。

## 注意事项

- `sdkconfig` 是本地生成文件，已被 `.gitignore` 排除；发布和移植以 `sdkconfig.defaults` 为准。
- 默认设备信息、token、厂商 ID、产品 ID 和固件版本都是 demo 值，正式固件应使用自己的稳定参数。
- 不要修改 SDK 数据包格式。协议对接问题优先提交 issue 或 PR。
