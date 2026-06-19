# ============================================================
# 文件名: sdk_frame_chunk.py
# 简述: SDK frame 传输层切块辅助工具
#
# 说明：
#   本文件只处理完整 SDK frame bytes 的链路层 chunk 切分。
#   它不处理 BLE 写入、队列、重试、超时，也不引入 SDK 应用层分片。
# ============================================================

from sdk_codec import SDK_DEFAULT_TRANSPORT_MTU_PAYLOAD, sdk_encode


def _sdk_frame_chunk_normalize_size(chunk_size):
    try:
        size = int(chunk_size)
    except (TypeError, ValueError):
        return 0
    return size if size > 0 else 0


def sdk_frame_chunk_iter(frame, chunk_size=SDK_DEFAULT_TRANSPORT_MTU_PAYLOAD):
    """迭代完整 SDK frame 的传输层 chunk；参数非法时抛出 ValueError。"""
    size = _sdk_frame_chunk_normalize_size(chunk_size)
    if frame is None:
        raise ValueError("[sdk_frame_chunk] frame is None")
    if len(frame) == 0:
        raise ValueError("[sdk_frame_chunk] frame is empty")
    if size == 0:
        raise ValueError("[sdk_frame_chunk] invalid chunk_size")

    frame_len = len(frame)
    count = (frame_len + size - 1) // size
    index = 0
    offset = 0
    while offset < frame_len:
        end = min(offset + size, frame_len)
        yield {
            'data': frame[offset:end],
            'len': end - offset,
            'offset': offset,
            'total_len': frame_len,
            'index': index,
            'count': count,
        }
        offset = end
        index += 1


def sdk_frame_chunk_with_encode_iter(dev, pkt, chunk_size=SDK_DEFAULT_TRANSPORT_MTU_PAYLOAD):
    """将 packet 编码为 SDK frame 后迭代传输层 chunk；编码失败时抛出 ValueError。"""
    if pkt is None:
        raise ValueError("[sdk_frame_chunk] packet is None")
    frame = sdk_encode(dev, pkt)
    if not frame:
        raise ValueError("[sdk_frame_chunk] packet encode failed")
    for chunk in sdk_frame_chunk_iter(frame, chunk_size):
        yield chunk
