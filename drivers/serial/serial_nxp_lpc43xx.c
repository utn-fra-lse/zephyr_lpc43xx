/**
 * Copyright (c) 2026 UTN FRA - Laboratorio de Sistemas Embebidos
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lpc43xx_uart

#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>

/* CGU (Clock Generation Unit): habilita el "branch clock" propio de cada
 * UART. A diferencia de BASE_M4_CLK (el clock del núcleo, ya configurado
 * en soc.c), estos branches no quedan enrutados a una fuente funcional en
 * el reset -- hay que seleccionar la fuente (CLK_SEL) explícitamente antes
 * de que el periférico responda a nada. Mismo layout de campo que
 * BASE_M4_CLK: CLK_SEL en bits [28:24], 0x09 = PLL1. Direcciones de UM10503
 * cap. 12 (offsets desde el CGU, 0x40050000). */
#define LPC_CGU_BASE          0x40050000UL

#define BASE_UART0_CLK_OFFSET 0x09CU /* USART0 */
#define BASE_UART1_CLK_OFFSET 0x0A0U /* UART1  */
#define BASE_UART2_CLK_OFFSET 0x0A4U /* USART2 */
#define BASE_UART3_CLK_OFFSET 0x0A8U /* USART3 */

#define BASE_CLK_SEL_PLL1     (0x09U << 24)

/* Mapea la dirección base del periférico (la que viene de DT_INST_REG_ADDR)
 * al offset de su BASE_UARTn_CLK correspondiente -- no siguen una
 * progresión aritmética uniforme, así que no se puede derivar por cálculo. */
static volatile uint32_t *base_uart_clk_reg(uintptr_t uart_base)
{
	uint32_t offset;

	switch (uart_base) {
	case 0x40081000UL:
		offset = BASE_UART0_CLK_OFFSET;
		break;
	case 0x40082000UL:
		offset = BASE_UART1_CLK_OFFSET;
		break;
	case 0x400C1000UL:
		offset = BASE_UART2_CLK_OFFSET;
		break;
	case 0x400C2000UL:
		offset = BASE_UART3_CLK_OFFSET;
		break;
	default:
		return NULL;
	}

	return (volatile uint32_t *)(LPC_CGU_BASE + offset);
}

#define UART_LCR_WLEN8          (3 << 0)		/*!< UART word length select: 8 bit data mode */
#define UART_LCR_DLAB_EN        (1 << 7)		/*!< UART Divisor Latches Access bit enable */
#define UART_FCR_FIFO_EN        (1 << 0)	  /*!< UART FIFO enable */
#define UART_FCR_RX_RS          (1 << 1)	  /*!< UART RX FIFO reset */
#define UART_FCR_TX_RS          (1 << 2)	  /*!< UART TX FIFO reset */
#define UART_LSR_RDR            (1 << 0)	  /*!< Line status: Receive data ready */
#define UART_LSR_THRE           (1 << 5)	  /*!< Line status: Transmit holding register empty */

/** 
 * Estructura de registros para periféricos UART, mapeada en el offset
 * 0x4000 del periférico UART (0x40081000 en direcciones absolutas). 
 */
struct lpc43xx_uart_regs {
  union {
    volatile uint32_t thr;    /*!< 0x4000: Transmit Holding Register */
    volatile uint32_t rbr;    /*!< 0x4000: Receiver Buffer Register */
    volatile uint32_t dll;    /*!< 0x4000: Divisor Latch LSB */
  };
  union {
    volatile uint32_t ier;	  /*!< 0x4004: Interrupt Enable Register */
    volatile uint32_t dlm;	  /*!< 0x4004: Divisor Latch MSB */
  };
  union {
    volatile uint32_t iir;	  /*!< 0x4008: Interrupt Identification Register */
    volatile uint32_t fcr;	  /*!< 0x4008: FIFO Control Register */
  };
  volatile uint32_t lcr;	    /*!< 0x400C: Line Control Register */
  volatile uint32_t mcr;	    /*!< 0x4010: Modem Control Register */
  volatile uint32_t lsr;	    /*!< 0x4014: Line Status Register */
};

