# SDK 协议兼容策略

> 状态：正式策略
> 适用范围：WeldControl BLE SDK、设备固件、小程序、协议文档和后续 SDK 发布说明
> 当前协议：`SDK_PROTOCOL_VERSION = 0x1`

---

## 1. 目标

本文定义协议升级、版本交换、兼容判断、阻断提示和发布文档的统一规则。

协议规格文档只描述 wire 格式；本文描述什么时候可以改、怎么改、App 和设备应该如何判断是否兼容。

核心目标：

1. 避免旧 App 误控新设备。
2. 让新 App 尽量能读取旧设备的基础数据。
3. 把协议兼容、设备功能能力、固件版本三件事分开。
4. 让设备开发者升级 SDK 时能明确知道要改哪些 CMD 和 payload。

---

## 2. 总原则

### 2.1 发布状态

当前协议按 v1.0 正式发布状态维护。后续变更默认遵守：

1. 已发布 CMD 不删除、不改命令号。
2. 已发布字段不改偏移、不改长度、不改单位、不改语义。
3. 新字段只能尾部追加。
4. 旧字段可以弃用，但不能删除、复用或改变含义。
5. `SDK_TOKEN_LEN` 禁止变化。
6. Payload 总长度变化只能来自尾部追加字段。

如果做不到以上规则，必须新增 CMD、新增 payload 实体，或提高设备 `protocol_min_version`。

---

## 3. 版本模型

### 3.1 字段定义

设备必须在 `device_info_t` 和 `dashboard_init_t` 中返回：

| 字段 | 方向 | 含义 |
| --- | --- | --- |
| `protocol_version` | Device -> App | 设备当前使用的协议版本 |
| `protocol_min_version` | Device -> App | 设备要求的最低 App 协议版本 |

App 在 `dashboard_init_request_t` 中发送：

| 字段 | 方向 | 含义 |
| --- | --- | --- |
| `app_protocol_version` | App -> Device | App 当前使用的协议版本 |
| `app_min_protocol_version` | App -> Device | App 可解析的最低设备协议版本 |
| `app_version_major/minor/patch` | App -> Device | App 软件版本 |
| `app_build_id` | App -> Device | App 构建号 |

### 3.2 兼容范围

App 当前兼容范围由 SDK 常量定义：

```text
SDK_MIN_PROTOCOL_VERSION <= device.protocol_version <= SDK_PROTOCOL_VERSION
```

设备是否要求更高版本 App，由 `protocol_min_version` 决定：

```text
device.protocol_min_version > SDK_PROTOCOL_VERSION
```

出现上述情况时，当前 App 过旧，必须阻断连接或添加流程。

### 3.3 扫描列表行为

扫描列表读取 `device_info_t` 后判断：

```text
if device.protocol_min_version > SDK_PROTOCOL_VERSION:
  显示 tag：版本太新
  点击时阻断
```

弹窗文案：

```text
当前软件版本过旧不兼容新设备请升级后再连接
最低支持协议版本xxx 当前兼容协议版本xxx-xxx
```

该判断不依赖 `feature_mask`，也不依赖固件版本号。

---

## 4. 旧 App 与新设备

新设备不强制承诺兼容所有旧 App。设备可以通过提高 `protocol_min_version` 明确声明“低于该版本的 App 不允许继续使用”。

但如果一次升级只是兼容新增，应优先不提高 `protocol_min_version`，并遵守尾部追加规则，让旧 App 继续读取旧字段前缀。

推荐判断：

| 变更类型 | 是否提高 `protocol_min_version` | 说明 |
| --- | --- | --- |
| 尾部追加可选字段 | 否 | 旧 App 可忽略尾部 |
| 新增 CMD，旧流程不依赖它 | 否 | 旧 App 继续基础功能 |
| 新增功能能力位 | 否 | 旧 App 不显示新功能 |
| 改变控制语义 | 是 | 防止旧 App 误控设备 |
| 改变字段单位/长度/偏移 | 是，但优先新增实体/CMD | 已发布字段本身仍禁止直接改 |
| 修复安全漏洞导致旧协议不可用 | 是 | 允许阻断旧 App |

