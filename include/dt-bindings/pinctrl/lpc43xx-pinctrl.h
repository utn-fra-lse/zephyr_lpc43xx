/*
 * Copyright (c) 2026 UTN FRA - Laboratorio de Sistemas Embebidos
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_LPC43XX_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_LPC43XX_PINCTRL_H_

/*
 * Codificación de pines de la SCU (System Control Unit) del LPC43xx como una
 * sola palabra de 32 bits. Ver PORTING_GUIDE.md sección 6 para el detalle del
 * layout de bits y de los registros SFSPx_y.
 *
 * bits [4:0]   número de pin (0-31)
 * bits [8:5]   número de puerto SCU (0-15: P0-P9 = 0-9, PA-PF = 10-15)
 * bits [11:9]  función (0-7, campo MODE del registro SFSPx_y)
 * bits [13:12] modo de pull (0=pull-up, 1=repeater, 2=ninguno, 3=pull-down)
 * bit  [14]    habilitar buffer de entrada (EZI)
 */
#define LPC43XX_PIN(port, pin, func, bias, input_en) \
	(((pin) & 0x1F) | (((port) & 0xF) << 5) | \
	 (((func) & 0x7) << 9) | (((bias) & 0x3) << 12) | \
	 (((input_en) & 0x1) << 14))

/* Macros de decodificación usadas por el driver en C */
#define LPC43XX_PIN_NUM(mux)   ((mux) & 0x1F)
#define LPC43XX_PORT_NUM(mux)  (((mux) >> 5) & 0xF)
#define LPC43XX_FUNC(mux)      (((mux) >> 9) & 0x7)
#define LPC43XX_BIAS(mux)      (((mux) >> 12) & 0x3)
#define LPC43XX_INPUT_EN(mux)  (((mux) >> 14) & 0x1)

#define LPC43XX_PULL_UP    0
#define LPC43XX_REPEATER   1
#define LPC43XX_PULL_NONE  2
#define LPC43XX_PULL_DOWN  3

/*
 * Pines con nombre pre-codificados para la placa EDU-CIAA-NXP.
 *
 * Puerto/pin SCU y número de función (FUNC) confirmados contra la tabla de
 * pines de la sAPI del firmware oficial del proyecto CIAA
 * (github.com/ciaa/firmware_v2, modules/lpc4337_m4/sapi/src/sapi_gpio.c).
 * El GPIO resultante (puerto/bit dentro de GPIO_PORT) se indica en el
 * comentario de cada macro para referencia cruzada con gpio_nxp_lpc43xx.c.
 *
 * Notas importantes:
 * - LEDR/LEDG/LEDB (el LED RGB) están en el grupo de pines P2_0..P2_2, cuya
 *   función GPIO NO es la de reset (FUNC0): hay que programar FUNC4
 *   explícitamente en la SCU antes de poder usarlos como GPIO. Por eso estos
 *   pines requieren un nodo pinctrl (no alcanza con el gpio-controller solo).
 * - LED1/LED2/LED3 y TEC1-TEC4 SÍ son GPIO en FUNC0, que es la función de
 *   reset de esos pines — en la práctica funcionan como GPIO sin necesidad
 *   de pinctrl, pero igual se codifican acá para que quede documentado y
 *   sea trivial migrar a una placa donde el reset default sea distinto.
 */

/* LED RGB (LEDR=P2_0/GPIO5[0], LEDG=P2_1/GPIO5[1], LEDB=P2_2/GPIO5[2]) */
#define EDU_CIAA_PIN_LEDR  LPC43XX_PIN(2, 0, 4, LPC43XX_PULL_NONE, 0)
#define EDU_CIAA_PIN_LEDG  LPC43XX_PIN(2, 1, 4, LPC43XX_PULL_NONE, 0)
#define EDU_CIAA_PIN_LEDB  LPC43XX_PIN(2, 2, 4, LPC43XX_PULL_NONE, 0)

/* LEDs individuales (LED1=P2_10/GPIO0[14], LED2=P2_11/GPIO1[11], LED3=P2_12/GPIO1[12]) */
#define EDU_CIAA_PIN_LED1  LPC43XX_PIN(2, 10, 0, LPC43XX_PULL_NONE, 0)
#define EDU_CIAA_PIN_LED2  LPC43XX_PIN(2, 11, 0, LPC43XX_PULL_NONE, 0)
#define EDU_CIAA_PIN_LED3  LPC43XX_PIN(2, 12, 0, LPC43XX_PULL_NONE, 0)

/* Pulsadores (TEC1=P1_0/GPIO0[4], TEC2=P1_1/GPIO0[8], TEC3=P1_2/GPIO0[9], TEC4=P1_6/GPIO1[9]) */
#define EDU_CIAA_PIN_TEC1  LPC43XX_PIN(1, 0, 0, LPC43XX_PULL_UP, 1)
#define EDU_CIAA_PIN_TEC2  LPC43XX_PIN(1, 1, 0, LPC43XX_PULL_UP, 1)
#define EDU_CIAA_PIN_TEC3  LPC43XX_PIN(1, 2, 0, LPC43XX_PULL_UP, 1)
#define EDU_CIAA_PIN_TEC4  LPC43XX_PIN(1, 6, 0, LPC43XX_PULL_UP, 1)

/* UART usado para consola por USB (UART2) */
#define EDU_CIAA_PIN_UART2_TXD	LPC43XX_PIN(7, 1, 6, LPC43XX_PULL_NONE, 1)
#define EDU_CIAA_PIN_UART2_RXD	LPC43XX_PIN(7, 2, 6, LPC43XX_PULL_NONE, 1)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_LPC43XX_PINCTRL_H_ */
