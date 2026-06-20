#ifndef SDK_PAYLOADS_DASHBOARD_H
#define SDK_PAYLOADS_DASHBOARD_H

/**
 * @file dashboard.h
 * @brief Dashboard payload 编解码。
 */

#include <stdbool.h>
#include <stdint.h>

#include "../sdk_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t setting_voltage_max_mv;     /**< 设置项：充电电压上限，同时作为电压环和波形电压轴上限，单位 mV */
  uint32_t stat_welds_total;           /**< 统计项：设备累计点焊次数 */
  uint32_t stat_welds_task;            /**< 统计项：本次任务点焊次数 */
  uint16_t setting_weld_pre_ms10;      /**< 设置项：预热脉冲时间，单位 0.1ms */
  uint16_t setting_weld_cooling_ms10;  /**< 设置项：冷却时间，单位 0.1ms */
  uint16_t setting_weld_main_ms10;     /**< 设置项：主脉冲时间，单位 0.1ms */
  uint16_t protocol_version;           /**< 设备当前 SDK 协议版本 */
  uint8_t firmware_major;              /**< 固件版本 major */
  uint8_t firmware_minor;              /**< 固件版本 minor */
  uint8_t firmware_patch;              /**< 固件版本 patch */
  uint32_t firmware_build_id;          /**< 固件 build id */
  uint16_t protocol_min_version;       /**< 设备要求的最低 App 协议版本 */
  uint16_t setting_single_cap_voltage_mv; /**< 设置项：单电容限压，单位 mV */
  uint8_t setting_trigger_mode;        /**< 设置项：触发模式：0 未设置，1 手动，2 自动 */
  uint16_t setting_auto_delay_ms;      /**< 设置项：自动触发延迟，单位 ms */
} dashboard_init_t;

typedef struct {
  uint16_t app_protocol_version;      /**< App 当前使用的 SDK 协议版本 */
  uint16_t app_min_protocol_version;  /**< App 可解析的最低设备协议版本 */
  uint8_t app_version_major;          /**< App 版本 major */
  uint8_t app_version_minor;          /**< App 版本 minor */
  uint8_t app_version_patch;          /**< App 版本 patch */
  uint32_t app_build_id;              /**< App 构建号；未知时填 0 */
} dashboard_init_request_t;

typedef struct {
  uint16_t voltage_mv;                /**< 当前储能电压，单位 mV */
  uint16_t weld_current_a;            /**< 当前点焊电流，单位 A */
  uint16_t charge_current_ma;         /**< 充电电流，单位 mA */
  uint16_t voltage_cap_1_mv;          /**< 电容 1 实时电压，单位 mV */
  uint16_t voltage_cap_2_mv;          /**< 电容 2 实时电压，单位 mV */
  int8_t temperature_capacitor_c;     /**< 电容器组温度，单位 °C，整数 */
  int8_t temperature_mos_c;           /**< MOS 阵列温度，单位 °C，整数 */
  uint8_t machine_status;             /**< 设备运行状态码，占 wire status_flags 低 4 bit */
  uint8_t charge_mode_code;           /**< 充电模式码，占 wire status_flags 高 4 bit */
  uint8_t discharge_status;           /**< 放电状态码，占 wire flags 低 4 bit */
  uint8_t undefined_status;           /**< 未定义状态码，占 wire flags 高 4 bit */
} dashboard_compact_t;

