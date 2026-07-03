# Copyright (c) 2026 UTN FRA - Laboratorio de Sistemas Embebidos
# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd
  "--config=${BOARD_DIR}/support/openocd.cfg"
)

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
