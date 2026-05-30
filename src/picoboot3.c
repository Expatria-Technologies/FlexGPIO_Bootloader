/*
 * Copyright (c) 2024 Indoor Corgi
 *
 * SPDX-License-Identifier: MIT
 */

#include "picoboot3.h"

#include <stdio.h>
#include <string.h>

#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#define RECEIVE_BUFFER_SIZE 4200

// To determine interface
#define NO_INTERFACE 0
#define I2C_INTERFACE 1

// Command codes
#define NO_COMMAND 0
#define READY_BUSY_COMMAND 0x1
#define VERSION_COMMAND 0x2
#define GO_TO_APPCODE_COMMAND 0x40
#define ACTIVATE_COMMAND 0xA5
#define LOAD_RAM_COMMAND 0x45
#define EXEC_RAM_COMMAND 0x46

// To determine what data to be sent to the host
#define SEND_NONE 0
#define SEND_READY_BUSY 1
#define SEND_ACTIVATION_RESPONSE 2
#define SEND_VERSION 3

// Vector table offset
#if PICO_RP2040
#define VTOR_OFFSET M0PLUS_VTOR_OFFSET
#elif PICO_RP2350
#define VTOR_OFFSET M33_VTOR_OFFSET
#endif

typedef struct {
  uint8_t command;
  uint8_t target_address[4];
  uint8_t num_of_bytes[2];
  uint8_t data[RECEIVE_BUFFER_SIZE - 7];
} load_ram_command_t;

typedef struct {
  uint8_t command;
  uint8_t entry_point[4];
} exec_ram_command_t;

int activated_interface = NO_INTERFACE;
const uint8_t activation_response[] = PICOBOOT3_ACTIVATION_RESPONSE;
const uint8_t version[] = {PICOBOOT3_MAJOR_VERSION,
                           PICOBOOT3_MINOR_VERSION,
                           PICOBOOT3_PATCH_VERSION};
uint8_t ready = 1;

uint8_t i2c_receive_buffer[RECEIVE_BUFFER_SIZE];
int i2c_receive_counter = 0;
int i2c_select_send_data = SEND_NONE;
int i2c_send_counter = 0;

uint8_t reserved_command = NO_COMMAND;
exec_ram_command_t reserved_exec_ram_command;

// Branches to application code and does not return
// Before calling this, deinit all used resources.
void picoboot3_go_to_appcode() {
  asm volatile(
      "ldr r0, =%[appcode]\n"
      "ldr r1, =%[vtor]\n"
      "str r0, [r1]\n"
      "ldmia r0, {r0, r1}\n"
      "msr msp, r0\n"
      "bx r1\n"
      :
      : [appcode] "i"(XIP_BASE + PICOBOOT3_APPCODE_OFFSET), [vtor] "i"(PPB_BASE + VTOR_OFFSET)
      :);
}

void picoboot3_bootsel_init() {
  gpio_init(PICOBOOT3_BOOTSEL3_PIN);
  if (PICOBOOT3_BOOTSEL3_PULLUP) {
    gpio_pull_up(PICOBOOT3_BOOTSEL3_PIN);
  }
}

void picoboot3_bootsel_deinit() {
  gpio_deinit(PICOBOOT3_BOOTSEL3_PIN);
}

bool picoboot3_bootsel_is_bootloader() {
  if (watchdog_hw->scratch[0]) {
    watchdog_hw->scratch[0] = 0;
    return true;
  }
  if (PICOBOOT3_BOOTSEL3_VAL_TO_START_BOOTLOADER == gpio_get(PICOBOOT3_BOOTSEL3_PIN)) {
    return true;
  }
  return false;
}