#define DASHBOARD_INIT_REQUEST_PAYLOAD_SIZE 11 /**< 初始化请求 payload 长度 */
#define DASHBOARD_INIT_PAYLOAD_SIZE_V1 32      /**< 初始化响应 V1 payload 长度 */
#define DASHBOARD_INIT_PAYLOAD_SIZE DASHBOARD_INIT_PAYLOAD_SIZE_V1 /**< 当前初始化响应 payload 长度 */
#define DASHBOARD_COMPACT_PAYLOAD_SIZE 14      /**< 高频紧凑状态 payload 长度 */
#define DASHBOARD_WELD_RECORD_PAYLOAD_SIZE 6   /**< 单条点焊记录 payload 长度 */
#define DASHBOARD_LOG_PAYLOAD_SIZE 9           /**< 单条诊断日志 payload 长度 */
#define DASHBOARD_WELD_RECORD_MAX_ITEMS ((SDK_MAX_PAYLOAD - 1u) / DASHBOARD_WELD_RECORD_PAYLOAD_SIZE) /**< 单包最大点焊记录数 */
#define DASHBOARD_LOG_MAX_ITEMS ((SDK_MAX_PAYLOAD - 1u) / DASHBOARD_LOG_PAYLOAD_SIZE) /**< 单包最大诊断日志数 */
#define DASHBOARD_MACHINE_STATUS_MASK 0x0F     /**< status_flags 低 4 bit：设备运行状态 */
#define DASHBOARD_CHARGE_MODE_MASK 0x0F        /**< status_flags 高 4 bit：充电模式 */
#define DASHBOARD_CHARGE_MODE_SHIFT 4          /**< status_flags 中充电模式的位移 */
#define DASHBOARD_DISCHARGE_STATUS_MASK 0x0F   /**< flags 低 4 bit：放电状态 */
#define DASHBOARD_UNDEFINED_STATUS_MASK 0x0F   /**< flags 高 4 bit：预留状态 */
#define DASHBOARD_UNDEFINED_STATUS_SHIFT 4     /**< flags 中预留状态的位移 */

#define DASHBOARD_MACHINE_STATUS_WAITING_DATA 0 /**< 尚未收到有效运行数据 */
#define DASHBOARD_MACHINE_STATUS_NOT_READY 1    /**< 设备未就绪 */
#define DASHBOARD_MACHINE_STATUS_READY 2        /**< 设备运行就绪 */
#define DASHBOARD_MACHINE_STATUS_FAULT 3        /**< 设备异常 */
#define DASHBOARD_MACHINE_STATUS_VOLTAGE_LOW 4  /**< 储能电压低于禁焊电压 */

#define DASHBOARD_CHARGE_MODE_UNKNOWN 0          /**< 未知状态 */
#define DASHBOARD_CHARGE_MODE_STANDBY 1          /**< 待机状态 */
#define DASHBOARD_CHARGE_MODE_CONSTANT_CURRENT 2 /**< 恒流阶段 */
#define DASHBOARD_CHARGE_MODE_CONSTANT_VOLTAGE 3 /**< 恒压阶段 */
#define DASHBOARD_CHARGE_MODE_PRECHARGE 4        /**< 预充阶段 */
#define DASHBOARD_CHARGE_MODE_FLOAT 5            /**< 浮充阶段 */
#define DASHBOARD_CHARGE_MODE_PAUSED 6           /**< 暂停充电 */
#define DASHBOARD_CHARGE_MODE_FAULT 7            /**< 充电故障 */
#define DASHBOARD_CHARGE_MODE_BALANCE_FAULT 8    /**< 均衡异常 */
#define DASHBOARD_CHARGE_MODE_INPUT_FAULT 9      /**< 输入异常 */
#define DASHBOARD_CHARGE_MODE_OUTPUT_FAULT 10    /**< 输出异常 */
#define DASHBOARD_CHARGE_MODE_PROTECTION_MODE 11 /**< 保护模式 */

#define DASHBOARD_LOG_CODE_SYSTEM_READY 1      /**< 系统就绪 */
#define DASHBOARD_LOG_CODE_CHARGE_STARTED 2    /**< 开始充电 */
#define DASHBOARD_LOG_CODE_CHARGE_STOPPED 3    /**< 停止充电 */
#define DASHBOARD_LOG_CODE_WELD_TRIGGERED 4    /**< 点焊已触发 */
#define DASHBOARD_LOG_CODE_WELD_COMPLETE 5     /**< 点焊完成 */
#define DASHBOARD_LOG_CODE_TEMPERATURE_WARN 6  /**< 温度警告 */
#define DASHBOARD_LOG_CODE_TEMPERATURE_ERROR 7 /**< 温度异常 */
#define DASHBOARD_LOG_CODE_BLE_CONNECTED 8     /**< BLE 已连接 */
#define DASHBOARD_LOG_CODE_BLE_DISCONNECTED 9  /**< BLE 已断开 */
#define DASHBOARD_LOG_CODE_CONFIG_UPDATED 10   /**< 参数已更新 */

typedef struct {
  uint16_t id;              /**< 点焊记录递增 ID */
  uint16_t peak_current_a;  /**< 点焊峰值电流，单位 A */
  uint16_t post_voltage_mv; /**< 点焊后的最新电压，单位 mV */
} dashboard_weld_record_t;

