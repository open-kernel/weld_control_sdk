"""OTA payload 编解码。"""

import struct

# OTA 业务码，放在 sdk_result_t.code 中
OTA_CODE_NONE           = 0x00    # 无错误
OTA_CODE_SIZE           = 0x64    # 固件大小超限
OTA_CODE_SEQ            = 0x65    # 分片序号错误
OTA_CODE_CRC            = 0x66    # CRC 校验失败
OTA_CODE_BUSY           = 0x67    # 设备忙
OTA_CODE_COMPANY        = 0x68    # 厂商ID不匹配（防止上传错固件）
OTA_CODE_TYPE           = 0x69    # 固件类型不匹配（BLE/UART 混淆）

# OTA 最大固件大小（字节），默认 256 KB
SDK_OTA_MAX_FW_SIZE = 256 * 1024
# OTA 单包数据最大长度
SDK_OTA_CHUNK_MAX = 240
OTA_START_PAYLOAD_SIZE = 14     # OTA 开始请求 payload 长度
OTA_DATA_HDR_PAYLOAD_SIZE = 4   # OTA 数据片头 payload 长度，不含固件数据
OTA_VERIFY_PAYLOAD_SIZE = 6     # OTA 校验请求 payload 长度
OTA_ACK_DATA_PAYLOAD_SIZE = 2   # OTA ACK data payload 长度，不含 sdk_result_t 头

class ota_start_t:
    """OTA 开始载荷（上位机→设备）。"""

    FORMAT = "<HB3sIHH"
    @staticmethod
    def pack(fields):
        """打包 OTA 开始请求。

        注意：fw_size 不含固件包头，只表示 payload 固件体大小。
        """
        return struct.pack(
            ota_start_t.FORMAT,
            fields['company_id'],
            fields['fw_type'],
            fields['fw_ver'][:3],
            fields['fw_size'],
            fields['fw_crc16'],
            fields['chunk_size'],
        )

    @staticmethod
    def unpack(data):
        """解包 OTA 开始请求；长度不足时返回 None。"""
        if len(data) < OTA_START_PAYLOAD_SIZE: return None
        res = struct.unpack(ota_start_t.FORMAT, data[:OTA_START_PAYLOAD_SIZE])
        return {'company_id': res[0], 'fw_type': res[1], 'fw_ver': res[2],
                'fw_size': res[3], 'fw_crc16': res[4], 'chunk_size': res[5]}

class ota_data_hdr_t:
    """OTA 数据包头（上位机→设备，后紧跟 chunk_size 字节固件数据）。"""

    FORMAT = "<HH"
    @staticmethod
    def pack(fields):
        """打包 OTA 数据片头。"""
        return struct.pack(ota_data_hdr_t.FORMAT, fields['chunk_index'], fields['data_len'])

    @staticmethod
    def unpack(data):
        """解包 OTA 数据片头；长度不足时返回 None。"""
        if len(data) < OTA_DATA_HDR_PAYLOAD_SIZE: return None
        res = struct.unpack(ota_data_hdr_t.FORMAT, data[:OTA_DATA_HDR_PAYLOAD_SIZE])
        return {'chunk_index': res[0], 'data_len': res[1]}

class ota_verify_t:
    """OTA 校验载荷（上位机→设备）。"""

    FORMAT = "<IH"
    @staticmethod
    def pack(fields):
        """打包 OTA 校验请求。"""
        return struct.pack(ota_verify_t.FORMAT, fields['fw_size'], fields['fw_crc16'])

    @staticmethod
    def unpack(data):
        """解包 OTA 校验请求；长度不足时返回 None。"""
        if len(data) < OTA_VERIFY_PAYLOAD_SIZE: return None
        res = struct.unpack(ota_verify_t.FORMAT, data[:OTA_VERIFY_PAYLOAD_SIZE])
        return {'fw_size': res[0], 'fw_crc16': res[1]}

class ota_ack_data_t:
    """OTA 应答 data 载荷，外层 status/code 由 sdk_result_t 承载。"""

    FORMAT = "<H"
    @staticmethod
    def unpack(data):
        """解包 OTA 应答 data；长度不足时返回 None。"""
        if len(data) < OTA_ACK_DATA_PAYLOAD_SIZE: return None
        res = struct.unpack(ota_ack_data_t.FORMAT, data[:OTA_ACK_DATA_PAYLOAD_SIZE])
        return {'next_chunk': res[0]}

    @staticmethod
    def pack(next_chunk):
        """打包 OTA 应答 data。"""
        return struct.pack(ota_ack_data_t.FORMAT, next_chunk)