void picoboot3_i2c_init() {
  gpio_init(PICOBOOT3_I2C_SDA_PIN);
  gpio_set_function(PICOBOOT3_I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(PICOBOOT3_I2C_SDA_PIN);

  gpio_init(PICOBOOT3_I2C_SCL_PIN);
  gpio_set_function(PICOBOOT3_I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(PICOBOOT3_I2C_SCL_PIN);

  i2c_slave_init(PICOBOOT3_I2C_INST, PICOBOOT3_I2C_ADDRESS, &picoboot3_i2c_slave_handler);
}

void picoboot3_i2c_deinit() {
  i2c_slave_deinit(PICOBOOT3_I2C_INST);
  gpio_deinit(PICOBOOT3_I2C_SDA_PIN);
  gpio_deinit(PICOBOOT3_I2C_SCL_PIN);
}

// I2C interrupt handler
void picoboot3_i2c_slave_handler(i2c_inst_t* i2c, i2c_slave_event_t event) {
  switch (event) {
    case I2C_SLAVE_RECEIVE:
      if (i2c_receive_counter < RECEIVE_BUFFER_SIZE) {
        i2c_receive_buffer[i2c_receive_counter++] = i2c_read_byte_raw(i2c);
      } else {
        i2c_read_byte_raw(i2c);
      }
      break;
    case I2C_SLAVE_REQUEST:
      switch (i2c_select_send_data) {
        case SEND_READY_BUSY:
          i2c_write_byte_raw(i2c, ready);
          break;

        case SEND_VERSION:
          i2c_write_byte_raw(i2c, version[i2c_send_counter++]);
          if (i2c_send_counter >= sizeof(version)) {
            i2c_send_counter = 0;
          }
          break;

        case SEND_ACTIVATION_RESPONSE:
          i2c_write_byte_raw(i2c, activation_response[i2c_send_counter++]);
          if (i2c_send_counter >= sizeof(activation_response)) {
            i2c_send_counter = 0;
          }
          break;

        default:
          i2c_write_byte_raw(i2c, 0);
          break;
      }
      break;
    case I2C_SLAVE_FINISH:
      if (i2c_receive_counter > 0) picoboot3_i2c_command_handler(I2C_INTERFACE);
      i2c_receive_counter = 0;
      break;
    default:
      break;
  }
}

// Handle received commands from I2C
// Called by picoboot3_i2c_slave_handler
void picoboot3_i2c_command_handler() {
  uint16_t num_of_bytes;

  if (i2c_receive_buffer[0] == ACTIVATE_COMMAND) {
    if (activated_interface != NO_INTERFACE && activated_interface != I2C_INTERFACE)
      return;  // Only one interface is able to be acvive
  } else {
    if (activated_interface != I2C_INTERFACE)
      return;  // Other commands are valid after activate command
  }

  if (!ready && i2c_receive_buffer[0] != READY_BUSY_COMMAND)
    return;  // If busy, accept ready/busy command only

  switch (i2c_receive_buffer[0]) {
    case READY_BUSY_COMMAND:
      if (i2c_receive_counter != 1) break;
      i2c_select_send_data = SEND_READY_BUSY;
      break;

    case VERSION_COMMAND:
      if (i2c_receive_counter != 1) break;
      i2c_select_send_data = SEND_VERSION;
      i2c_send_counter = 0;
      break;

    case LOAD_RAM_COMMAND:
      if (i2c_receive_counter < 7) break;
      num_of_bytes = *(uint16_t*)(i2c_receive_buffer + 5);
      if (i2c_receive_counter < 7 + num_of_bytes) break;
      {
        uint32_t target_addr = *(uint32_t*)(i2c_receive_buffer + 1);
        memcpy((void*)target_addr, i2c_receive_buffer + 7, num_of_bytes);
      }
      i2c_select_send_data = SEND_READY_BUSY;
      break;

    case EXEC_RAM_COMMAND:
      if (i2c_receive_counter != sizeof(exec_ram_command_t)) break;
      memcpy(&reserved_exec_ram_command, i2c_receive_buffer, sizeof(exec_ram_command_t));
      i2c_select_send_data = SEND_READY_BUSY;
      reserved_command = EXEC_RAM_COMMAND;
      ready = 0;
      break;

    case GO_TO_APPCODE_COMMAND:
      if (i2c_receive_counter != 1) break;
      i2c_select_send_data = SEND_READY_BUSY;
      reserved_command = GO_TO_APPCODE_COMMAND;
      ready = 0;
      break;

    case ACTIVATE_COMMAND:
      if (i2c_receive_counter != 1) break;
      activated_interface = I2C_INTERFACE;
      i2c_select_send_data = SEND_ACTIVATION_RESPONSE;
      i2c_send_counter = 0;
      break;

    default:
      break;
  }
}

// Call this in the main loop
void picoboot3_reserved_command_handler() {
  switch (reserved_command) {
    case GO_TO_APPCODE_COMMAND:
      picoboot3_bootsel_deinit();
      picoboot3_i2c_deinit();
      picoboot3_go_to_appcode();
      break;

    case EXEC_RAM_COMMAND: {
      uint32_t entry = *(uint32_t*)reserved_exec_ram_command.entry_point;
      picoboot3_i2c_deinit();
      uint32_t interrupt_status = save_and_disable_interrupts();
      asm volatile(
          "str %[entry], [%[vtor]]\n"
          "ldr r0, [%[entry]]\n"
          "msr msp, r0\n"
          "ldr r1, [%[entry], #4]\n"
          "bx r1\n"
          :
          : [entry] "r"(entry), [vtor] "r"(PPB_BASE + VTOR_OFFSET)
          : "r0", "r1");
      break;
    }

    default:
      break;
  }
}
