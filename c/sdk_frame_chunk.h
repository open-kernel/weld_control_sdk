/**
 * @file sdk_frame_chunk.h
 * @brief SDK frame 传输层切块辅助工具
 *
 * 说明：
 *   本文件只处理完整 SDK frame bytes 的链路层 chunk 切分。
 *   它不处理 BLE 写入、队列、重试、超时，也不引入 SDK 应用层分片。
 */

#ifndef SDK_FRAME_CHUNK_H
#define SDK_FRAME_CHUNK_H

#include <stdbool.h>
#include <stdint.h>

#include "sdk_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 单个传输 chunk 视图。 */
typedef struct {
  const uint8_t *data; /**< 当前 chunk 指针，指向原 frame 内存 */
  uint16_t len;        /**< 当前 chunk 长度 */
  uint16_t offset;     /**< 当前 chunk 在 frame 中的偏移 */
  uint16_t total_len;  /**< 完整 frame 总长度 */
  uint16_t index;      /**< 当前 chunk 序号，从 0 开始 */
  uint16_t count;      /**< 总 chunk 数 */
} sdk_frame_chunk_t;

/** frame chunk 迭代器。 */
typedef struct {
  const uint8_t *frame; /**< 完整 SDK frame 指针 */
  uint16_t frame_len;   /**< 完整 SDK frame 长度 */
  uint16_t chunk_size;  /**< 单个 chunk 最大长度 */
  uint16_t offset;      /**< 下一次读取偏移 */
  uint16_t index;       /**< 下一次返回序号 */
  uint16_t count;       /**< 总 chunk 数 */
} sdk_frame_chunk_iter_t;

/** packet 编码后 chunk 迭代器。 */
typedef struct {
  sdk_frame_chunk_iter_t frame_iter; /**< 已编码 frame 的 chunk 迭代器 */
  uint16_t frame_len;                /**< sdk_encode() 得到的 frame 长度 */
  bool ok;                           /**< 初始化是否成功 */
} sdk_frame_chunk_with_encode_iter_t;

/**
 * @brief 迭代完整 SDK frame 的传输层 chunk。
 * @param frame 完整 SDK frame 指针。
 * @param frame_len 完整 SDK frame 长度。
 * @param chunk_size 单个 chunk 最大长度。
 * @return chunk 迭代器。
 *
 * 注意：返回的 chunk.data 指向原 frame 内存，调用方必须保证 frame 生命周期。
 */
static inline sdk_frame_chunk_iter_t sdk_frame_chunk_iter(
    const uint8_t *frame, uint16_t frame_len, uint16_t chunk_size) {
  sdk_frame_chunk_iter_t iter = {
      .frame = frame,
      .frame_len = frame_len,
      .chunk_size = chunk_size,
      .offset = 0,
      .index = 0,
      .count = 0,
  };
  if (frame && frame_len > 0 && chunk_size > 0) {
    iter.count = (uint16_t)((frame_len + chunk_size - 1u) / chunk_size);
  }
  return iter;
}

/**
 * @brief 读取下一个 frame chunk。
 * @param iter 迭代器。
 * @param out_chunk 输出 chunk 视图。
 * @return 有 chunk 返回 true；结束或参数非法返回 false。
 */
static inline bool sdk_frame_chunk_next(sdk_frame_chunk_iter_t *iter,
                                        sdk_frame_chunk_t *out_chunk) {
  if (!iter || !out_chunk) return false;
  if (!iter->frame || iter->chunk_size == 0) return false;
  if (iter->offset >= iter->frame_len) return false;

  uint16_t len = (uint16_t)(iter->frame_len - iter->offset);
  if (len > iter->chunk_size) len = iter->chunk_size;

  out_chunk->data = iter->frame + iter->offset;
  out_chunk->len = len;
  out_chunk->offset = iter->offset;
  out_chunk->total_len = iter->frame_len;
  out_chunk->index = iter->index;
  out_chunk->count = iter->count;

  iter->offset = (uint16_t)(iter->offset + len);
  iter->index = (uint16_t)(iter->index + 1u);
  return true;
}

/**
 * @brief 将 packet 编码为 SDK frame 后迭代传输层 chunk。
 * @param dev 设备上下文，可为 NULL。
 * @param pkt 待编码 packet。
 * @param frame_buf 调用方提供的 frame 缓冲区。
 * @param frame_buf_size frame 缓冲区长度。
 * @param chunk_size 单个 chunk 最大长度。
 * @return packet 编码后 chunk 迭代器。
 */
static inline sdk_frame_chunk_with_encode_iter_t
sdk_frame_chunk_with_encode_iter(sdk_device_t *dev, const sdk_packet_t *pkt,
                                 uint8_t *frame_buf,
                                 uint16_t frame_buf_size,
                                 uint16_t chunk_size) {
  sdk_frame_chunk_with_encode_iter_t iter = {0};
  if (!pkt || !frame_buf || frame_buf_size == 0 || chunk_size == 0) return iter;

  const uint16_t frame_len = sdk_encode(dev, pkt, frame_buf, frame_buf_size);
  if (frame_len == 0) return iter;

  iter.frame_len = frame_len;
  iter.frame_iter = sdk_frame_chunk_iter(frame_buf, frame_len, chunk_size);
  iter.ok = true;
  return iter;
}

/**
 * @brief 读取下一个已编码 packet chunk。
 * @param iter 迭代器。
 * @param out_chunk 输出 chunk 视图。
 * @return 有 chunk 返回 true；结束或初始化失败返回 false。
 */
static inline bool sdk_frame_chunk_with_encode_next(
    sdk_frame_chunk_with_encode_iter_t *iter, sdk_frame_chunk_t *out_chunk) {
  if (!iter || !iter->ok) return false;
  return sdk_frame_chunk_next(&iter->frame_iter, out_chunk);
}

#ifdef __cplusplus
}
#endif

#endif /* SDK_FRAME_CHUNK_H */