/** Estructura de configuración del periférico UART */
struct lpc43xx_uart_config {
	struct lpc43xx_uart_regs *regs;
  uint32_t current_baudrate;
  const struct pinctrl_dev_config *pincfg;
};

static int uart_lpc43xx_poll_in(const struct device *dev, unsigned char *c) {
  const struct lpc43xx_uart_config *cfg = dev->config;
  // Revisa el registro LSR para ver si hay datos disponibles. Si no hay datos, devuelve -1.
  if(!(cfg->regs->lsr & UART_LSR_RDR)) {
    return -1;
  }
  *c = (unsigned char) cfg->regs->rbr;
  return 0;
}

static void uart_lpc43xx_poll_out(const struct device *dev, unsigned char c) {
  const struct lpc43xx_uart_config *cfg = dev->config;
  // Espera hasta que el registro LSR indique que el THR está vacío.
  while(!(cfg->regs->lsr & UART_LSR_THRE));
  cfg->regs->thr = (uint32_t) c;
}

static int uart_lpc43xx_init(const struct device *dev) {
  const struct lpc43xx_uart_config *cfg = dev->config;

  // Habilita el branch clock de este UART en la CGU. Tiene que pasar antes
  // que cualquier acceso a lcr/dll/dlm/fcr: si el clock sigue apagado esos
  // registros no llegan a reaccionar (es lo que causaba el cuelgue en
  // poll_out esperando THRE para siempre).
  volatile uint32_t *base_clk = base_uart_clk_reg((uintptr_t)cfg->regs);
  if (base_clk == NULL) {
    return -ENODEV;
  }
  *base_clk = BASE_CLK_SEL_PLL1;

  // Aplica la configuración de pines para el periférico UART.
  int ret = pinctrl_apply_state(cfg->pincfg, PINCTRL_STATE_DEFAULT);
  if(ret < 0) {
    return ret;
  }
  // Configura el registro LCR para habilitar el acceso a los registros divisor y establecer la longitud de palabra a 8 bits.
  cfg->regs->lcr = UART_LCR_DLAB_EN | UART_LCR_WLEN8;
  // Solo es válido porque Chip_SetupXtalClocking() ya fue llamado en el arranque del sistema y CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC es la frecuencia del cristal.
  uint32_t baudrate_divisor = (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / (16 * cfg->current_baudrate));
  cfg->regs->dll = (uint32_t)(baudrate_divisor & 0xFF);
  cfg->regs->dlm = (uint32_t)((baudrate_divisor >> 8) & 0xFF);
  cfg->regs->lcr &= ~UART_LCR_DLAB_EN;
  // Habilita y reinicia los FIFOs.
  cfg->regs->fcr = UART_FCR_FIFO_EN | UART_FCR_RX_RS | UART_FCR_TX_RS;
  return 0;
}

// Implementación de la API de UART para el periférico LPC43xx.
static DEVICE_API(uart, uart_lpc43xx_api) = {
	.poll_in =  uart_lpc43xx_poll_in,
	.poll_out = uart_lpc43xx_poll_out
};

#define UART_LPC43XX_INIT(n)  \
  PINCTRL_DT_INST_DEFINE(n);   \
  static const struct lpc43xx_uart_config uart_lpc43xx_cfg_##n = { \
    .regs = (struct lpc43xx_uart_regs *)DT_INST_REG_ADDR(n), \
    .current_baudrate = DT_INST_PROP(n, current_speed), \
    .pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n), \
  }; \
  DEVICE_DT_INST_DEFINE(n,  \
    uart_lpc43xx_init, NULL, NULL, &uart_lpc43xx_cfg_##n, \
    PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &uart_lpc43xx_api);

// Instancia el controlador UART para cada nodo compatible en el Device Tree.
DT_INST_FOREACH_STATUS_OKAY(UART_LPC43XX_INIT)