# Copyright (c) 2026 UTN FRA - Laboratorio de Sistemas Embebidos
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SOC_LPC4337)

  # El header dt-bindings (lpc43xx-pinctrl.h) debe ser visible globalmente para
  # que los archivos DTS y los drivers lo incluyan con <dt-bindings/pinctrl/...>.
  zephyr_include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/../include
  )

  # CHIP_LPC43XX selecciona el mapa de registros LPC43xx dentro de LPCOpen.
  # CORE_M4 es requerido por varios headers de LPCOpen para elegir las rutas
  # específicas del Cortex-M4. Deben definirse globalmente porque cualquier
  # driver que incluya chip.h los necesita ver.
  zephyr_compile_definitions(
    CHIP_LPC43XX
    CORE_M4
  )

  # Librería HAL — el include path de LPCOpen se limita solo a esta librería.
  # NO usar zephyr_include_directories() para lpc_chip_43xx/include porque
  # contiene su propio core_cm4.h / cmsis.h que entran en conflicto con el
  # CMSIS incluido en Zephyr cuando se agregan globalmente.
  zephyr_library_named(hal_lpc43xx)
  zephyr_library_include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/include
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/include/config_43xx
      ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/include/usbd_rom
  )
  zephyr_library_sources(
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/chip_18xx_43xx.c
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/clock_18xx_43xx.c
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/sysinit_18xx_43xx.c
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/uart_18xx_43xx.c
  )
  # clock_18xx_43xx.c    -> funciones Chip_Clock_* (CGU: PLL1, dividers, base clocks)
  # sysinit_18xx_43xx.c  -> Chip_SetupXtalClocking() / Chip_SystemInit()
  # chip_18xx_43xx.c     -> define SystemCoreClock / SystemCoreClockUpdate(),
  #                          requerido por el resto de los módulos de LPCOpen
  #
  # El multiplexado de pines (SCU) NO tiene un .c propio: Chip_SCU_PinMuxSet()
  # y compañía están definidas STATIC INLINE en scu_18xx_43xx.h, así que no
  # hace falta (ni existe) un scu_43xx.c para agregar a la librería.

  # Agregar más fuentes de LPCOpen acá a medida que se implementan drivers:
  # zephyr_library_sources_ifdef(CONFIG_UART_LPC43XX
  #   ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/uart_18xx_43xx.c
  # )

  if(CONFIG_PINCTRL)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../drivers/pinctrl
                     ${CMAKE_CURRENT_BINARY_DIR}/drivers/pinctrl)
  endif()

  if(CONFIG_GPIO)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../drivers/gpio
                     ${CMAKE_CURRENT_BINARY_DIR}/drivers/gpio)
  endif()

  if(CONFIG_UART_LPC43XX)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../drivers/serial
                     ${CMAKE_CURRENT_BINARY_DIR}/drivers/serial)
  endif()

endif()