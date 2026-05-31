#ifndef RP2040_RAM_LOADER_H
#define RP2040_RAM_LOADER_H

#include <stdint.h>

int rp2040_loader_start(const uint8_t *firmware, uint32_t len, uint32_t entry);
int rp2040_loader_reload(const uint8_t *firmware, uint32_t len, uint32_t entry);

#endif
