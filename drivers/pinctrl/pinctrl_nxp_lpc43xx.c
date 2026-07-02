#include <zephyr/drivers/pinctrl.h>

#define SCU_BASE   DT_REG_ADDR(DT_NODELABEL(pinctrl))

#define SFSP_MODE_MASK   0x7U
#define SFSP_EPD         BIT(3)
#define SFSP_EPUN        BIT(4)
#define SFSP_EZI         BIT(6)

static volatile uint32_t *scu_sfsp(uint8_t port, uint8_t pin)
{
	return (volatile uint32_t *)(SCU_BASE + (uint32_t)port * 0x80U +
				      (uint32_t)pin * 0x4U);
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt,
			   uintptr_t reg)
{
	ARG_UNUSED(reg);

	for (uint8_t i = 0; i < pin_cnt; i++) {
		uint32_t mux   = pins[i];
		uint8_t port   = LPC43XX_PORT_NUM(mux);
		uint8_t pin    = LPC43XX_PIN_NUM(mux);
		uint8_t func   = LPC43XX_FUNC(mux);
		uint8_t bias   = LPC43XX_BIAS(mux);
		uint8_t in_en  = LPC43XX_INPUT_EN(mux);

		uint32_t val = func & SFSP_MODE_MASK;

		if (bias == LPC43XX_PULL_DOWN || bias == LPC43XX_REPEATER) {
			val |= SFSP_EPD;
		}
		if (bias == LPC43XX_PULL_NONE || bias == LPC43XX_PULL_DOWN) {
			val |= SFSP_EPUN;
		}
		if (in_en) {
			val |= SFSP_EZI;
		}

		*scu_sfsp(port, pin) = val;
	}

	return 0;
}