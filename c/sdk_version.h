#ifndef SDK_VERSION_H
#define SDK_VERSION_H

/**
 * @file sdk_version.h
 * @brief SDK 协议版本常量。
 *
 * 说明：版本号用于兼容提示/阻断；payload 解包按已知长度读取前缀。
 */

/** 协议版本：v1 */
#define SDK_PROTOCOL_VERSION 0x1
/** 当前 App/设备可解析的最低协议版本 */
#define SDK_MIN_PROTOCOL_VERSION 0x1

#endif /* SDK_VERSION_H */
