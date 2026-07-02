# Guía de port de SoC fuera del árbol (out-of-tree) para Zephyr

**Objetivo de referencia:** NXP LPC4337 — Cortex-M4F (204 MHz) + coprocesador Cortex-M0,
512 KB de Flash (2 bancos de 256 KB), 136 KB de SRAM total
**Versión de Zephyr:** v4.2.0
**Estado:** Guía inicial para arrancar el port. Aún no compilada ni verificada en hardware
real. Se planifica implementar primero GPIO y luego UART (ver [Flasheo](#8-flasheo)
para las notas de depuración/programación).

Esta guía documenta cada archivo, decisión y problema esperable al portar un SoC nuevo
como módulo fuera del árbol de Zephyr. Seguí las fases numeradas en orden. Cada sección
explica qué hace un archivo, por qué existe en ese lugar, y qué sale mal si está mal.

> **Nota sobre el hardware:** el LPC4337 es un chip **dual-core** (Cortex-M4 principal +
> coprocesador Cortex-M0). Las secciones 1 a 9 cubren el port del núcleo M4 solo (el caso
> típico y el que hay que hacer funcionar primero). La sección 10 trata específicamente el
> escenario AMP (Asymmetric Multi-Processing) con ambos núcleos, que es el caso real de
> este chip, no un ejemplo hipotético.

Referencias oficiales:
- [Guía de porting de SoC](https://docs.zephyrproject.org/latest/hardware/porting/soc_porting.html)
- [Guía de porting de placas](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [Bindings de Device Tree](https://docs.zephyrproject.org/latest/build/dts/bindings.html)

Fuentes técnicas usadas para los datos específicos del LPC4337: UM10503 (LPC43xx/LPC43Sxx
User Manual, NXP), el header CMSIS `LPC43xx.h` y la configuración de OpenOCD para la
familia lpc4300. Donde un valor no pudo confirmarse contra una fuente primaria se marca
explícitamente como **[verificar]**.

---

## Estructura del repositorio

```
lpc43xx-zephyr/
├── zephyr/
│   └── module.yml                   # Punto de entrada del módulo Zephyr (Fase 1)
├── Kconfig                          # Kconfig raíz, reenviado por module.yml
├── cmake/
│   └── CMakeLists.txt               # Reglas de build de la HAL del fabricante (Fase 2)
├── lpc_chip_43xx/                   # HAL del fabricante (NXP LPCOpen para LPC43xx)
│   ├── include/                     # chip.h, cmsis.h, headers de periféricos
│   └── src/                         # Fuentes .c de periféricos
├── soc/
│   └── nxp/
│       └── lpc43xx/                 # Capa de SoC (Fase 3)
│           ├── soc.yml
│           ├── Kconfig
│           ├── Kconfig.soc
│           ├── Kconfig.defconfig
│           ├── CMakeLists.txt
│           ├── soc.h
│           ├── soc.c
│           ├── linker.ld
│           └── pinctrl_soc.h
├── dts/
│   ├── arm/nxp/
│   │   └── nxp_lpc4337.dtsi         # Device tree del SoC (Fase 4)
│   └── bindings/
│       ├── gpio/nxp,lpc43xx-gpio.yaml
│       ├── gpio/nxp,lpc43xx-gpio-port.yaml
│       └── pinctrl/nxp,lpc43xx-pinctrl.yaml
├── include/
│   └── dt-bindings/
│       └── pinctrl/
│           └── lpc43xx-pinctrl.h    # Macros de codificación de pines (compartido por DTS y C)
├── boards/
│   └── nxp/
│       └── lpc4337/                 # Capa de placa (Fase 5)
│           ├── board.yml
│           ├── board.cmake
│           ├── Kconfig.lpc4337
│           ├── Kconfig.defconfig
│           ├── lpc4337_lpc4337.dts
│           ├── lpc4337_lpc4337_defconfig
│           └── support/
│               └── lpc4337.cfg
└── drivers/
    ├── pinctrl/                     # Driver de pinctrl (Fase 6)
    │   ├── CMakeLists.txt
    │   └── pinctrl_nxp_lpc43xx.c
    └── gpio/                        # Driver de GPIO (Fase 7)
        ├── CMakeLists.txt
        └── gpio_nxp_lpc43xx.c
```

---

## 1. Registro del módulo

**Archivo: `zephyr/module.yml`**

Es el único archivo que el sistema de build de Zephyr busca al recorrer
`ZEPHYR_EXTRA_MODULES`. Declara las cuatro raíces que Zephyr necesita conocer.

```yaml
build:
  cmake: cmake
  kconfig: Kconfig
  settings:
    board_root: .
    soc_root: .
    dts_root: .
```

- `cmake: cmake` — Zephyr llama a `add_subdirectory(cmake/)` durante el build.
- `kconfig: Kconfig` — se incluye el `Kconfig` raíz del módulo.
- `board_root: .` — Zephyr busca `boards/` dentro de este módulo.
- `soc_root: .` — Zephyr busca `soc/` dentro de este módulo.
- `dts_root: .` — Zephyr busca `dts/` dentro de este módulo (para archivos `.dtsi` y
  `dts/bindings/`).

**Cómo registrarlo con west:**

Opción A — en el `west.yml` del workspace:
```yaml
manifest:
  projects:
    - name: lpc43xx-zephyr
      path: app/lpc43xx-zephyr
      url: <url-del-repo>
```

Opción B — sin modificar `west.yml`, seteando la variable de entorno antes de compilar:
```sh
export ZEPHYR_EXTRA_MODULES=/ruta/a/lpc43xx-zephyr
west build -b lpc4337 app/
```

**Archivo: `Kconfig`** (raíz del módulo)

Este archivo es el primero que lee el árbol de Kconfig de Zephyr:

```kconfig
# Copyright (c) 2026 Universidad
# SPDX-License-Identifier: Apache-2.0
```

El `Kconfig` raíz del módulo puede quedar vacío en ports simples — Zephyr descubre los
archivos Kconfig del SoC automáticamente a través de `soc_root`.

---

## 2. Biblioteca HAL del fabricante

La librería LPCOpen (`lpc_chip_43xx/`) es la HAL provista por NXP para los chips LPC43xx.
Trae su propia copia de CMSIS (`core_cm4.h`, `cmsis.h`).

### El conflicto crítico de CMSIS

La decisión arquitectónica más importante de este port es **aislar los headers CMSIS de
la HAL del fabricante del build de Zephyr**. Este problema es idéntico al que aparece en
cualquier port de un chip NXP LPC, independientemente del núcleo.

Zephyr trae su propio CMSIS-Core a través del módulo west `cmsis_6`. Cuando Zephyr
compila `kernel/`, `arch/arm/` y la generación de offsets (`arch/arm/core/offsets/`), usa
su propio `core_cm4.h` y valida varias macros: `__NVIC_PRIO_BITS`, `__MPU_PRESENT`,
`__FPU_PRESENT`.

Si el directorio `include/` de LPCOpen se agrega **globalmente** (vía
`zephyr_include_directories()`), el `core_cm4.h` de LPCOpen tapa la versión de Zephyr
para TODAS las unidades de compilación — incluido el kernel. Esto produce errores como:

```
error: 'IPSR_ISR_Msk' undeclared
error: 'SCB' undeclared
error: '__set_BASEPRI_MAX' undeclared
```

**La solución:** usar `zephyr_library_include_directories()` para limitar el include path
de LPCOpen únicamente a la librería HAL. Esta es la regla más importante de todas.

### `cmake/CMakeLists.txt`

```cmake
# Copyright (c) 2026 Universidad
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
  )
  zephyr_library_sources(
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/cgu_43xx.c
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/scu_43xx.c
    ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/sysinit_43xx.c
  )
  # Nombres de archivo ilustrativos siguiendo la convención de LPCOpen para
  # LPC43xx (CGU = reloj, SCU = pines); confirmar los nombres exactos contra
  # el paquete LPCOpen que se use. [verificar]

  # Agregar más fuentes de LPCOpen acá a medida que se implementan drivers:
  # zephyr_library_sources_ifdef(CONFIG_UART_LPC43XX
  #   ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/uart_43xx.c
  # )

  if(CONFIG_PINCTRL)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../drivers/pinctrl
                     ${CMAKE_CURRENT_BINARY_DIR}/drivers/pinctrl)
  endif()

  if(CONFIG_GPIO)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../drivers/gpio
                     ${CMAKE_CURRENT_BINARY_DIR}/drivers/gpio)
  endif()

endif()
```

**Referencia de funciones CMake:**

| Función | Alcance |
|---|---|
| `zephyr_include_directories()` | Todos los targets del build |
| `zephyr_compile_definitions()` | Todos los targets del build |
| `zephyr_library_named(nombre)` | Crea una librería nueva y la deja como actual |
| `zephyr_library_include_directories()` | Solo la librería actual |
| `zephyr_library_sources()` | Solo la librería actual |
| `zephyr_library_sources_ifdef(cfg ...)` | Solo la librería actual, si la opción de Kconfig está activa |

---

## 3. Capa de SoC

Todos los archivos de SoC viven bajo `soc/nxp/lpc43xx/`. El nivel `nxp/` es el fabricante
y `lpc43xx/` es la familia de SoC. Zephyr descubre esta ruta porque `soc_root: .` está
seteado en `module.yml`.

### `soc/nxp/lpc43xx/soc.yml`

Declara la jerarquía del SoC. Zephyr lo lee para enumerar todos los SoC de la familia.

```yaml
family:
  - name: lpc43xx
    series:
      - name: lpc433x
        socs:
          - name: lpc4337
```

### Archivos Kconfig — tres responsabilidades separadas

Zephyr incluye tres archivos Kconfig distintos para un SoC. Cada uno tiene un rol
específico:

**`soc/nxp/lpc43xx/Kconfig.soc`** — Símbolos estructurales y valores por defecto de
cadenas. Incluido por el árbol Kconfig principal de Zephyr y por sysbuild. No debe
seleccionar símbolos de capacidad de hardware (esos van en `Kconfig`):

```kconfig
# Copyright (c) 2026 Universidad
# SPDX-License-Identifier: Apache-2.0

config SOC_FAMILY_LPC43XX
	bool

config SOC_SERIES_LPC433X
	bool
	select SOC_FAMILY_LPC43XX

config SOC_LPC4337
	bool
	select SOC_SERIES_LPC433X
	select SOC_EARLY_INIT_HOOK

config SOC_FAMILY
	default "lpc43xx" if SOC_FAMILY_LPC43XX

config SOC_SERIES
	default "lpc433x" if SOC_SERIES_LPC433X

config SOC
	default "lpc4337" if SOC_LPC4337
```

`SOC_EARLY_INIT_HOOK` — le dice a Zephyr que llame a `soc_early_init_hook()` desde
`soc.c` antes de arrancar el kernel. Ahí va la inicialización del reloj.

**`soc/nxp/lpc43xx/Kconfig`** — Capacidades de hardware. Incluido en el árbol Kconfig
principal. Acá van `select ARM`, `select CPU_CORTEX_M4`, etc.:

```kconfig
# Copyright (c) 2026 Universidad
# SPDX-License-Identifier: Apache-2.0

config SOC_FAMILY_LPC43XX
	select ARM
	select CLOCK_CONTROL

config SOC_LPC4337
	select CPU_CORTEX_M4
	select CPU_HAS_ARM_MPU
	select CPU_HAS_FPU
	select CPU_CORTEX_M_HAS_SYSTICK
```

`CPU_HAS_FPU` — el LPC4337 tiene FPU de precisión simple en el núcleo M4 (el header
CMSIS oficial define `__FPU_PRESENT 1` para `CORE_M4`). `CPU_HAS_ARM_MPU` — el núcleo M4
tiene MPU (el coprocesador M0 no la tiene; irrelevante acá porque esta sección cubre solo
el M4).

**`soc/nxp/lpc43xx/Kconfig.defconfig`** — Valores numéricos por defecto para símbolos ya
existentes de Zephyr. Nunca definir símbolos nuevos acá:

```kconfig
# Copyright (c) 2026 Universidad
# SPDX-License-Identifier: Apache-2.0

if SOC_LPC4337

config NUM_IRQS
	default 53

config SYS_CLOCK_HW_CYCLES_PER_SEC
	default 204000000

endif # SOC_LPC4337
```

`NUM_IRQS = 53` — la tabla de vectores del núcleo M4 del LPC43xx llega hasta `QEI_IRQn =
52` (IRQ0..IRQ52 inclusive, con algunas posiciones reservadas en el medio), según el
enum `IRQn_Type` del header CMSIS oficial para `CORE_M4`.
`SYS_CLOCK_HW_CYCLES_PER_SEC` — clock del CPU tras inicializar PLL1 (204 MHz, el máximo
soportado por el núcleo M4).

### `soc/nxp/lpc43xx/soc.h`

`soc.h` es visible globalmente — el kernel, los drivers y el código de arch de Zephyr lo
ven. Este archivo tiene un solo trabajo: hacer que la validación CMSIS de Zephyr pase,
incluyendo `cmsis_core_m_defaults.h`.

**Qué hace `cmsis_core_m_defaults.h`:**
1. Lee `arm,num-irq-priority-bits` del DTS → setea `__NVIC_PRIO_BITS`
2. Lee `CONFIG_CPU_HAS_ARM_MPU` → setea `__MPU_PRESENT`
3. Lee `CONFIG_CPU_HAS_FPU` → setea `__FPU_PRESENT`
4. Incluye el `core_cm4.h` propio de Zephyr — provee `SCB`, `IRQn_Type`,
   `__set_BASEPRI_MAX`, `IPSR_ISR_Msk`, etc.

**No incluir `chip.h` ni `cmsis.h` de LPCOpen acá.** Esos headers también definen
`IRQn_Type` (desde su propio `cmsis_43xx.h`) — incluir ambos causa un error de
redeclaración para cada enumerador de `IRQn_Type`.

```c
/*
 * Copyright (c) 2026 Universidad
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
```

### `soc/nxp/lpc43xx/soc.c`

Implementa `soc_early_init_hook()`, llamado por Zephyr antes de arrancar el kernel.

**Por qué acceso directo a registros en vez de las APIs de reloj de LPCOpen:**
`soc.c` se compila como parte de la librería del kernel de Zephyr, que ve `soc.h` y por
lo tanto el `IRQn_Type` de Zephyr. Incluir `chip.h` desde `soc.c` volvería a traer el
`cmsis_43xx.h` de LPCOpen → una segunda definición de `IRQn_Type` → error de compilación.
El patrón estándar de Zephyr es que `soc.c` use acceso directo a registros; los drivers
compilados como librerías separadas pueden incluir `chip.h` sin problema porque su
include path está aislado con `zephyr_library_include_directories()`.

**Arquitectura de reloj del LPC43xx (CGU):** a diferencia del LPC17xx, que tiene un único
PLL0 simple, el LPC43xx usa una CGU (Clock Generation Unit) con varios PLL (PLL1 para el
reloj principal del sistema, PLL0USB y PLL0AUDIO para periféricos). La secuencia de abajo
habilita el cristal externo, configura PLL1 en modo directo (`DIRECT=1`, sin post-divisor,
válido porque 204 MHz cae dentro del rango de CCO permitido) para multiplicar 12 MHz × 17
= 204 MHz, y finalmente conmuta `BASE_M4_CLK` para que el núcleo M4 tome su reloj de
PLL1.

```c
/*
 * Copyright (c) 2026 Universidad
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
```

> **[verificar]** Los valores de `MSEL`/`NSEL`/`PSEL` y la secuencia exacta de arranque
> del PLL1 están construidos a partir de los campos de registro confirmados en UM10503
> (tabla 134), pero no fueron validados contra un ejemplo de referencia probado en
> hardware real. Confirmar contra el manual antes de dar por buena la secuencia en una
> placa física. También existe en el LPC43xx (igual que en el LPC17xx) un mecanismo de
> "shadow"/alias de memoria en `0x00000000` controlado por un registro del bloque CREG
> (análogo al `MEMMAP` del LPC17xx); su offset exacto no se confirmó contra la fuente
> primaria y se omite acá — revisar el capítulo de CREG en UM10503 antes del bring-up
> final.

### `soc/nxp/lpc43xx/CMakeLists.txt`

```cmake
# Copyright (c) 2026 Universidad
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(soc.c)
zephyr_include_directories(.)

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld CACHE INTERNAL "")
```

`zephyr_include_directories(.)` — hace visibles globalmente `soc.h` y `pinctrl_soc.h`
(los necesitan tanto el kernel como el código de arch).

### `soc/nxp/lpc43xx/linker.ld`

Delega en la plantilla de linker ARM estándar de Zephyr vía `REGION_ALIAS`.

```ld
/*
 * Copyright (c) 2026 Universidad
 * SPDX-License-Identifier: Apache-2.0
 */

/* CMSIS SystemInit setea VTOR a &__Vectors — alias a la tabla de vectores de Zephyr. */
__Vectors = _vector_table;

#include <zephyr/arch/arm/cortex_m/scripts/linker.ld>
```

Mapa de memoria del LPC4337 (UM10503, tablas 10 y 11, fila correspondiente a la serie
LPC433x):

| Región | Dirección | Tamaño |
|---|---|---|
| Flash banco A | `0x1A000000` | 256 KB |
| Flash banco B | `0x1B000000` | 256 KB |
| SRAM local banco 1 | `0x10000000` | 32 KB |
| SRAM local banco 2 | `0x10080000` | 40 KB |
| SRAM AHB banco 1 | `0x20000000` | 32 KB |
| SRAM AHB banco 2 | `0x20008000` | 16 KB |
| SRAM AHB (compartida con ETB) | `0x2000C000` | 16 KB |

El LPC4337 **no** tiene 1 MB de flash — ese tamaño corresponde a la serie LPC43x7 (p.ej.
LPC4357). El LPC4337 tiene 512 KB totales, repartidos en dos bancos de 256 KB.

El nodo DTS `sram0` mapea al banco 1 de SRAM local (`0x10000000`, 32 KB), usado para
código/datos del núcleo M4. El `chosen` `zephyr,sram` apunta ahí. El nodo `flash0` mapea
al banco de flash A (`0x1A000000`, 256 KB), que es el que ejecuta el M4 en este port de
un solo núcleo. El banco de flash B y el segundo banco de SRAM local se usan en el
escenario AMP (ver [sección 10](#10-porting-de-un-soc-con-múltiples-procesadores-amp)).

---

## 4. Device tree — capa de SoC

### `dts/arm/nxp/nxp_lpc4337.dtsi`

El `.dtsi` del SoC declara todos los periféricos con sus direcciones de hardware y
números de IRQ. Las placas incluyen este archivo y habilitan periféricos selectivamente.
Todos los valores provienen de UM10503.

```dts
/*
 * Copyright (c) 2026 Universidad
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arm/armv7-m.dtsi>
#include <zephyr/dt-bindings/gpio/gpio.h>

/ {
	cpus {
		#address-cells = <1>;
		#size-cells = <0>;

		cpu0: cpu@0 {
			device_type = "cpu";
			compatible = "arm,cortex-m4f";
			reg = <0>;
		};
	};

	flash0: flash@1a000000 {
		compatible = "soc-nv-flash";
		reg = <0x1a000000 0x40000>;   /* Banco A, 256 KB */
	};

	sram0: memory@10000000 {
		compatible = "mmio-sram";
		reg = <0x10000000 0x8000>;    /* 32 KB SRAM local, banco 1 */
	};

	sram1: memory@10080000 {
		compatible = "mmio-sram";
		reg = <0x10080000 0xa000>;    /* 40 KB SRAM local, banco 2 */
	};

	sram2: memory@20000000 {
		compatible = "mmio-sram";
		reg = <0x20000000 0x8000>;    /* 32 KB SRAM AHB, banco 1 */
	};

	sram3: memory@20008000 {
		compatible = "mmio-sram";
		reg = <0x20008000 0x4000>;    /* 16 KB SRAM AHB, banco 2 */
	};

	soc {
		pinctrl: pinctrl@40086000 {
			compatible = "nxp,lpc43xx-pinctrl";
			reg = <0x40086000 0x800>;
		};

		uart0: uart@40081000 {
			compatible = "nxp,lpc43xx-uart";
			reg = <0x40081000 0x1000>;   /* USART0 */
			interrupts = <24 0>;
			status = "disabled";
		};

		uart1: uart@40082000 {
			compatible = "nxp,lpc43xx-uart";
			reg = <0x40082000 0x1000>;   /* UART1 */
			interrupts = <25 0>;
			status = "disabled";
		};

		gpio: gpio@400f6000 {
			compatible = "nxp,lpc43xx-gpio";
			reg = <0x400f6000 0x400>;
			#address-cells = <1>;
			#size-cells = <0>;

			gpio0: port@0 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <0>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};

			gpio1: port@1 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <1>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};

			gpio2: port@2 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <2>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};

			gpio3: port@3 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <3>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};

			gpio4: port@4 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <4>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};

			gpio5: port@5 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <5>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};

			gpio6: port@6 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <6>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};

			gpio7: port@7 {
				compatible = "nxp,lpc43xx-gpio-port";
				reg = <7>;
				gpio-controller;
				#gpio-cells = <2>;
				status = "okay";
			};
		};
	};
};

/* El núcleo Cortex-M4 del LPC43xx tiene 3 bits de prioridad NVIC (8 niveles),
 * según el header CMSIS oficial para CORE_M4. */
&nvic {
	arm,num-irq-priority-bits = <3>;
};
```

`arm,num-irq-priority-bits = <3>` — lo lee `cmsis_core_m_defaults.h` para setear
`__NVIC_PRIO_BITS = 3`, que Zephyr valida luego contra `NUM_IRQS`.

**Sobre el nodo `gpio`:** a diferencia del LPC17xx, donde cada puerto GPIO es un
periférico separado con su propia dirección base, el LPC43xx tiene un único periférico
`GPIO_PORT` con registros organizados como arreglos (`DIR[8]`, `SET[8]`, `CLR[8]`, etc.),
uno por puerto. Por eso el nodo `gpio` mapea el bloque de registros completo, y cada
puerto (`gpio0`..`gpio7`) es un nodo hijo cuyo `reg` es simplemente el índice de puerto
(0-7) dentro de esos arreglos, no una dirección de memoria — de ahí `#size-cells = <0>`
en el nodo padre. Ver la [sección 7](#7-driver-de-gpio) para el detalle del driver.

### Bindings de DTS

Los bindings viven en `dts/bindings/<subsistema>/`. Zephyr los encuentra porque
`dts_root` está seteado en `module.yml`.

**`dts/bindings/gpio/nxp,lpc43xx-gpio.yaml`** — el nodo padre, solo mapea el bloque de
registros compartido:

```yaml
description: NXP LPC43xx GPIO_PORT (bloque de registros compartido por los 8 puertos)

compatible: "nxp,lpc43xx-gpio"

include: base.yaml

properties:
  reg:
    required: true
```

**`dts/bindings/gpio/nxp,lpc43xx-gpio-port.yaml`** — cada puerto individual:

```yaml
description: Un puerto GPIO del LPC43xx (índice dentro de GPIO_PORT)

compatible: "nxp,lpc43xx-gpio-port"

include:
  - name: gpio-controller.yaml
  - name: base.yaml

properties:
  reg:
    required: true
    description: índice de puerto dentro de los arreglos de GPIO_PORT (0-7)
  "#gpio-cells":
    const: 2
  ngpios:
    type: int
    default: 32

gpio-cells:
  - pin
  - flags
```

`gpio-cells` debe listar los nombres de celda en el mismo orden en que aparecen en el
DTS `gpios = <&gpio0 pin flags>`. El binding extiende `gpio-controller.yaml` de Zephyr,
que agrega la validación estándar de la propiedad `gpio-controller`.

**`dts/bindings/pinctrl/nxp,lpc43xx-pinctrl.yaml`**

```yaml
description: NXP LPC43xx System Control Unit (SCU) — multiplexado de pines

compatible: "nxp,lpc43xx-pinctrl"

include: base.yaml

child-binding:
  description: Grupo de pines del LPC43xx
  child-binding:
    description: Nodo de configuración de un pin del LPC43xx.
    properties:
      pinmux:
        required: true
        type: array
        description: |
          Arreglo de valores de configuración de pines codificados con
          LPC43XX_PIN().
```

La estructura de dos niveles `child-binding` refleja la organización de los nodos
pinctrl de Zephyr:
```
&pinctrl {
    uart0_default: uart0_default {     ← nodo de estado (hijo)
        group1 {                        ← nodo de grupo (hijo del hijo)
            pinmux = < ... >;
        };
    };
};
```

---

## 5. Capa de placa

### `boards/nxp/lpc4337/board.yml`

```yaml
board:
  name: lpc4337
  full_name: NXP LPC4337
  vendor: nxp
  socs:
    - name: lpc4337
```

`name` debe coincidir con el nombre del directorio y con el prefijo de todos los archivos
de la placa. `vendor` debe coincidir con el nombre del directorio bajo `boards/`.

### Archivos Kconfig de la placa

**`boards/nxp/lpc4337/Kconfig.lpc4337`** — nombrado según la placa, no `Kconfig.board`:

```kconfig
config BOARD_LPC4337
	select SOC_LPC4337
```

**`boards/nxp/lpc4337/Kconfig.defconfig`** — valores por defecto a nivel placa:

```kconfig
if BOARD_LPC4337

# UART_CONSOLE se reactiva cuando el driver nxp,lpc43xx-uart esté implementado

endif # BOARD_LPC4337
```

### `boards/nxp/lpc4337/lpc4337_lpc4337_defconfig`

Patrón de nombre: `<placa>_<soc>_defconfig`. Para una placa de un solo SoC, el
calificador de SoC coincide con el nombre de la placa:

```
CONFIG_SERIAL=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
CONFIG_BUILD_OUTPUT_HEX=y
CONFIG_PINCTRL=y
CONFIG_GPIO=y
```

`SERIAL=n` y `UART_CONSOLE=n` — deshabilitados hasta implementar el driver de UART.
`BUILD_OUTPUT_HEX=y` — genera `zephyr.hex` para flashear.

### `boards/nxp/lpc4337/lpc4337_lpc4337.dts`

```dts
/*
 * Copyright (c) 2026 Universidad
 * SPDX-License-Identifier: Apache-2.0
 */

/dts-v1/;
#include <nxp/nxp_lpc4337.dtsi>
#include <dt-bindings/pinctrl/lpc43xx-pinctrl.h>

/ {
	model = "NXP LPC4337";
	compatible = "nxp,lpc4337";

	chosen {
		zephyr,sram  = &sram0;
		zephyr,flash = &flash0;
	};

	leds {
		compatible = "gpio-leds";
		led0: led_0 {
			/* Ajustar puerto/pin según el LED de la placa de evaluación
			 * específica que se use — este valor es solo un ejemplo. */
			gpios = <&gpio5 5 GPIO_ACTIVE_LOW>;
			label = "LED 0";
		};
	};

	aliases {
		led0 = &led0;
	};
};

&pinctrl {
	uart0_default: uart0_default {
		group1 {
			/* P2_0/P2_1 son los pines de ISP por UART del boot ROM;
			 * el número de función exacto para USART0 TXD/RXD debe
			 * confirmarse contra la tabla de multiplexado de pines
			 * del paquete elegido. [verificar] */
			pinmux = <LPC43XX_PIN_P2_0_UART0_TXD>,
				 <LPC43XX_PIN_P2_1_UART0_RXD>;
		};
	};
};

/* uart0 se mantiene deshabilitado hasta implementar el driver nxp,lpc43xx-uart */
```

---

## 6. Driver de pinctrl

Pinctrl es el primer driver a implementar porque todos los demás periféricos dependen de
él para configurar sus pines antes de usarlos. Debe registrarse antes de que corra el
`init` de cualquier otro driver.

### Codificación de pines

**`include/dt-bindings/pinctrl/lpc43xx-pinctrl.h`**

Este header lo incluyen tanto los archivos DTS (vía `#include`) como el código del driver
en C. Codifica una configuración de pin como una sola palabra de 32 bits:

```
bits [4:0]   número de pin (0–31)
bits [8:5]   número de puerto SCU (0–15: P0-P9 = 0-9, PA-PF = 10-15)
bits [11:9]  función (0–7, campo MODE de 3 bits del registro SFSPx_y)
bits [13:12] modo de pull (0=pull-up, 1=repeater, 2=ninguno, 3=pull-down)
bit  [14]    habilitar buffer de entrada (EZI; requerido para usar el pin como entrada)
```

El LPC43xx no usa PINSEL/PINMODE como el LPC17xx: usa la SCU (System Control Unit), con
un registro `SFSPx_y` por cada pin (`x` = puerto, `y` = número de pin dentro del puerto).

```c
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

/* Pines con nombre pre-codificados.
 * El número de función (FUNC) es un valor de ejemplo — confirmar contra la
 * tabla de multiplexado de pines (SCU) del paquete específico. [verificar] */
#define LPC43XX_PIN_P2_0_UART0_TXD  LPC43XX_PIN(2, 0, 1, LPC43XX_PULL_UP, 0)
#define LPC43XX_PIN_P2_1_UART0_RXD  LPC43XX_PIN(2, 1, 1, LPC43XX_PULL_UP, 1)
/* ... agregar más según se necesiten ... */
```

### `soc/nxp/lpc43xx/pinctrl_soc.h`

El subsistema pinctrl de Zephyr requiere un `pinctrl_soc.h` en el directorio del SoC.
Define `pinctrl_soc_pin_t` y las macros que expanden los arreglos `pinmux` del DTS en
listas de inicialización en C:

```c
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
```

### `drivers/pinctrl/pinctrl_nxp_lpc43xx.c`

Los registros `SFSPx_y` de la SCU del LPC43xx (base `0x40086000`) están organizados con
un espaciado fijo de `0x80` bytes por puerto y `0x4` bytes por pin dentro del puerto —
fórmula derivada directamente de las direcciones confirmadas en el header CMSIS
(`SFSPD_0 @ 0x40086680`, `SFSPD_16 @ 0x400866C0`, con la SCU en `0x40086000`, lo que da
puerto `D` = índice 13, es decir `0x40086000 + 13*0x80 = 0x40086680`).

Layout del registro `SFSPx_y` (UM10503 tabla 190):

| Bits | Nombre | Significado |
|---|---|---|
| `[2:0]` | `MODE` | función alternativa del pin (0-7) |
| `[3]` | `EPD` | habilitar pull-down |
| `[4]` | `EPUN` | deshabilitar pull-up (activo = deshabilitado) |
| `[6]` | `EZI` | habilitar buffer de entrada (debe estar en 1 para usar el pin como entrada) |
| `[7]` | `ZIF` | filtro de glitch de entrada (0=habilitado, default) |

```c
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
```

**`drivers/pinctrl/CMakeLists.txt`:**

```cmake
zephyr_library()
zephyr_library_sources(pinctrl_nxp_lpc43xx.c)
```

---

## 7. Driver de GPIO

### `dts/bindings/gpio/nxp,lpc43xx-gpio-port.yaml`

Ver sección 4. El `compatible` del binding debe coincidir exactamente con lo que aparece
en `#define DT_DRV_COMPAT` del driver.

### `drivers/gpio/gpio_nxp_lpc43xx.c`

El periférico `GPIO_PORT` del LPC43xx (base `0x400F4000`) organiza sus registros de
control como arreglos de 8 palabras (uno por puerto), no como bloques separados por
puerto como en el LPC17xx. Los arreglos relevantes están en el offset `0x2000` de la
base del periférico, espaciados `0x80` bytes entre sí (confirmado directamente contra el
header CMSIS):

| Offset desde `0x400F4000` | Registro | Rol |
|---|---|---|
| `0x2000` | `DIR[8]` | Dirección (1=salida) |
| `0x2080` | `MASK[8]` | Máscara de pines |
| `0x2100` | `PIN[8]` | Valor actual de los pines |
| `0x2180` | `MPIN[8]` | Valor de pines enmascarado |
| `0x2200` | `SET[8]` | Escribir 1 pone en alto |
| `0x2280` | `CLR[8]` | Escribir 1 pone en bajo |
| `0x2300` | `NOT[8]` | Escribir 1 invierte (toggle) |

```c
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

#define GPIO_LPC43XX_INIT(n)                                            \
	static struct gpio_lpc43xx_data gpio_lpc43xx_data_##n;          \
	static const struct gpio_lpc43xx_config gpio_lpc43xx_cfg_##n = {\
		.common = {                                              \
			.port_pin_mask =                                 \
				GPIO_PORT_PIN_MASK_FROM_DT_INST(n),      \
		},                                                       \
		.port = DT_INST_REG_ADDR(n),                             \
	};                                                               \
	DEVICE_DT_INST_DEFINE(n,                                         \
		gpio_lpc43xx_init, NULL,                                 \
		&gpio_lpc43xx_data_##n, &gpio_lpc43xx_cfg_##n,          \
		PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,                 \
		&gpio_lpc43xx_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_LPC43XX_INIT)
```

`DT_DRV_COMPAT nxp_lpc43xx_gpio_port` — los guiones bajos reemplazan comas y guiones en
el string `compatible`. Las macros `DT_INST_*` de Zephyr lo usan para iterar sobre todos
los nodos habilitados con ese `compatible`.

`DT_INST_REG_ADDR(n)` — acá `reg` no es una dirección de memoria sino el índice de puerto
(0-7) definido en el nodo hijo del DTS (ver sección 4), gracias a `#size-cells = <0>` en
el nodo padre `gpio`.

`GPIO_PORT_PIN_MASK_FROM_DT_INST(n)` — lee `ngpios` del binding para construir una
máscara de bits de los pines válidos.

**`drivers/gpio/CMakeLists.txt`:**

```cmake
zephyr_library()
zephyr_library_sources(gpio_nxp_lpc43xx.c)
```

---

## 8. Flasheo

### Build

```sh
west build -b lpc4337 app/
# Salida: build/zephyr/zephyr.hex (y .bin, .elf)
```

### Configuración de OpenOCD

pyOCD no tiene un target integrado para LPC4337 (el target más cercano en su lista
`builtin` es `LPC4330`, que es una variante sin flash interna con boot por SPIFI, no
aplicable acá) — **OpenOCD es la vía de flasheo realista para este chip**, no
pyOCD/linkserver.

OpenOCD no trae un `target/lpc4337.cfg` de fábrica, pero sí trae `target/lpc4357.cfg`
(mismo silicio, mismos TAP ID de M4/M0, solo cambia el tamaño de los bancos de flash), que
sirve como plantilla directa.

**`boards/nxp/lpc4337/support/lpc4337.cfg`** — basado en `lpc4357.cfg`, ajustado al
tamaño real de banco del LPC4337 (256 KB en vez de 512 KB):

```tcl
source [find target/swj-dp.tcl]

adapter speed 500

set _CHIPNAME lpc4337

if { [using_jtag] } {
    set _CPUTAPID 0x4ba00477
} else {
    set _CPUTAPID 0x2ba01477
}

swj_newdap $_CHIPNAME cpu -irlen 4 -expected-id $_CPUTAPID
dap create $_CHIPNAME.dap -chain-position $_CHIPNAME.cpu

target create $_CHIPNAME.m4 cortex_m -dap $_CHIPNAME.dap

$_CHIPNAME.m4 configure -work-area-phys 0x10000000 -work-area-size 0x4000

# 512 KB de flash interna repartidos en dos bancos de 256 KB (banco A y B).
# El driver "lpc2000" con variante "lpc4300" es el soporte de OpenOCD para
# toda la familia LPC43x2/3/5/7 (incluye LPC4337). El parámetro de clock
# (en kHz) es el que usa IAP internamente durante la programación.
flash bank $_CHIPNAME.flasha lpc2000 0x1a000000 0x40000 0 0 $_CHIPNAME.m4 \
    lpc4300 204000 calc_checksum
flash bank $_CHIPNAME.flashb lpc2000 0x1b000000 0x40000 0 0 $_CHIPNAME.m4 \
    lpc4300 204000 calc_checksum

# connect_assert_srst: fuerza nRESET en bajo antes de cualquier acceso
# SWD/DP para que la CPU nunca esté ejecutando durante la enumeración del AP
# o la programación de flash.
# Requiere el pin nRESET cableado desde el pin 10 del probe hasta el pin de
# RESET de la placa.
reset_config srst_only srst_nogate connect_assert_srst
adapter srst delay 100
cortex_m reset_config sysresetreq
```

**`boards/nxp/lpc4337/support/openocd.cfg`:**

```tcl
source [find interface/cmsis-dap.cfg]
adapter usb vid_pid 0x1fc9 0x0090   # NXP LPC-Link2
transport select swd
source [find lpc4337.cfg]
```

> **[verificar]** El VID/PID `0x1fc9:0x0090` corresponde al LPC-Link2, el probe habitual
> en placas de evaluación NXP/Hitex/Embedded Artists para LPC43xx. Si se usa un probe
> MCU-Link, el par es `0x1fc9:0x0143` (mismo VID/PID que ya se usaba en el port de
> LPC17xx) — este segundo valor está confirmado con menor certeza que el del LPC-Link2 y
> conviene verificarlo con `lsusb`/`system_profiler` antes de asumirlo.

**`boards/nxp/lpc4337/board.cmake`:**

```cmake
board_runner_args(openocd
  "--config=${BOARD_DIR}/support/openocd.cfg"
)

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
```

No se incluye un runner `linkserver`/pyocd porque, como se explicó arriba, no hay soporte
de fábrica para este chip en esas herramientas.

### Comando de flasheo

```sh
west flash --runner openocd
```

### Protección de lectura de código (CRP)

Igual que en el LPC17xx, el offset de CRP es `0x2FC` desde el inicio del banco de flash
en uso (UM10503 §6.6, tabla 31). Valores mágicos:

| Valor | Efecto |
|---|---|
| `0x12345678` (CRP1) | Deshabilita JTAG/SWD; permite actualización parcial de flash vía ISP |
| `0x87654321` (CRP2) | Como CRP1, pero solo permite borrado completo de chip vía ISP |
| `0x43218765` (CRP3) | Como CRP2, y además deshabilita la entrada a ISP por el pin P2_7 si hay código de usuario válido (bloqueo total) |
| `0x4E697370` (NO_ISP) | Deshabilita solo el pedido de ISP por P2_7 (sin protección de lectura; JTAG/SWD siguen funcionando) |

El setup de linker actual no protege explícitamente esta dirección — agregar una sección
si se necesita.

### Recuperación por ISP UART (evita SWD por completo)

El boot ROM del LPC43xx tiene un modo ISP por UART que funciona sin importar el estado de
SWD o si hay CRP1/CRP2 activo.

**A diferencia del LPC17xx (que usaba P2.10 como pin de ISP), el LPC4337 usa P2_7,
activo en bajo.** Además, el boot ROM también samplea los pines P2_9, P2_8, P1_2 y P1_1
para elegir la fuente de boot cuando P2_7 está en bajo; con esos cuatro pines también en
bajo, el chip entra en modo ISP por USART0 (pines P2_0/P2_1). En la mayoría de las placas
de evaluación estos pines de selección ya están cableados/con pull-ups fijos para
seleccionar UART0 por defecto — revisar el esquemático de la placa específica.

1. Cablear un adaptador USB-UART: **P2_0 → RX del adaptador**, **P2_1 → TX del
   adaptador**, **GND → GND**
2. Mantener **P2_7** (pin de ISP) en bajo
3. Presionar y soltar RESET
4. Soltar P2_7 — el chip corre el bootloader ISP sobre USART0
5. Flashear o borrar con una herramienta compatible con LPC43xx, por ejemplo
   [Flash Magic](https://www.flashmagictool.com) (herramienta gráfica gratuita de NXP) o
   `lpc21isp` **[verificar compatibilidad con LPC43xx de la versión usada]**.

Tras un borrado ISP exitoso, SWD debería volver a funcionar normalmente en el siguiente
ciclo de energía si la causa era un firmware que colgaba el AHB, o CRP1/CRP2.

---

## 9. Agregar un driver de periférico nuevo

Esta sección describe el patrón completo para agregar un driver nuevo, usando UART como
ejemplo.

### Paso 1 — Binding de DTS

Crear `dts/bindings/serial/nxp,lpc43xx-uart.yaml`:

```yaml
description: NXP LPC43xx UART

compatible: "nxp,lpc43xx-uart"

include: [uart-controller.yaml, base.yaml]

properties:
  reg:
    required: true
  interrupts:
    required: true
  current-speed:
    required: true
  pinctrl-0:
    required: true
  pinctrl-names:
    required: true
```

### Paso 2 — Habilitar en el DTS

En el `.dts` de la placa, agregar la configuración de pines y habilitar la UART:

```dts
&uart0 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart0_default>;
    pinctrl-names = "default";
};
```

### Paso 3 — Agregar la fuente de LPCOpen a la librería HAL

En `cmake/CMakeLists.txt`, agregar el archivo fuente condicionalmente:

```cmake
zephyr_library_sources_ifdef(CONFIG_UART_LPC43XX
  ${CMAKE_CURRENT_LIST_DIR}/../lpc_chip_43xx/src/uart_43xx.c
)
```

### Paso 4 — Escribir el driver de Zephyr

Crear `drivers/serial/uart_nxp_lpc43xx.c`. El driver puede incluir headers de LPCOpen
porque se compila como librería separada, con `zephyr_library_include_directories()`
limitando el include path de LPCOpen:

```c
#define DT_DRV_COMPAT nxp_lpc43xx_uart

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>
/* chip.h es seguro acá porque esta es una librería separada */
#include "chip.h"

/* ... implementar uart_driver_api ... */
```

### Paso 5 — Registrar vía Kconfig

En `soc/nxp/lpc43xx/Kconfig` (o un `drivers/serial/Kconfig` nuevo):

```kconfig
config UART_LPC43XX
	bool "Driver de UART NXP LPC43xx"
	default y
	depends on DT_HAS_NXP_LPC43XX_UART_ENABLED
	select SERIAL_HAS_DRIVER
```

### Paso 6 — Habilitar en defconfig

En `boards/nxp/lpc4337/lpc4337_lpc4337_defconfig`:

```
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

Y en `boards/nxp/lpc4337/Kconfig.defconfig`:

```kconfig
if BOARD_LPC4337

config UART_CONSOLE
	default y

endif
```

---

## 10. Porting de un SoC con múltiples procesadores (AMP)

El LPC4337 es un caso real, no hipotético, de este escenario: tiene un núcleo Cortex-M4
principal y un coprocesador Cortex-M0 (`M0APP` en la tabla de IRQ del M4, IRQ 1). Zephyr
v4.x soporta Asymmetric Multi-Processing (AMP) vía builds multi-imagen.

### `soc.yml` multi-núcleo para el LPC4337

```yaml
family:
  - name: lpc43xx
    series:
      - name: lpc433x
        socs:
          - name: lpc4337
            cpuclusters:
              - name: m4
                cpus:
                  - name: cpu0
              - name: m0app
                cpus:
                  - name: cpu0
```

Cada `cpucluster` genera una imagen de build separada. Un `board.yml` mapea los clusters
a targets de placa:

```yaml
board:
  name: lpc4337
  vendor: nxp
  socs:
    - name: lpc4337
      cpucluster: m4      # imagen por defecto
    - name: lpc4337
      cpucluster: m0app
```

### DTS de placa multi-núcleo

Un archivo `.dts` separado por cluster:

```
boards/nxp/lpc4337/
  lpc4337_lpc4337_m4.dts
  lpc4337_lpc4337_m0app.dts
```

Ambos incluyen el mismo `.dtsi` de SoC pero pueden habilitar periféricos distintos y
apuntar `zephyr,sram` a regiones de RAM distintas.

### Regiones de linker por núcleo

En el LPC4337, el núcleo M4 típicamente ejecuta desde el banco de flash A
(`0x1A000000`, 256 KB) y usa el banco 1 de SRAM local (`0x10000000`, 32 KB). El
coprocesador M0 no tiene una ruta de ejecución directa (XIP) tan directa como el M4 en el
flujo de arranque típico: el patrón habitual es que la imagen del M0 se linkee en una
sección propia dentro del flash, y que el M4 la copie a RAM (banco 2 de SRAM local,
`0x10080000`, 40 KB) antes de sacar al M0 de reset:

```ld
/* Linker del núcleo M4 */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x1A000000, LENGTH = 256K  /* banco A */
    SRAM  (rwx) : ORIGIN = 0x10000000, LENGTH = 32K
}
```

```ld
/* Linker del núcleo M0APP — la imagen se enlaza para ejecutar desde RAM */
MEMORY {
    SRAM  (rwx) : ORIGIN = 0x10080000, LENGTH = 40K
}
```

### IPC

A diferencia de otros SoC NXP con un periférico de mailbox dedicado, el LPC43xx no tiene
una IP de mailbox genérica de ARM para la comunicación M4↔M0. El mecanismo típico es
memoria compartida (en una de las regiones de SRAM AHB) combinada con las interrupciones
cruzadas entre núcleos (el M4 ve al M0 como la fuente de IRQ `M0APP` = 1; el M0 tiene su
propio mecanismo para señalizar al M4). Implementar esto como backend de `ipm`/`mbox` de
Zephyr requeriría un driver específico para LPC43xx.

> **[verificar]** El mecanismo exacto de señalización de interrupciones entre el M4 y el
> M0APP (registros involucrados, más allá de que el M4 observa el IRQ `M0APP`) no se
> confirmó contra UM10503 en esta pasada de investigación — revisar el capítulo de
> arranque/control del M0 antes de implementar el backend de IPC.

---

## Resumen de problemas críticos

| Problema | Síntoma | Solución |
|---|---|---|
| `zephyr_include_directories()` para la HAL del fabricante | `IPSR_ISR_Msk`, `SCB` indefinidos en offsets.c | Usar `zephyr_library_include_directories()` — limitado solo a la librería HAL |
| `chip.h` en `soc.h` | `chip.h: No such file or directory` al compilar el kernel | Sacarlo de `soc.h`; ahí solo va `cmsis_core_m_defaults.h` |
| `chip.h` en `soc.c` | `error: redeclaration of enumerator 'Reset_IRQn'` | Usar acceso directo a registros en `soc.c`; la HAL del fabricante solo en drivers |
| `soc.h` sin incluir `cmsis_core_m_defaults.h` | `#error "__MPU_PRESENT and CONFIG_CPU_HAS_ARM_MPU mismatch"` | Incluir siempre `cmsis_core_m_defaults.h` en `soc.h` |
| `SYS_CLOCK_HW_CYCLES_PER_SEC` faltante en Kconfig.defconfig | Resolución de timer incorrecta | Setear al clock de CPU tras PLL1 (204000000 para LPC4337 a 204 MHz) |
| `soc.yml` llamado `soc.yaml` | El SoC no se detecta | Zephyr espera `.yml`, no `.yaml` |
| Archivo Kconfig de placa llamado `Kconfig.board` | La placa no se encuentra | Nombrarlo `Kconfig.<nombre-de-placa>` |
| Un solo nodo GPIO por puerto con `reg` como dirección de memoria | Direcciones duplicadas/inválidas en el DTS | El LPC43xx comparte un único bloque de registros (`GPIO_PORT`) entre los 8 puertos; usar `reg` como índice de puerto (`#size-cells = <0>`), no como dirección |
| Asumir el pin de ISP del LPC17xx (P2.10) | La placa LPC4337 nunca entra en modo ISP | El LPC4337 usa **P2_7** activo en bajo, no P2_10 |
| nRESET no cableado al probe | Se cuelga el MEM-AP incluso durante SRST | Cablear el pin 10 del probe a RESET de la placa; revisar el pin 1 (VTref) |