---

## 5. 新 App 与旧设备

新 App 应尽量支持旧设备，策略是：

1. payload 解包只按当前 SDK 已知结构长度做前缀解析。
2. payload 长度只用于安全校验：够当前已知长度就解析当前字段，额外尾部忽略。
3. `protocol_version` / `protocol_min_version` 只在 `device_info_t` 和 `dashboard_init_t` 解析成功后用于提示、阻断、刷新设备信息和兼容策略判断。
4. 缺少新字段时，UI 隐藏新功能或显示“不支持”。
5. 只有核心字段缺失、payload 无法解析、或存在安全风险时才阻断。

不允许因为设备版本旧就默认阻断。版本旧只应触发提示、降级或功能隐藏。

---

## 6. Payload 演进规则

### 6.1 固定前缀 + 可选尾部

发布后的实体必须显式定义当前 SDK 已知长度。解包时不根据 `protocol_version` 切换解析路径，也不依赖连接级版本上下文；只按当前 SDK 已知长度读取字段。后续新增字段只能尾部追加，旧 SDK 读取前缀并忽略尾部。

```ts
const ENTITY_BYTE_LENGTH = 27

if (data.length < ENTITY_BYTE_LENGTH) return null

// 只读取当前 SDK 已知字段，data 末尾如果有新版追加字段则忽略。
return unpack_current_prefix(data)
```

禁止用 payload 长度推导协议版本。长度只能证明“够不够读当前 SDK 已知字段”，不能证明“这是哪个协议版本”。

禁止发布后直接把旧实体 `BYTE_LENGTH` 改大并要求所有旧设备立即返回新长度。

### 6.2 变长结构

变长结构继续使用：

```text
fixed_header + len + data
```

新增变长字段时，追加新的 `len + data` 尾部，不改已有 length 字段含义。

### 6.3 数组结构

数组结构继续使用：

```text
size + item[]
```

如果 item 字段需要大改，优先新增数组 CMD 或新增 item version，不在高频包里堆复杂兼容逻辑。

### 6.4 控制类命令

控制类命令以安全为优先。新增控制动作优先新增 CMD，不复用旧 CMD 改语义，如果是更新旧版本命令payload字段枚举或者状态 必须强调说明并做安全兼容处理 避免出现意料之外的情况。

---

## 7. `feature_mask` 边界

`feature_mask` 只回答“设备支持哪些功能”，不回答“协议是否兼容”。

示例：

```text
feature_mask 有安全放电功能：显示安全放电入口
protocol_version 支持安全放电 CMD：允许发送对应协议请求
protocol_min_version 大于当前 App 协议版本：提示 App 过旧并阻断连接
```

三者不能互相替代。

---

## 8. Dashboard 初始化刷新策略

Dashboard 进入时发送 `CMD_READ_DASHBOARD_INIT`，payload 为 `dashboard_init_request_t`。

设备返回 `dashboard_init_t`，其中包含 Dashboard 基础字段和版本指纹：

```text
protocol_version
firmware_major
firmware_minor
firmware_patch
firmware_build_id
protocol_min_version
setting_single_cap_voltage_mv
setting_trigger_mode
setting_auto_delay_ms
```

App 处理规则：

1. 先用 init 中的基础字段渲染 Dashboard。
2. 对比本地设备记录中的协议/固件指纹。
3. 如果本地缺少设备信息，或协议/固件指纹变化，后台读取 `CMD_DEVICE_GET_INFO`。
4. `device_info_t` 读取成功后更新本地设备表。
5. 如果读取失败，不应清空 Dashboard；但固件更新、设备信息等依赖完整信息的入口需要提示设备信息未刷新。

