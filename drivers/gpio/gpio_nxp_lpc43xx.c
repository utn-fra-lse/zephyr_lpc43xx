#define DT_DRV_COMPAT nxp_lpc43xx_gpio_port

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>

/* Estructura de registros compartida por los 8 puertos, mapeada en el offset
 * 0x2000 del periférico GPIO_PORT (0x400F6000 en direcciones absolutas). */
struct lpc43xx_gpio_regs {
	volatile uint32_t DIR[8];
	volatile uint32_t RESERVED0[24];
	volatile uint32_t MASK[8];
	volatile uint32_t RESERVED1[24];
	volatile uint32_t PIN[8];
	volatile uint32_t RESERVED2[24];
	volatile uint32_t MPIN[8];
	volatile uint32_t RESERVED3[24];
	volatile uint32_t SET[8];
	volatile uint32_t RESERVED4[24];
	volatile uint32_t CLR[8];
	volatile uint32_t RESERVED5[24];
	volatile uint32_t NOT[8];
};

#define LPC43XX_GPIO_REGS  ((struct lpc43xx_gpio_regs *)0x400F6000UL)

struct gpio_lpc43xx_config {
	struct gpio_driver_config common;
	uint8_t port;
};

struct gpio_lpc43xx_data {
	struct gpio_driver_data common;
};

static int gpio_lpc43xx_pin_configure(const struct device *dev,
				      gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_lpc43xx_config *cfg = dev->config;
	struct lpc43xx_gpio_regs *regs = LPC43XX_GPIO_REGS;

	if ((flags & GPIO_SINGLE_ENDED) != 0) {
		return -ENOTSUP;
	}
	if ((flags & GPIO_OUTPUT) != 0) {
		regs->DIR[cfg->port] |= BIT(pin);
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			regs->SET[cfg->port] = BIT(pin);
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			regs->CLR[cfg->port] = BIT(pin);
		}
	} else {
		regs->DIR[cfg->port] &= ~BIT(pin);
	}
	return 0;
}

static int gpio_lpc43xx_port_get_raw(const struct device *dev,
				     gpio_port_value_t *value)
{
	const struct gpio_lpc43xx_config *cfg = dev->config;

	*value = LPC43XX_GPIO_REGS->PIN[cfg->port];
	return 0;
}

static int gpio_lpc43xx_port_set_masked_raw(const struct device *dev,
					    gpio_port_pins_t mask,
					    gpio_port_value_t value)
{
	const struct gpio_lpc43xx_config *cfg = dev->config;
	struct lpc43xx_gpio_regs *regs = LPC43XX_GPIO_REGS;

	regs->SET[cfg->port] = mask & value;
	regs->CLR[cfg->port] = mask & ~value;
	return 0;
}

static int gpio_lpc43xx_port_set_bits_raw(const struct device *dev,
					  gpio_port_pins_t pins)
{
	const struct gpio_lpc43xx_config *cfg = dev->config;

	LPC43XX_GPIO_REGS->SET[cfg->port] = pins;
	return 0;
}

static int gpio_lpc43xx_port_clear_bits_raw(const struct device *dev,
					    gpio_port_pins_t pins)
{
	const struct gpio_lpc43xx_config *cfg = dev->config;

	LPC43XX_GPIO_REGS->CLR[cfg->port] = pins;
	return 0;
}

static int gpio_lpc43xx_port_toggle_bits(const struct device *dev,
					 gpio_port_pins_t pins)
{
	const struct gpio_lpc43xx_config *cfg = dev->config;

	LPC43XX_GPIO_REGS->NOT[cfg->port] = pins;
	return 0;
}

static int gpio_lpc43xx_pin_interrupt_configure(const struct device *dev,
						gpio_pin_t pin,
						enum gpio_int_mode mode,
						enum gpio_int_trig trig)
{
	return -ENOTSUP;  /* soporte de interrupciones aún no implementado */
}

static int gpio_lpc43xx_manage_callback(const struct device *dev,
					struct gpio_callback *cb, bool set)
{
	return -ENOTSUP;
}

static const struct gpio_driver_api gpio_lpc43xx_api = {
	.pin_configure           = gpio_lpc43xx_pin_configure,
	.port_get_raw            = gpio_lpc43xx_port_get_raw,
	.port_set_masked_raw     = gpio_lpc43xx_port_set_masked_raw,
	.port_set_bits_raw       = gpio_lpc43xx_port_set_bits_raw,
	.port_clear_bits_raw     = gpio_lpc43xx_port_clear_bits_raw,
	.port_toggle_bits        = gpio_lpc43xx_port_toggle_bits,
	.pin_interrupt_configure = gpio_lpc43xx_pin_interrupt_configure,
	.manage_callback         = gpio_lpc43xx_manage_callback,
};

static int gpio_lpc43xx_init(const struct device *dev)
{
	return 0;
}

#define GPIO_LPC43XX_INIT(n)                                          \
	static struct gpio_lpc43xx_data gpio_lpc43xx_data_##n;              \
	static const struct gpio_lpc43xx_config gpio_lpc43xx_cfg_##n = {    \
		.common = {                                                       \
			.port_pin_mask =                                                \
				GPIO_PORT_PIN_MASK_FROM_DT_INST(n),                           \
		},                                                                \
		.port = DT_INST_REG_ADDR(n),                                      \
	};                                                                  \
	DEVICE_DT_INST_DEFINE(n,                                            \
		gpio_lpc43xx_init, NULL,                                          \
		&gpio_lpc43xx_data_##n, &gpio_lpc43xx_cfg_##n,                    \
		PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,                          \
		&gpio_lpc43xx_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_LPC43XX_INIT)