typedef struct {
  uint16_t id;       /**< 日志递增 ID */
  uint8_t level;     /**< 日志等级码 */
  uint32_t time_sec; /**< 日志时间，建议设备启动后的秒数或 Unix 秒 */
  uint16_t code;     /**< 日志码，App 侧映射为本地文案 */
} dashboard_log_t;

/** @brief 打包设备运行状态和充电模式到 status_flags。 */
uint8_t dashboard_pack_status_flags(uint8_t machine_status,
                                    uint8_t charge_mode_code);
/** @brief 从 status_flags 读取设备运行状态。 */
uint8_t dashboard_get_machine_status(uint8_t status_flags);
/** @brief 从 status_flags 读取充电模式码。 */
uint8_t dashboard_get_charge_mode_code(uint8_t status_flags);
/** @brief 打包放电状态和预留状态到 flags。 */
uint8_t dashboard_pack_flags(uint8_t discharge_status,
                             uint8_t undefined_status);
/** @brief 从 flags 读取放电状态。 */
uint8_t dashboard_get_discharge_status(uint8_t flags);
/** @brief 从 flags 读取预留状态。 */
uint8_t dashboard_get_undefined_status(uint8_t flags);

/**
 * @brief 打包 Dashboard 初始化响应 data。
 *
 * 注意：新增字段只能尾部追加，已发布字段 offset/长度/单位不可变。
 */
bool dashboard_init_pack(const dashboard_init_t *fields, uint8_t *buf,
                         uint16_t buf_size);
/**
 * @brief 解包 Dashboard 初始化响应 data。
 *
 * 注意：按当前 SDK 已知长度读取前缀，不根据协议版本切换解析结构。
 */
bool dashboard_init_unpack(const uint8_t *data, uint16_t len,
                           dashboard_init_t *out_fields);
/** @brief 打包 Dashboard 初始化请求。 */
bool dashboard_init_request_pack(const dashboard_init_request_t *fields,
                                 uint8_t *buf, uint16_t buf_size);
/** @brief 解包 Dashboard 初始化请求。 */
bool dashboard_init_request_unpack(const uint8_t *data, uint16_t len,
                                   dashboard_init_request_t *out_fields);
/** @brief 打包 Dashboard 高频紧凑状态。 */
bool dashboard_compact_pack(const dashboard_compact_t *fields, uint8_t *buf,
                            uint16_t buf_size);
/** @brief 解包 Dashboard 高频紧凑状态。 */
bool dashboard_compact_unpack(const uint8_t *data, uint16_t len,
                              dashboard_compact_t *out_fields);
/**
 * @brief 将点焊记录数组打包为批量上报 payload。
 * @param out_len 实际写入长度。
 *
 * 注意：记录数量超过单包上限时会截断。
 */
bool dashboard_weld_records_from_array(const dashboard_weld_record_t *records,
                                       uint8_t size, uint8_t *buf,
                                       uint16_t buf_size, uint16_t *out_len);
/** @brief 预读批量点焊记录数量。 */
bool dashboard_weld_records_peek_size(const uint8_t *data, uint16_t len,
                                      uint8_t *out_size);
/** @brief 解包指定下标的点焊记录。 */
bool dashboard_weld_records_unpack_item(const uint8_t *data, uint16_t len,
                                        uint8_t index,
                                        dashboard_weld_record_t *out_record);
/**
 * @brief 将诊断日志数组打包为批量上报 payload。
 * @param out_len 实际写入长度。
 *
 * 注意：日志数量超过单包上限时会截断。
 */
bool dashboard_logs_from_array(const dashboard_log_t *logs, uint8_t size,
                               uint8_t *buf, uint16_t buf_size,
                               uint16_t *out_len);
/** @brief 预读批量诊断日志数量。 */
bool dashboard_logs_peek_size(const uint8_t *data, uint16_t len,
                              uint8_t *out_size);
/** @brief 解包指定下标的诊断日志。 */
bool dashboard_logs_unpack_item(const uint8_t *data, uint16_t len,
                                uint8_t index, dashboard_log_t *out_log);

#ifdef __cplusplus
}
#endif

#endif /* SDK_PAYLOADS_DASHBOARD_H */
