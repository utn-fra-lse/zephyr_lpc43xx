/*
 * Copyright (c) 2026 UTN FRA - Laboratorio de Sistemas Embebidos
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SOC__H_
#define _SOC__H_

#ifndef _ASMLANGUAGE
/* cmsis_core_m_defaults.h completa __NVIC_PRIO_BITS a partir del DTS
 * (arm,num-irq-priority-bits = <3>), __MPU_PRESENT a partir de
 * CONFIG_CPU_HAS_ARM_MPU (1), __FPU_PRESENT a partir de CONFIG_CPU_HAS_FPU (1),
 * y luego incluye core_cm4.h — que provee SCB, IRQn_Type, intrínsecos CMSIS, etc.
 * NO incluir cmsis.h de LPCOpen acá; taparía estos headers. */
#include <cmsis_core_m_defaults.h>
#endif /* !_ASMLANGUAGE */

#endif /* _SOC__H_ */