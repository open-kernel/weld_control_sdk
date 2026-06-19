"""SDK 命令字定义。

说明：命令字按功能区间分配，请勿复用已发布值。
"""

# 系统 / 配对 / 鉴权命令（0x00-0x0F）
CMD_PING                = 0x00    # 心跳
CMD_PONG                = 0x80    # 心跳应答
CMD_DEVICE_GET_INFO            = 0x01    # 获取设备信息
CMD_DEVICE_GET_INFO_ACK        = 0x81    # 设备信息应答
CMD_DEVICE_RESET               = 0x02    # 设备复位
CMD_DEVICE_RESET_ACK           = 0x82    # 设备复位应答（发送后设备重启）
CMD_DEVICE_PAIR_REQUEST        = 0x03    # 上位机发起配对请求
CMD_DEVICE_PAIR_REQUEST_ACK    = 0x83    # 设备回复配对确认
CMD_DEVICE_PAIR_UNPAIR              = 0x04    # 上位机发起解除配对
CMD_DEVICE_PAIR_UNPAIR_ACK          = 0x84    # 设备回复解除配对确认
CMD_DEVICE_AUTH                = 0x05    # 上位机发起 Token 鉴权登录
CMD_DEVICE_AUTH_ACK            = 0x85    # 设备回复鉴权确认
CMD_DEVICE_AUTH_REQUIRED       = 0x86    # 设备要求重新配对/鉴权

# 免鉴权命令白名单：仅用于建立配对/鉴权前的基础握手。
SDK_AUTH_FREE_COMMANDS = (
    CMD_PING,
    CMD_DEVICE_GET_INFO,
    CMD_DEVICE_PAIR_REQUEST,
    CMD_DEVICE_AUTH,
)

# OTA 命令（0x10-0x1F）
CMD_OTA_START           = 0x10    # OTA 开始
CMD_OTA_START_ACK       = 0x90    # OTA 开始应答
CMD_OTA_DATA            = 0x11    # OTA 数据包
CMD_OTA_DATA_ACK        = 0x91    # OTA 数据应答（含期望下一片索引）
CMD_OTA_VERIFY          = 0x12    # OTA 校验
CMD_OTA_VERIFY_ACK      = 0x92    # OTA 校验应答
CMD_OTA_ABORT           = 0x13    # OTA 取消
CMD_OTA_ABORT_ACK       = 0x93    # OTA 取消应答

# 读命令（0x20-0x3F）
CMD_READ_DASHBOARD_INIT      = 0x20    # 读取 Dashboard 初始化数据
CMD_READ_DASHBOARD_INIT_ACK  = 0xA0    # Dashboard 初始化数据应答
CMD_SETTINGS_READ_CURRENT = 0x21  # 读取设置页设备当前参数
CMD_SETTINGS_READ_CURRENT_ACK = 0xA1 # 设置页设备当前参数应答
CMD_SETTINGS_READ_SELF_CHECK = 0x22 # 读取设置页设备自检数据
CMD_SETTINGS_READ_SELF_CHECK_ACK = 0xA2    # 设置页设备自检数据应答

# 写命令（0x40-0x5F）
CMD_SETTINGS_PROFILE = 0x40 # 应用一个设置页参数到设备
CMD_SETTINGS_PROFILE_ACK = 0xC0 # 设置页参数应用应答
CMD_SETTINGS_RESET      = 0x41    # 恢复设置页设备参数
CMD_SETTINGS_RESET_ACK  = 0xC1    # 恢复设置页设备参数应答
CMD_DASHBOARD_START     = 0x42    # 开始 Dashboard 实时上报
CMD_DASHBOARD_START_ACK = 0xC2    # 开始 Dashboard 实时上报应答
CMD_DASHBOARD_STOP      = 0x43    # 停止 Dashboard 实时上报
CMD_DASHBOARD_STOP_ACK  = 0xC3    # 停止 Dashboard 实时上报应答
CMD_MANUAL_TRIGGER      = 0x44    # 手动触发点焊
CMD_MANUAL_TRIGGER_ACK  = 0xC4    # 手动触发点焊应答
CMD_SAFE_DISCHARGE      = 0x45    # 开始安全放电
CMD_SAFE_DISCHARGE_ACK  = 0xC5    # 开始安全放电应答
CMD_SAFE_DISCHARGE_STOP = 0x46    # 停止安全放电
CMD_SAFE_DISCHARGE_STOP_ACK = 0xC6 # 停止安全放电应答
CMD_CHARGE_START        = 0x47    # 开始充电
CMD_CHARGE_START_ACK    = 0xC7    # 开始充电应答
CMD_CHARGE_PAUSE        = 0x48    # 暂停充电
CMD_CHARGE_PAUSE_ACK    = 0xC8    # 暂停充电应答

# 事件上报（0x60-0x6F），设备→上位机，无需应答
CMD_DASHBOARD_COMPACT   = 0x60    # Dashboard 高频紧凑状态上报
CMD_DASHBOARD_WELD_RECORDS = 0x61 # Dashboard 点焊记录批量上报
CMD_DASHBOARD_LOGS      = 0x62    # Dashboard 诊断日志批量上报
