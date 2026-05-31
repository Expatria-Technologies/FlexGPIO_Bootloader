#define I2C_SDA_PIN         6
#define I2C_SCL_PIN         7
#define I2C_BAUD            1000000
#define PICOBOOT3_ADDR      0x48
#define PICOBOOT3_ACTIVATE  0xA5
#define PICOBOOT3_LOAD_RAM  0x45
#define PICOBOOT3_EXEC_RAM  0x46
#define CHUNK_SIZE          4096
#define ACTIVATE_RETRIES    5
#define ACTIVATE_RETRY_MS   10

#include <hardware/i2c.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rp2040_ram_loader.h"

static const uint8_t activate_response[4] = {0x70, 0x62, 0x74, 0x33};

static void i2c_init_host(void)
{
    i2c_init(i2c1, I2C_BAUD);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

static void send_magic_packet(void)
{
    printf("  sending magic...\n");
    uint8_t magic[8];
    memset(magic, 0xFF, 8);
    i2c_write_blocking(i2c1, PICOBOOT3_ADDR, magic, 8, false);
    printf("  magic sent\n");
}

static int picoboot3_activate(void)
{
    printf("  activate: write\n");
    uint8_t cmd = PICOBOOT3_ACTIVATE;
    if (i2c_write_blocking(i2c1, PICOBOOT3_ADDR, &cmd, 1, false) != 1) {
        printf("  activate: write NACK\n");
        return -1;
    }

    printf("  activate: read\n");
    sleep_us(10);

    uint8_t resp[4];
    if (i2c_read_blocking(i2c1, PICOBOOT3_ADDR, resp, 4, false) != 4) {
        printf("  activate: read failed\n");
        return -1;
    }

    int ok = (memcmp(resp, activate_response, 4) == 0);
    printf("  activate: %s\n", ok ? "OK" : "bad response");
    return ok ? 0 : -1;
}

static int ram_load_chunk(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint8_t buf[7 + CHUNK_SIZE];
    buf[0] = PICOBOOT3_LOAD_RAM;
    buf[1] = addr & 0xFF;
    buf[2] = (addr >> 8) & 0xFF;
    buf[3] = (addr >> 16) & 0xFF;
    buf[4] = (addr >> 24) & 0xFF;
    buf[5] = len & 0xFF;
    buf[6] = (len >> 8) & 0xFF;
    memcpy(buf + 7, data, len);

    if (i2c_write_blocking(i2c1, PICOBOOT3_ADDR, buf, 7 + len, false) != 7 + len)
        return -1;

    sleep_us(10);

    uint8_t ready;
    if (i2c_read_blocking(i2c1, PICOBOOT3_ADDR, &ready, 1, false) != 1)
        return -1;

    return (ready == 1) ? 0 : -1;
}

static int ram_execute(uint32_t entry)
{
    uint8_t buf[5];
    buf[0] = PICOBOOT3_EXEC_RAM;
    buf[1] = entry & 0xFF;
    buf[2] = (entry >> 8) & 0xFF;
    buf[3] = (entry >> 16) & 0xFF;
    buf[4] = (entry >> 24) & 0xFF;

    return (i2c_write_blocking(i2c1, PICOBOOT3_ADDR, buf, 5, false) == 5) ? 0 : -1;
}

static int load_and_run(const uint8_t *data, uint32_t len, uint32_t entry)
{
    uint32_t loaded = 0;

    while (loaded < len) {
        uint16_t chunk = (len - loaded > CHUNK_SIZE) ? CHUNK_SIZE : (len - loaded);
        printf("  load chunk %d @ 0x%08x\n", chunk, entry + loaded);
        if (ram_load_chunk(entry + loaded, data + loaded, chunk) != 0)
            return -1;
        loaded += chunk;
    }

    printf("  exec ram @ 0x%08x\n", entry);
    return ram_execute(entry);
}

static int enter_bootloader(bool try_activate_first)
{
    if (try_activate_first && picoboot3_activate() == 0)
        return 0;

    send_magic_packet();

    for (int i = 0; i < ACTIVATE_RETRIES; i++) {
        if (picoboot3_activate() == 0)
            return 0;
        sleep_ms(ACTIVATE_RETRY_MS);
    }

    return -1;
}

int rp2040_loader_start(const uint8_t *firmware, uint32_t len, uint32_t entry)
{
    printf("rp2040_loader: entering\n");
    i2c_init_host();
    printf("rp2040_loader: i2c init done\n");

    printf("rp2040_loader: contacting picoboot3...\n");
    if (enter_bootloader(true) != 0) {
        printf("rp2040_loader: enter failed\n");
        return -1;
    }
    printf("rp2040_loader: picoboot3 ready, loading...\n");

    int ret = load_and_run(firmware, len, entry);
    printf("rp2040_loader: done (%d)\n", ret);
    return ret;
}

int rp2040_loader_reload(const uint8_t *firmware, uint32_t len, uint32_t entry)
{
    i2c_init_host();
    if (enter_bootloader(false) != 0)
        return -1;
    return load_and_run(firmware, len, entry);
}
