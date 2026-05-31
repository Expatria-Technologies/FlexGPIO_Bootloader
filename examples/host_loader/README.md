# FlexGPIO Bootloader Host Example

Self-contained I2C host loader that loads firmware to an RP2040 running the
[FlexGPIO_Bootloader](https://github.com/Expatria-Technologies/FlexGPIO_Bootloader)
(picoboot3 fork).

Use with the FlexiHAL 2350. 

## Prerequisites

- [pico-sdk](https://github.com/raspberrypi/pico-sdk) — set `PICO_SDK_PATH`
- CMake ≥ 3.13
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Python 3 (for the firmware embedding pre-build script)

## Build

```bash
# 1. Copy your target firmware (e.g. FlexGPIO_ram.bin) as firmware.bin
cp /path/to/your_firmware.bin firmware.bin

# 2. Build
rm -rf build && mkdir build && cd build
cmake -DPICO_BOARD=flexi_2350 -DPICO_SDK_PATH=$PICO_SDK_PATH ..
make -j$(nproc)

# 3. Flash flexgpio_bootloader_host_example.uf2 to your host MCU
```

The example will print status on the UART on GP4/GP5 as it loads the firmware to RAM.


## Notes

```c
// Loads firmware to RP2040 RAM and executes it. Tries ACTIVATE first,
// falls back to magic packet + reload if target is already running.
int rp2040_loader_start(const uint8_t *firmware, uint32_t len, uint32_t entry);

// Skips the initial ACTIVATE attempt — sends magic packet immediately,
// then reloads. Use for runtime firmware updates.
int rp2040_loader_reload(const uint8_t *firmware, uint32_t len, uint32_t entry);
```