# MicroPython 设备模拟示例

`main.py` 是 WeldControl BLE SDK 的 MicroPython 设备端参考实现，用于验证配对、鉴权、设备信息、Dashboard、设置、自检、OTA 和控制命令的协议交互。

硬件说明和通用注意事项见 `../README.md`。

## 定位

该示例偏协议流程参考，适合快速阅读和调试：

- BLE 广播和 GATT 注册
- SDK frame 解码与编码
- CMD 派发
- 配对、等待确认、确认、拒绝和鉴权失效
- Dashboard 初始化、实时数据流和日志上报
- Settings 当前参数、自检、应用预设和恢复设置
- OTA start / data / verify / abort 的内存态模拟

示例中的业务动作均为协议联调模拟逻辑；真实硬件动作应在设备固件中替换为实际控制代码。

## 注意事项

- 需要 MicroPython 固件提供 `bluetooth` 模块和 BLE peripheral 能力。
- 不同开发板的按键、LED 和存储能力不同，示例中的配对确认方式可以按目标硬件调整。
- 示例 token 和设备信息为测试默认值，正式设备应使用稳定序列号、真实厂商 ID、产品 ID 和可靠 token 存储。
- 不要修改 SDK 数据包格式。协议对接问题优先提交 issue 或 PR。

## 部署到设备

将核心 MicroPython SDK 文件和本示例一起复制到设备根目录：

```sh
mpremote cp micropython/sdk_codec.py :
mpremote cp micropython/sdk_commands.py :
mpremote cp micropython/sdk_compat.py :
mpremote cp micropython/sdk_version.py :
mpremote cp micropython/feature_mask.py :
mpremote mkdir :payloads
mpremote cp micropython/payloads/*.py :payloads/
mpremote cp examples/micropython_device/main.py :
```
