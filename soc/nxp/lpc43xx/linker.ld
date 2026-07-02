/*
 * Copyright (c) 2026 UTN FRA - Laboratorio de Sistemas Embebidos
 * SPDX-License-Identifier: Apache-2.0
 */

/* CMSIS SystemInit setea VTOR a &__Vectors — alias a la tabla de vectores de Zephyr. */
__Vectors = _vector_table;

#include <zephyr/arch/arm/cortex_m/scripts/linker.ld>