---

## 9. `sdk_compat` 模块

兼容判断必须放进独立模块，不放进 codec。

发布 SDK 文件：

```text
sdk_compat.h
sdk_compat.py
```

codec 只做字节编解码；compat 模块负责版本判断和风险等级。App 侧可以在内部 SDK 镜像中提供 UI 提示文案构造函数，但该函数不属于第三方 SDK 发布 API。

等级使用数字枚举：

| level | 行为 |
| --- | --- |
| `0` / `SDK_COMPAT_LEVEL_OK` | 正常使用 |
| `1` / `SDK_COMPAT_LEVEL_WARN` | 可使用，提示升级或已知风险 |
| `2` / `SDK_COMPAT_LEVEL_LIMITED` | 可使用基础功能，隐藏或禁用部分新功能 |
| `3` / `SDK_COMPAT_LEVEL_BLOCK` | 阻断使用，必须给出原因和下一步 |

推荐阻断条件：

1. `device.protocol_min_version > SDK_PROTOCOL_VERSION`。
2. 设备协议低于 App 可解析基础字段。
3. 已知安全漏洞。
4. 已知会导致错误控制输出。
5. 核心 payload 无法解码，页面会留下脏数据。

---

## 10. 发布文档要求

每次用户明确说明“已发布新版本 SDK，需要编写文档”或类似要求时，必须先对比旧版本和当前版本，再编写面向设备开发者的升级文档。

升级文档必须说明：

1. 协议版本变化。
2. CMD 新增、删除、改名或编号变化。
3. Payload 字段新增、删除、改名、偏移、长度、单位变化。
4. `sdk_result_t.status/code/data` 语义变化。
5. 发布 SDK 与 App / 上位机实现的 API 名称变化。
6. 设备端需要修改的发送/接收流程。
7. App 侧兼容、降级或阻断策略。
8. 旧固件升级到新协议的风险点。
9. 推荐验证用例。

发布文档必须显式确认：

1. 已发布 CMD 编号是否保持不变。
2. 已发布字段单位是否保持不变。
3. 已发布字段长度是否保持不变。
4. 已发布字段偏移和顺序是否保持不变。
5. `SDK_TOKEN_LEN` 是否保持不变。
6. Payload 总长度变化是否只来自尾部追加字段。

如果没有旧版本快照，应在文档开头说明差异依据，例如 Git diff、用户提供的旧版文件、协议文档旧副本或当前工作树对比。

---

## 11. 发布前检查清单

每次协议变更前检查：

1. 是否触碰已发布字段的偏移、长度、单位、语义。
2. 是否可以改成尾部追加字段。
3. 是否应该新增 CMD 或新实体。
4. 是否需要提高 `protocol_min_version`。
5. 新 App 是否能读取旧 payload。
6. 旧 App 是否能忽略新 payload 尾部。
7. 是否需要新增 `sdk_compat` 规则。
8. 是否需要更新设备端 `NOT_SUPPORTED` 或业务错误码。
9. 是否同步更新 TS / C / MicroPython。
10. 是否同步更新小程序 SDK 镜像。
11. 是否更新 `protocol_spec_v1.0.md`。
12. 是否需要新增设备端示例或升级文档。

---

## 12. 当前 v1.0 落地结论

当前 v1.0 采用以下策略：

1. `device_info_t` 包含 `protocol_version` 和 `protocol_min_version`。
2. `dashboard_init_t` 包含 `protocol_version`、固件版本指纹和 `protocol_min_version`。
3. App 扫描阶段根据 `protocol_min_version` 阻断过旧 App。
4. App 进入 Dashboard 时通过 `dashboard_init_request_t` 向设备上报自身协议和版本。
5. App 后续通过 init 指纹决定是否刷新完整 `device_info_t`。
6. `feature_mask` 只用于功能能力，不参与协议兼容判断。
