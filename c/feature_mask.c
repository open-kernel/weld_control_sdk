#include "feature_mask.h"

static uint32_t g_feature_mask = 0;

void feature_mask_set(uint32_t mask) {
  g_feature_mask = mask;
}

void feature_mask_add(uint32_t features) {
  g_feature_mask |= features;
}

uint32_t feature_mask_get(void) {
  return g_feature_mask;
}

bool feature_mask_has(uint32_t mask, uint32_t feature) {
  return (mask & feature) != 0;
}

void feature_mask_to_hex(uint32_t mask, char *buf) {
  static const char hex_chars[] = "0123456789ABCDEF";

  for (int i = 0; i < 8; i++) {
    uint8_t nibble = (mask >> (28 - i * 4)) & 0x0F;
    buf[i] = hex_chars[nibble];
  }
  buf[8] = '\0';
}

uint32_t feature_mask_from_hex(const char *hex) {
  uint32_t mask = 0;

  for (int i = 0; i < 8; i++) {
    char c = hex[i];
    uint8_t nibble;

    if (c >= '0' && c <= '9') {
      nibble = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      nibble = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
      nibble = c - 'a' + 10;
    } else {
      nibble = 0;
    }

    mask = (mask << 4) | nibble;
  }

  return mask;
}