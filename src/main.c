/*
 * Copyright (c) 2024 Indoor Corgi
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "picoboot3.h"

int main() {
  picoboot3_i2c_init();

  while (1) {
    picoboot3_reserved_command_handler();
  }
}
