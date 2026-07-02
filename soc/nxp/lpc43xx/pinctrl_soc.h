#ifndef ZEPHYR_SOC_ARM_NXP_LPC43XX_PINCTRL_SOC_H_
#define ZEPHYR_SOC_ARM_NXP_LPC43XX_PINCTRL_SOC_H_

#include <zephyr/types.h>
#include <dt-bindings/pinctrl/lpc43xx-pinctrl.h>

typedef uint32_t pinctrl_soc_pin_t;

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx) \
	DT_PROP_BY_IDX(node_id, prop, idx),

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                     \
	{ DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop),          \
				 DT_FOREACH_PROP_ELEM, pinmux,        \
				 Z_PINCTRL_STATE_PIN_INIT) }

#endif