# WCFW 固件打包工具

`scripts/add_wcfw_header.py` 用于给固件 payload 添加 64 字节 WCFW 头部，生成可被上位机识别的 OTA 文件。

推荐用途是给已有固件 payload 添加 WCFW 头部，使上位机可以读取版本、厂商、构建号、payload 大小和 CRC 信息。工具也提供 `--mock` 模式，用于没有真实固件时生成测试 payload。

## 文件结构

```text
WCFW header，64 bytes
payload，真实固件内容或测试 payload
```

上位机读取 WCFW 头部用于展示和校验，真正发送给设备的是 header 后面的 payload 数据。

## 给已有固件添加 WCFW 头

```sh
python3 scripts/add_wcfw_header.py \
  --input firmware.bin \
  --company-id 0xAAAA \
  --type ble \
  --version 1.2.3 \
  --build-id 0x20260616
```

指定输出路径：

```sh
python3 scripts/add_wcfw_header.py \
  --input firmware.bin \
  --company-id 0xAAAA \
  --type ble \
  --version 1.2.3 \
  --build-id 0x20260616 \
  --output /tmp/weldcontrol_ble_v1.2.3.wcfw
```

## 生成测试 payload

没有真实固件时，可以用 `--mock` 生成确定性测试 payload：

```sh
python3 scripts/add_wcfw_header.py \
  --mock \
  --company-id 0xAAAA \
  --type ble \
  --version 1.2.3 \
  --build-id 0x20260616 \
  --payload-size 65536
```

相同参数和 `--seed` 会生成相同 payload，便于复现 CRC 和 OTA 分片问题。

## 输出位置

未指定 `--output` 时，文件输出到 `generated/`，并生成相邻 JSON manifest。`generated/` 是本地测试产物，已被 `.gitignore` 排除。

只生成固件文件，不生成 manifest：

```sh
python3 scripts/add_wcfw_header.py --input firmware.bin --version 1.2.3 --no-manifest
```

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input` | 输入固件 payload 文件，工具会在前面添加 WCFW 头 |
| `--mock` | 生成测试 payload，不读取输入固件 |
| `--company-id` | 厂商 ID，支持十进制或十六进制，例如 `0xAAAA`；正式产品应使用已登记且不冲突的 ID |
| `--type` | 固件类型，当前支持 `ble`、`uart` |
| `--version` | 固件版本，格式 `major.minor.patch` |
| `--build-id` | 构建 ID，支持十进制或十六进制 |
| `--payload-size` | mock payload 字节数，不包含 64 字节 WCFW 头；只在 `--mock` 模式使用 |
| `--seed` | mock payload 生成种子，相同参数可生成确定性内容 |
| `--output` | 输出文件路径 |
| `--no-manifest` | 不生成相邻 JSON manifest |

`--input` 和 `--mock` 二选一，不能同时使用。

## Metadata

脚本会打印 metadata，例如：

```json
{
  "magic": "WCFW",
  "mode": "wrapped",
  "company_id": "0xAAAA",
  "firmware_type": "ble",
  "version": "1.2.3",
  "build_id": 539362838,
  "payload_size": 65536,
  "payload_crc16": "0x1234"
}
```

其中 `version`、`build_id`、`company_id`、`payload_size`、`payload_crc16` 可用于核对上位机固件更新对话框展示是否正确。

## 厂商 ID

`company_id` 必须与设备信息包中的 `company_id` 一致，否则 OTA 校验会返回厂商不匹配。SDK 示例默认使用 `0xAAAA`，该值仅用于联调示例，不建议正式产品沿用。

正式接入前，请到本仓库 Issue #1 查看和登记厂商 ID 使用情况，避免和其他项目重复。
