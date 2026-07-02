/*
 * Copyright (c) 2026 UTN FRA - Laboratorio de Sistemas Embebidos
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

/* Registros de la CGU (Clock Generation Unit), UM10503 cap. 12. */
#define LPC_CGU_BASE     0x40050000UL

#define XTAL_OSC_CTRL  (*(volatile uint32_t *)(LPC_CGU_BASE + 0x018U))
#define PLL1_STAT      (*(volatile uint32_t *)(LPC_CGU_BASE + 0x040U))
#define PLL1_CTRL      (*(volatile uint32_t *)(LPC_CGU_BASE + 0x044U))
#define BASE_M4_CLK    (*(volatile uint32_t *)(LPC_CGU_BASE + 0x06CU))

/* Registros de configuración de flash en el bloque CREG, UM10503 tabla 98. */
#define LPC_CREG_BASE    0x40043000UL

#define FLASHCFGA      (*(volatile uint32_t *)(LPC_CREG_BASE + 0x120U))
#define FLASHCFGB      (*(volatile uint32_t *)(LPC_CREG_BASE + 0x124U))

#define XTAL_OSC_ENABLE   BIT(0)   /* 0 = habilitado (activo bajo) */
#define XTAL_OSC_BYPASS   BIT(1)

#define PLL1_PD        BIT(0)
#define PLL1_BYPASS    BIT(1)
#define PLL1_FBSEL     BIT(6)
#define PLL1_DIRECT    BIT(7)
#define PLL1_STAT_LOCK BIT(0)

/* CLK_SEL = 0x06 selecciona el oscilador de cristal como entrada de PLL1
 * (UM10503 tabla 134). */
#define PLL1_CLK_SEL_XTAL   (0x06U << 24)

/* NSEL = 0 (÷1), MSEL = 16 (×17 → 12 MHz × 17 = 204 MHz).
 * Con DIRECT=1 la salida de PLL1 es Fcco = M * Fin / N directamente,
 * sin post-divisor. 204 MHz cae dentro del rango válido de CCO. */
#define PLL1_NSEL_VAL   (0U << 12)
#define PLL1_MSEL_VAL   (16U << 16)

/* FLASHTIM = 0x9 → 10 ciclos de BASE_M4_CLK por acceso a flash, el ajuste
 * seguro para operar hasta 204 MHz (UM10503 tabla 98). */
#define FLASHCFG_FLASHTIM_204MHZ   (0x9U << 12)
#define FLASHCFG_FLASHTIM_MASK     (0xFU << 12)

void soc_early_init_hook(void)
{
	/* Subir los wait-states de flash antes de aumentar el clock del CPU. */
	FLASHCFGA = (FLASHCFGA & ~FLASHCFG_FLASHTIM_MASK) | FLASHCFG_FLASHTIM_204MHZ;
	FLASHCFGB = (FLASHCFGB & ~FLASHCFG_FLASHTIM_MASK) | FLASHCFG_FLASHTIM_204MHZ;

	/* Habilitar el oscilador de cristal externo. */
	XTAL_OSC_CTRL &= ~(XTAL_OSC_ENABLE | XTAL_OSC_BYPASS);

	/* Poner PLL1 en bypass mientras se reconfigura. */
	PLL1_CTRL |= PLL1_BYPASS;

	/* Configurar PLL1: fuente = cristal, modo directo, M=17, N=1. */
	PLL1_CTRL = PLL1_CLK_SEL_XTAL | PLL1_FBSEL | PLL1_DIRECT |
		    PLL1_NSEL_VAL | PLL1_MSEL_VAL;

	/* Salir de power-down y de bypass. */
	PLL1_CTRL &= ~(PLL1_PD | PLL1_BYPASS);

	/* Esperar a que PLL1 trabe (lock). */
	while (!(PLL1_STAT & PLL1_STAT_LOCK)) {
	}

	/* Conmutar el reloj del núcleo M4 a la salida de PLL1 (CLK_SEL = 0x09,
	 * mismo esquema de codificación que el mux de entrada de PLL1_CTRL). */
	BASE_M4_CLK = (0x09U << 24);
}