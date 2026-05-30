# FlexGPIO Bootloader

Custom bootloader based on [picoboot3](https://github.com/IndoorCorgi/picoboot3) that allows loading FlexGPIO firmware into RAM via I2C. 


Raspberry Pi Pico's built-in bootloader allows you to write firmware via USB or SWD. The USB UF2 bootloader is usable for the FlexGPIO, but it is convienient to have the RP2350 on the FlexiHAL 2350 load the FlexGPIO firmware at runtime, removing the need to flash the RP2040 when the firmware is updated. This way, the bootloader is the only thing that needs to be loaded to flash, and it should only need to be loaded once.




## How it Works

### Cold boot — both chips power up simultaneously

| Time | RP2350 (Main MCU) | RP2040 (FlexGPIO) |
|------|---------------------|--------|
| t=0 | Power on | Power on, bootrom starts |
| t=0.5ms | Firmware starts | Bootrom loading boot2 |
| t=1ms | Boot sequence begins | Boot2 → picoboot3 |
| t=2ms | Initializes I2C bus at 1 MHz (slower is fine too, will just change timing listed here) | picoboot3 enters bootloader (always) |
| t=3ms | Sends ACTIVATE (`0xA5`) to `0x48` | I2C slave at `0x48` receives byte |
| t=4ms | Reads 4-byte response: `"pbt3"` | Replies with activation response |
| t=5ms | `LOAD_RAM` chunks begin (42KB @ 4KB each) | `memcpy()` data to SRAM at `0x20002000` |
| t=~600ms | Sends EXEC_RAM (`0x46`) for entry `0x20002000` | Sets VTOR, loads MSP, `bx` → FlexGPIO |
| t=~601ms | Configures I2C1 at 1MHz, enables GP31 edge IRQ | `custom_startup.S` runs: set SP, zero BSS, `cpsie i` (re-enables interrupts), `main()` → `setup()` → `i2c_slave_init()` |
| t=~602ms | Sends `output_packet_t` with `mcu_irq_mask` + `probe_irq_mask` | I2C slave at `0x48` ACKs, IRQ monitoring begins |
| t=~603ms | Core 1 starts, normal operation | FlexGPIO running normally |
| — | Normal operation | Normal operation |


### RP2350-only reset (FlexGPIO still running from RAM on RP2040)

| Step | RP2350 (Main MCU) | RP2040 (FlexGPIO) |
|------|---------------------|--------|
| 1 | Sends ACTIVATE (`0xA5`) to `0x48` | FlexGPIO receives `0xA5` in `mem[0]` (packet data, not activation) |
| 2 | Reads response ≠ `"pbt3"` → FAIL | — |
| 3 | Sends `output_packet_t` with all fields set to `0xFF`/`0xFFFF` | FlexGPIO `i2c_task()` detects `outputpacket.value == 0xFFFFFFFF` |
| 4 | — | Sets watchdog scratch[0] = 1 (signals picoboot3 to enter bootloader on next boot) |
| 5 | — | Triggers hardware reset → **RP2040 RESETS** |
| 6 | — | Bootrom → boot2 → picoboot3 (~10ms watchdog + ~5ms boot) |
| 7 | Retries ACTIVATE (currently using 5 retries, 10ms apart. Watchdog reset should be ~10ms, boot to bootloader and ready to accept firmware ~5ms) | picoboot3 enters bootloader (always), I2C slave ready |
| 8 | Reads response: `"pbt3"` | Replies with activation response |
| 9 | `LOAD_RAM` chunks → Sends EXEC_RAM (`0x46`) for entry `0x20002000` | FlexGPIO loaded to RAM, same process as cold boot here. |


## Memory layout after boot

### RP2040 Flash (2MB)

| Address | Size | Contents |
|---------|------|----------|
| `0x10000000` – `0x10007FFF` | 32KB | picoboot3 bootloader |
| `0x10008000` – `0x100FFFFF` | 1984KB | Unused |

### RP2040 SRAM (264KB)

| Address | Size | Contents |
|---------|------|----------|
| `0x20000000` – `0x20001FFF` | 8KB | picoboot3 .bss (I2C buffer, ~6KB used) |
| `0x20002000` – `0x2003FFFF` | 248KB | FlexGPIO RAM image (~42KB loaded every boot) |
| `0x20040000` – `0x20040FFF` | 4KB | SCRATCH_X (unused) |
| `0x20041000` – `0x20041FFF` | 4KB | SCRATCH_Y (stack) |

## I2C protocol

| Command | Code | Host sends | Bootloader responds |
|---------|------|-----------|-------------------|
| ACTIVATE | `0xA5` | `[0xA5]` | 4 bytes: "pbt3" (`0x70 0x62 0x74 0x33`) |
| LOAD_RAM | `0x45` | `[0x45] [addr4 LE] [len2 LE] [data...]` | 1 byte: ready (1 = ok) |
| EXEC_RAM | `0x46` | `[0x46] [entry4 LE]` | (jumps to entry, no response) |


## Embedding the FlexGPIO binary

The `tools/generate_flexgpio_ram.py` script runs as a PlatformIO pre-build step for the firmware running on the RP2350. It reads `FlexGPIO_ram.bin` from the build output and generates `src/flexgpio_ram.c`, which contains the binary as a `const uint8_t` array. The RP2350 firmware includes this array and sends it to the RP2040 over I2C via the `LOAD_RAM` bootloader command at every boot. Paths in this script may need to change if using a different build system.

## Building the Bootloader

Install [pico-sdk](https://github.com/raspberrypi/pico-sdk) and set `PICO_SDK_PATH`.

```bash
cd FlexGPIO_Bootloader
mkdir -p build && cd build
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH ..
make -j$(nproc)
```
Then flash FlexGPIO_bootloader.uf2 to RP2040 via USB or FlexGPIO_bootloader.elf via SWD. This should only need to be done once.