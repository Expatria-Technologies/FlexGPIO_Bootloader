#include <pico/stdlib.h>
#include <stdio.h>

#include "rp2040_ram_loader.h"

extern const uint8_t firmware_bin[];
extern const uint32_t firmware_bin_len;

int main(void)
{
    stdio_init_all();
    printf("FlexGPIO Host Loader\n");

    int ret = rp2040_loader_start(firmware_bin, firmware_bin_len, 0x20002000);
    if (ret != 0) {
        printf("FAILED\n");
        for (;;);
    }

    printf("SUCCESS\n");
    for (;;);
}
