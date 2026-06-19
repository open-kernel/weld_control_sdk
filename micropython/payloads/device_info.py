"""设备信息 payload 编解码。"""

import struct

DEVICE_INFO_FIXED_SIZE_V1 = 29 # 设备信息 V1 固定头 payload 长度，不含变长字符串
DEVICE_INFO_FIXED_SIZE = DEVICE_INFO_FIXED_SIZE_V1 # 当前设备信息固定头 payload 长度


def _encode_var_string(value):
    """编码变长 UTF-8 字符串，单段最长 255 字节。"""
    return (value or '').encode('utf-8')[:0xFF]


def _decode_var_string(data):
    """解码变长 UTF-8 字符串。"""
    return data.decode('utf-8', 'replace')


class device_info_t:
    """设备信息 payload：固定头 + 变长字符串。"""

    FIXED_SIZE = DEVICE_INFO_FIXED_SIZE
    FORMAT = '<HHIHHHHBBBIBBBBBB'

    @staticmethod
    def pack(fields):
        """打包设备信息 payload。

        注意：manufacturer/model/serial 均按 255 字节截断；serial 不应为空。
        """
        manufacturer = _encode_var_string(fields.get('manufacturer', ''))
        model = _encode_var_string(fields.get('model', ''))
        serial = _encode_var_string(fields.get('serial', ''))
        values = ()
        if DEVICE_INFO_FIXED_SIZE >= DEVICE_INFO_FIXED_SIZE_V1:
            values = (
                fields['company_id'],
                fields['product_id'],
                fields['feature_mask'],
                fields['protocol_version'],
                fields['protocol_min_version'],
                fields['ota_max_kb'],
                fields['ota_chunk_max'],
                fields['firmware_major'],
                fields['firmware_minor'],
                fields['firmware_patch'],
                fields['firmware_build_id'],
                fields['hardware_major'],
                fields['hardware_minor'],
                fields['hardware_patch'],
                len(manufacturer),
                len(model),
                len(serial),
            )
        fixed = struct.pack(
            device_info_t.FORMAT,
            *values,
        )
        return fixed + manufacturer + model + serial

    @staticmethod
    def unpack(data):
        """解包设备信息 payload。

        注意：按当前 SDK 已知长度读取前缀，不根据协议版本切换解析结构。
        """
        if len(data) < DEVICE_INFO_FIXED_SIZE_V1:
            return None
        values = struct.unpack(device_info_t.FORMAT, data[:DEVICE_INFO_FIXED_SIZE_V1])
        manufacturer_len = values[14]
        model_len = values[15]
        serial_len = values[16]
        total_len = DEVICE_INFO_FIXED_SIZE_V1 + manufacturer_len + model_len + serial_len
        if len(data) < total_len:
            return None
        offset = DEVICE_INFO_FIXED_SIZE_V1
        manufacturer = _decode_var_string(data[offset: offset + manufacturer_len])
        offset += manufacturer_len
        model = _decode_var_string(data[offset: offset + model_len])
        offset += model_len
        serial = _decode_var_string(data[offset: offset + serial_len])
        result = {
            'company_id': values[0],
            'product_id': values[1],
            'feature_mask': values[2],
            'protocol_version': values[3],
            'protocol_min_version': values[4],
            'ota_max_kb': values[5],
            'ota_chunk_max': values[6],
            'firmware_major': values[7],
            'firmware_minor': values[8],
            'firmware_patch': values[9],
            'firmware_build_id': values[10],
            'hardware_major': values[11],
            'hardware_minor': values[12],
            'hardware_patch': values[13],
            'manufacturer': manufacturer,
            'model': model,
            'serial': serial,
        }
        return result
