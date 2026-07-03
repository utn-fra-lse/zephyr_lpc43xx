# Guía de port de SoC fuera del árbol (out-of-tree) para Zephyr

**Objetivo de referencia:** placa **EDU-CIAA-NXP** (Proyecto CIAA — Computadora Industrial
Abierta Argentina, iniciativa conjunta de ACSE y CADIEEL), montada sobre el SoC NXP
LPC4337 — Cortex-M4F (204 MHz) + coprocesador Cortex-M0, 512 KB de Flash (2 bancos de
256 KB), 136 KB de SRAM total
**Versión de Zephyr:** v4.2.0
**Estado:** build y flasheo funcionando. GPIO, pinctrl y UART (polling, por USART2)
implementados y probados en hardware. Ver la sección [Driver de UART](#8-driver-de-uart)
para las lecciones de la puesta en marcha, la nota sobre el core M0 en la sección de
[Flasheo](#9-flasheo), y la [sección 11](#11-porting-de-un-soc-con-múltiples-procesadores-amp)
para lo que falta del M0.

Esta guía documenta, archivo por archivo, qué hace cada pieza del módulo y por qué existe
en ese lugar. No reproduce el contenido de los archivos — el código fuente real vive en
este mismo repositorio y es la referencia autoritativa; acá se explica el propósito, las
decisiones no obvias y los problemas esperables de cada uno.

> **Nota sobre el hardware:** el LPC4337 es un chip **dual-core** (Cortex-M4 principal +
> coprocesador Cortex-M0). Este port implementa hasta ahora solo el núcleo M4 (el caso
> típico y el que hay que hacer funcionar primero). La sección 11 trata el escenario AMP
> (Asymmetric Multi-Processing) con ambos núcleos — que es el caso real de este chip, no
> un ejemplo hipotético — y documenta qué falta para arrancar el M0 en el futuro.

> **Nota sobre el vendor:** en la jerarquía de directorios de Zephyr, `boards/<vendor>/`
> identifica a quien **diseña la placa**, no a quien fabrica el SoC que lleva montado. La
> EDU-CIAA-NXP la diseña el Proyecto CIAA, no NXP — por eso vive en `boards/ciaa/`, aunque
> el SoC siga viviendo en `soc/nxp/` (ahí sí corresponde NXP, que es quien fabrica el
> LPC4337).

Referencias oficiales:
- [Guía de porting de SoC](https://docs.zephyrproject.org/latest/hardware/porting/soc_porting.html)
- [Guía de porting de placas](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [Bindings de Device Tree](https://docs.zephyrproject.org/latest/build/dts/bindings.html)

Fuentes técnicas: UM10503 (LPC43xx/LPC43Sxx User Manual, NXP), el header CMSIS
`LPC43xx.h`, y para la placa: el firmware oficial del proyecto CIAA
(`github.com/ciaa/firmware_v2`, tabla de pines de la sAPI en `sapi_gpio.c`) y su script de
OpenOCD (`github.com/ciaa/firmware_v1`,
`.../openocd/cfg/cortexM4/lpc43xx/lpc4337/ciaa-nxp.cfg`). Donde un valor no pudo
confirmarse contra una fuente primaria se marca explícitamente como **[verificar]**.

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
│       ├── pinctrl/nxp,lpc43xx-pinctrl.yaml
│       └── serial/nxp,lpc43xx-uart.yaml
├── include/
│   └── dt-bindings/
│       └── pinctrl/
│           └── lpc43xx-pinctrl.h    # Macros de codificación de pines (compartido por DTS y C)
├── boards/
│   └── ciaa/                        # Vendor = quien diseña la placa (Proyecto CIAA),
│       └── edu_ciaa_nxp/            # no el fabricante del SoC (ver nota arriba)
│           ├── board.yml            # Capa de placa (Fase 5)
│           ├── board.cmake
│           ├── Kconfig.edu_ciaa_nxp
│           ├── Kconfig.defconfig
│           ├── edu_ciaa_nxp_lpc4337.dts
│           ├── edu_ciaa_nxp_lpc4337_defconfig
│           └── support/
│               └── openocd.cfg      # Debugger on-board (FT2232H), no un LPC-Link2
└── drivers/
    ├── pinctrl/                     # Driver de pinctrl (Fase 6)
    │   ├── CMakeLists.txt
    │   └── pinctrl_nxp_lpc43xx.c
    ├── gpio/                        # Driver de GPIO (Fase 7)
    │   ├── CMakeLists.txt
    │   └── gpio_nxp_lpc43xx.c
    └── serial/                      # Driver de UART (Fase 8)
        ├── CMakeLists.txt
        └── serial_nxp_lpc43xx.c
```

---

## 1. Registro del módulo

### `zephyr/module.yml`

Es el único archivo que el sistema de build de Zephyr busca al recorrer
`ZEPHYR_EXTRA_MODULES`. Declara las cuatro raíces que Zephyr necesita conocer:
`cmake/` (se le hace `add_subdirectory`), `Kconfig` (se incluye), `board_root: .` (Zephyr
busca `boards/` acá), `soc_root: .` (busca `soc/` acá) y `dts_root: .` (busca `dts/` acá,
en particular `dts/bindings/` — **ojo, esta ruta es fija**: si los bindings viven en
cualquier otro lado dentro del módulo, Zephyr no los encuentra y falla la validación de
devicetree sin un error obvio de "archivo no encontrado").

Para registrar el módulo con west: o bien agregarlo como proyecto en el `west.yml` del
workspace, o setear `ZEPHYR_EXTRA_MODULES=/ruta/a/este/repo` antes de `west build`.

### `Kconfig` (raíz del módulo)

Primer archivo que lee el árbol de Kconfig de Zephyr. Puede quedar vacío (solo con el
header de copyright) en ports simples — Zephyr descubre los Kconfig del SoC
automáticamente a través de `soc_root`.

---

## 2. Biblioteca HAL del fabricante

### `cmake/CMakeLists.txt`

Compila la librería `hal_lpc43xx` a partir de las fuentes de LPCOpen
(`lpc_chip_43xx/src/`) y registra los subdirectorios de drivers (`drivers/pinctrl`,
`drivers/gpio`, ...) gateados por sus respectivas opciones de Kconfig
(`CONFIG_PINCTRL`, `CONFIG_GPIO`).

La decisión arquitectónica más importante de todo el port está acá: **aislar los headers
CMSIS de la HAL del fabricante del build de Zephyr**. Zephyr trae su propio CMSIS-Core
(módulo west `cmsis_6`) y lo necesita para compilar el kernel, `arch/arm/` y la
generación de offsets. Si el include path de LPCOpen (`lpc_chip_43xx/include/`) se agrega
globalmente con `zephyr_include_directories()`, el `core_cm4.h` de LPCOpen tapa el de
Zephyr para **todas** las unidades de compilación — incluido el kernel — y aparecen
errores como `'IPSR_ISR_Msk' undeclared` o `'SCB' undeclared`. La solución es usar
`zephyr_library_include_directories()`, que limita ese include path únicamente a la
librería `hal_lpc43xx`.

Fuentes de LPCOpen incluidas actualmente y por qué:
- `clock_18xx_43xx.c` — funciones `Chip_Clock_*` (CGU: PLL1, dividers, base clocks).
- `sysinit_18xx_43xx.c` — `Chip_SetupXtalClocking()` / `Chip_SystemInit()`.
- `chip_18xx_43xx.c` — define `SystemCoreClock` / `SystemCoreClockUpdate()`, que el resto
  de los módulos de LPCOpen da por hecho que existe.
- El multiplexado de pines (SCU) **no tiene `.c` propio**: `Chip_SCU_PinMuxSet()` y
  compañía están `STATIC INLINE` en `scu_18xx_43xx.h`, así que no hace falta (ni existe)
  un `scu_43xx.c` para agregar a la librería.
- Los include paths adicionales `include/config_43xx` e `include/usbd_rom` se agregaron
  porque `chip.h` y otros headers de LPCOpen los referencian con rutas relativas simples
  (`#include "cmsis_43xx.h"`, etc.), no con el prefijo de subdirectorio.
- `CHIP_LPC43XX` y `CORE_M4` se definen globalmente (`zephyr_compile_definitions()`, no
  `zephyr_library_compile_definitions()`) porque cualquier driver que incluya `chip.h`
  necesita verlas, y esos drivers viven en librerías distintas de `hal_lpc43xx`.

---

## 3. Capa de SoC

Todos los archivos de SoC viven bajo `soc/nxp/lpc43xx/`. El nivel `nxp/` es el fabricante
del chip y `lpc43xx/` es la familia de SoC. Zephyr descubre esta ruta porque
`soc_root: .` está seteado en `module.yml`.

### `soc/nxp/lpc43xx/soc.yml`

Declara la jerarquía `family → series → socs` que Zephyr lee para enumerar los SoC de la
familia (acá: familia `lpc43xx`, serie `lpc433x`, un solo SoC `lpc4337`).

### Los tres Kconfig del SoC

Zephyr distingue tres responsabilidades distintas, cada una en su propio archivo:

- **`Kconfig.soc`** — símbolos estructurales y strings (`SOC_FAMILY_LPC43XX`,
  `SOC_SERIES_LPC433X`, `SOC_LPC4337`, y los defaults de `SOC_FAMILY`/`SOC_SERIES`/`SOC`).
  Acá también se selecciona `SOC_EARLY_INIT_HOOK`, que le dice a Zephyr que llame a
  `soc_early_init_hook()` desde `soc.c` antes de arrancar el kernel — ahí va la
  inicialización del reloj. **No** deben ir acá símbolos de capacidad de hardware.
- **`Kconfig`** — capacidades de hardware: `select CPU_CORTEX_M4`, `CPU_HAS_ARM_MPU`,
  `CPU_HAS_FPU` (el M4 del LPC4337 tiene FPU de precisión simple — confirmado por el
  header CMSIS oficial, `__FPU_PRESENT 1` bajo `CORE_M4`) y
  `CPU_CORTEX_M_HAS_SYSTICK`. También define `UART_LPC43XX` (ver
  [sección 8](#8-driver-de-uart)) — el símbolo de habilitación dedicado del driver de
  UART vive acá porque `soc/nxp/lpc43xx/Kconfig` ya se sabe que se incluye
  automáticamente; un `drivers/serial/Kconfig` nuevo necesitaría su propio `source`/
  `rsource` que nada provee todavía. Un `select CLOCK_CONTROL` que estaba acá quedó
  comentado: nada en este port usa la API `clock_control` de Zephyr (`soc.c` y el driver
  de UART tocan la CGU con registros crudos directamente) — dejarlo activo solo generaba
  un warning cosmético de CMake (`No SOURCES given to Zephyr library: drivers__clock_control`)
  sin aportar nada.
- **`Kconfig.defconfig`** — valores numéricos por defecto para símbolos ya existentes de
  Zephyr, nunca símbolos nuevos: `NUM_IRQS = 53` (la tabla de vectores del M4 llega hasta
  `QEI_IRQn = 52`, confirmado contra el enum `IRQn_Type` del header CMSIS oficial) y
  `SYS_CLOCK_HW_CYCLES_PER_SEC = 204000000` (clock de CPU tras PLL1, el máximo soportado
  por el M4).

### `soc/nxp/lpc43xx/soc.h`

Visible globalmente (kernel, drivers y arch lo ven). Tiene un solo trabajo: incluir
`<cmsis_core_m_defaults.h>`, que lee `arm,num-irq-priority-bits` del DTS (3 bits en este
chip → `__NVIC_PRIO_BITS = 3`, 8 niveles), `CONFIG_CPU_HAS_ARM_MPU` y `CONFIG_CPU_HAS_FPU`
del Kconfig, y termina incluyendo el `core_cm4.h` **de Zephyr** (no el de LPCOpen). No
incluir `chip.h` ni `cmsis.h` de LPCOpen acá — ambos definen `IRQn_Type` también, y
tener las dos definiciones es un error de redeclaración para cada enumerador.

### `soc/nxp/lpc43xx/soc.c`

Implementa `soc_early_init_hook()`. Se compila como parte de la librería del kernel de
Zephyr (ve `soc.h`, por lo tanto el `IRQn_Type` de Zephyr) — por eso usa acceso directo a
registros de la CGU (Clock Generation Unit) en vez de las funciones `Chip_Clock_*` de
LPCOpen: incluir `chip.h` acá volvería a traer el `IRQn_Type` de LPCOpen y produciría el
mismo conflicto de redeclaración. Ese patrón (registros crudos en `soc.c`, HAL del
fabricante permitida en librerías separadas) es el que sigue el resto del port.

La secuencia de arranque de reloj configura PLL1 en modo directo (crystal de 12 MHz × 17
= 204 MHz, sin post-divisor) y después conmuta `BASE_M4_CLK` para que el núcleo M4 tome
su reloj de ahí. A diferencia de familias NXP más simples (un solo PLL), el LPC43xx usa
una CGU con varios PLL (PLL1 principal, PLL0USB, PLL0AUDIO) — la secuencia y los
registros usados están documentados con comentarios en el archivo mismo.

> **[verificar]** Los valores de `MSEL`/`NSEL` y la secuencia de arranque del PLL1 se
> construyeron a partir de los campos de registro confirmados en UM10503 (tabla 134),
> pero no están validados contra un ejemplo de referencia probado en otro hardware. El
> LPC43xx también tiene (igual que el LPC17xx) un mecanismo de "shadow"/alias de memoria
> en `0x00000000` controlado por un registro del bloque CREG; su offset exacto no se
> confirmó y se omite acá — revisar el capítulo de CREG en UM10503 antes de depender de
> él.

### `soc/nxp/lpc43xx/CMakeLists.txt`

Compila `soc.c` en la librería del SoC y expone el directorio actual con
`zephyr_include_directories(.)` para que `soc.h` y `pinctrl_soc.h` sean visibles
globalmente. También registra `linker.ld` como `SOC_LINKER_SCRIPT`.

### `soc/nxp/lpc43xx/linker.ld`

Delega en la plantilla de linker ARM estándar de Zephyr vía `REGION_ALIAS`; no define un
mapa de memoria propio más que el alias de la tabla de vectores. El mapa de memoria real
(qué región de flash/SRAM usa el M4) lo definen los nodos `flash0`/`sram0` del
`.dtsi` — ver sección 4.

Mapa de memoria del LPC4337 completo (UM10503, tablas 10 y 11, fila LPC433x — el LPC4337
tiene 512 KB de flash en total, no 1 MB: ese tamaño es el de la serie LPC43x7):

| Región | Dirección | Tamaño | Uso en este port |
|---|---|---|---|
| Flash banco A | `0x1A000000` | 256 KB | `flash0` / código del M4 |
| Flash banco B | `0x1B000000` | 256 KB | libre (candidato para imagen del M0, sección 11) |
| SRAM local banco 1 | `0x10000000` | 32 KB | `sram0` / `zephyr,sram` del M4 |
| SRAM local banco 2 | `0x10080000` | 40 KB | libre (candidato para RAM del M0, sección 11) |
| SRAM AHB banco 1 | `0x20000000` | 32 KB | `sram2` — también usada como work-area de OpenOCD (sección 9) |
| SRAM AHB banco 2 | `0x20008000` | 16 KB | `sram3` |

---

## 4. Device tree — capa de SoC

### `dts/arm/nxp/nxp_lpc4337.dtsi`

Declara todos los periféricos del SoC con sus direcciones de hardware e IRQ, más los
nodos de CPU/flash/SRAM. Las placas lo incluyen y habilitan periféricos selectivamente.
Todos los valores vienen de UM10503.

Puntos no obvios:
- `cpu0` usa `compatible = "arm,cortex-m4f"` (con F, porque el M4 de este chip tiene
  FPU) y el override `&nvic { arm,num-irq-priority-bits = <3>; };` al final del archivo,
  que alimenta a `cmsis_core_m_defaults.h` (ver sección 3).
- El nodo `gpio` es un solo periférico `GPIO_PORT` con 8 puertos hijos (`gpio0`..`gpio7`),
  no 8 periféricos separados como en otras familias LPC. Cada puerto hijo usa `reg` como
  **índice de puerto** (0-7) dentro de los arreglos de registros compartidos, no como
  dirección de memoria — de ahí `#size-cells = <0>` en el nodo padre. El detalle de por
  qué está en la sección 7 (driver de GPIO).
- Los cuatro UART del chip están declarados (`uart0`=USART0, `uart1`=UART1,
  `uart2`=USART2, `uart3`=USART3), pero solo `uart2` está habilitado a nivel de placa
  (ver sección 5) — los demás quedan `status = "disabled"` en el `.dtsi` de SoC.
  **`uart2` y `uart3` NO son contiguos con `uart0`/`uart1` en el mapa de memoria**:
  `0x40081000`/`0x40082000` para USART0/UART1, pero `0x400C1000`/`0x400C2000` para
  USART2/USART3 — un salto de bus completo, no `+0x1000` por instancia. Este port tuvo
  un bug real por asumir lo contrario (`uart2` mal puesto en `0x40083000`, extrapolando
  la progresión aritmética de `uart0`/`uart1`): compilaba y flasheaba sin error, pero
  el UART nunca respondía porque esa dirección no correspondía a ningún periférico real.
  **No extrapolar direcciones de periféricos — confirmar cada una individualmente**
  contra el header CMSIS o UM10503.

### `dts/bindings/gpio/nxp,lpc43xx-gpio.yaml` y `nxp,lpc43xx-gpio-port.yaml`

Dos bindings porque el hardware tiene esa forma: el primero solo declara el bloque de
registros compartido (`GPIO_PORT`); el segundo declara cada puerto como
`gpio-controller`, con `reg` reinterpretado como índice de puerto y `ngpios` default 32.

### `dts/bindings/pinctrl/nxp,lpc43xx-pinctrl.yaml`

Sigue la estructura de dos niveles `child-binding` que usa el subsistema pinctrl de
Zephyr (`&pinctrl { estado { grupo { pinmux = <...>; }; }; };`) — ver sección 6 para el
detalle de la codificación de `pinmux`.

---

## 5. Capa de placa

La placa concreta de este port es la EDU-CIAA-NXP del Proyecto CIAA, no un devkit
genérico de NXP — de ahí `boards/ciaa/edu_ciaa_nxp/` en vez de `boards/nxp/...`.

### `boards/ciaa/edu_ciaa_nxp/board.yml`

`name: edu_ciaa_nxp` (debe coincidir con el directorio y el prefijo de archivos),
`vendor: ciaa` (quien diseña la placa), `socs: [lpc4337]`.

### `boards/ciaa/edu_ciaa_nxp/Kconfig.edu_ciaa_nxp`

Nombrado según la placa, no `Kconfig.board`. Define `config BOARD_EDU_CIAA_NXP` (el
símbolo lo deriva Zephyr del `name` de `board.yml` en mayúsculas — un símbolo truncado o
mal derivado es un error común y silencioso: la placa se sigue detectando, pero
`Kconfig.defconfig` nunca se activa) y `select SOC_LPC4337`.

### `boards/ciaa/edu_ciaa_nxp/Kconfig.defconfig`

Valores por defecto a nivel placa, bajo `if BOARD_EDU_CIAA_NXP`. Hoy solo tiene el
comentario recordando reactivar `UART_CONSOLE` cuando exista el driver de UART.

### `boards/ciaa/edu_ciaa_nxp/edu_ciaa_nxp_lpc4337_defconfig`

Patrón `<placa>_<soc>_defconfig` — acá el nombre de placa (`edu_ciaa_nxp`) y el de SoC
(`lpc4337`) son distintos, a diferencia de un devkit de un solo SoC donde suelen
coincidir. Habilita `PINCTRL`/`GPIO`, deja `SERIAL`/`UART_CONSOLE` apagados y
`BUILD_OUTPUT_HEX=y` para generar `zephyr.hex`.

### `boards/ciaa/edu_ciaa_nxp/edu_ciaa_nxp_lpc4337.dts`

Nombres y pines de LEDs/pulsadores confirmados contra la tabla de pines de la sAPI del
firmware oficial del proyecto CIAA (`sapi_gpio.c`, formato
`{grupo-SCU,pin-SCU}, FUNC, {puerto-GPIO,bit-GPIO}`):

| Señal | Pin SCU | FUNC | GPIO |
|---|---|---|---|
| LEDR (LED RGB, rojo) | P2_0 | 4 | GPIO5[0] |
| LEDG (LED RGB, verde) | P2_1 | 4 | GPIO5[1] |
| LEDB (LED RGB, azul) | P2_2 | 4 | GPIO5[2] |
| LED1 | P2_10 | 0 | GPIO0[14] |
| LED2 | P2_11 | 0 | GPIO1[11] |
| LED3 | P2_12 | 0 | GPIO1[12] |
| TEC1 | P1_0 | 0 | GPIO0[4] |
| TEC2 | P1_1 | 0 | GPIO0[8] |
| TEC3 | P1_2 | 0 | GPIO0[9] |
| TEC4 | P1_6 | 0 | GPIO1[9] |

Puntos no obvios:
- LED1-LED3 y TEC1-TEC4 quedan en función GPIO ya con el FUNC de reset (FUNC0) — andan
  sin aplicar ningún estado de pinctrl. LEDR/LEDG/LEDB (el LED RGB) no: su función GPIO es
  FUNC4, así que sin configurar la SCU explícitamente esos tres pines quedan en la función
  de reset (no GPIO) y el LED RGB no responde. El archivo define un grupo `leds_default`
  en `&pinctrl` con los seis pines para dejarlo documentado, aunque **[verificar]**:
  `gpio-leds`/`gpio-keys` son bindings genéricos de Zephyr sin `pinctrl-0` declarado, así
  que ese estado no se aplica solo — hace falta confirmar el mecanismo correcto
  (`pinctrl-0` en el nodo, o un `pinctrl_apply_state()` manual desde código de placa)
  antes de que el LED RGB funcione.
- `uart0` se mantiene deshabilitado: los pines P2_0/P2_1 (donde en otras placas suele
  mapearse USART0 TXD/RXD) están soldados acá al LED RGB, no a un conector serie.
  El puente USB-serie de depuración de esta placa sale por el canal B del FT2232H (U6),
  conectado a **USART2** en P7_1 (TXD)/P7_2 (RXD), FUNC6 — confirmado por prueba directa
  en hardware (`poll_out`/`poll_in` funcionando extremo a extremo contra un terminal
  serie). `uart2` es el único UART habilitado en este `.dts`; ver sección 8 para el
  driver.
- Cualquier pin "genérico" del LPC4337 que se quiera usar para un periférico nuevo debe
  chequearse contra esta tabla antes de asumir que está libre en esta placa específica.

### `boards/ciaa/edu_ciaa_nxp/board.cmake` y `support/openocd.cfg`

Ver sección 9 — el debugger on-board de esta placa (FT2232H por JTAG) es
significativamente distinto del de un devkit NXP genérico (LPC-Link2 por SWD), así que
esta configuración no es intercambiable con la de otra placa LPC43xx.

---

## 6. Driver de pinctrl

Es el primer driver a implementar porque todos los demás periféricos dependen de él para
configurar sus pines antes de usarlos. Debe registrarse antes de que corra el `init` de
cualquier otro driver.

### `include/dt-bindings/pinctrl/lpc43xx-pinctrl.h`

Header incluido tanto por los `.dts` como por el driver en C. Codifica una configuración
de pin SCU (System Control Unit — el equivalente en LPC43xx del IOCON/PINSEL de otras
familias LPC) como una palabra de 32 bits:

| Bits | Campo | Significado |
|---|---|---|
| `[4:0]` | pin | número de pin (0-31) |
| `[8:5]` | puerto | puerto SCU (0-15: P0-P9 = 0-9, PA-PF = 10-15) |
| `[11:9]` | función | campo `MODE` del registro `SFSPx_y` (0-7) |
| `[13:12]` | bias | 0=pull-up, 1=repeater, 2=ninguno, 3=pull-down |
| `[14]` | input-enable | habilita el buffer de entrada (`EZI`) |

También define las macros con nombre para los pines reales de la EDU-CIAA-NXP
(`EDU_CIAA_PIN_LEDR`, `..._TEC1`, etc., ver sección 5) — deliberadamente **no** define
macros de UART todavía, porque el pin real del puente USB-serie no está confirmado.

### `soc/nxp/lpc43xx/pinctrl_soc.h`

Requerido por el subsistema pinctrl de Zephyr en todo SoC: define `pinctrl_soc_pin_t`
(acá, el `uint32_t` de arriba) y las macros `Z_PINCTRL_STATE_PIN(S)_INIT` que expanden los
arreglos `pinmux` del DTS en listas de inicialización en C. Es genérico y no debería
necesitar cambios al agregar nuevos periféricos.

### `drivers/pinctrl/pinctrl_nxp_lpc43xx.c`

Implementa `pinctrl_configure_pins()`. Los registros `SFSPx_y` de la SCU (base
`0x40086000`) están espaciados `0x80` bytes por puerto y `0x4` bytes por pin — fórmula
derivada directamente de las direcciones confirmadas en el header CMSIS
(`SFSPD_0 @ 0x40086680`, con la SCU en `0x40086000`, da puerto D = índice 13). El
registro tiene el campo `MODE` (función, bits `[2:0]`), `EPD`/`EPUN` (bits `[3]`/`[4]`,
combinados según la tabla de bias de arriba) y `EZI` (bit `[6]`, buffer de entrada).

### `drivers/pinctrl/CMakeLists.txt`

Trivial: una librería con un solo archivo fuente.

---

## 7. Driver de GPIO

### `drivers/gpio/gpio_nxp_lpc43xx.c`

El periférico `GPIO_PORT` del LPC43xx (base `0x400F4000`) organiza sus registros de
control como arreglos de 8 palabras — uno por puerto — no como bloques separados por
puerto (a diferencia de otras familias LPC). Los arreglos relevantes empiezan en el
offset `0x2000` de la base del periférico (`0x400F6000` absoluto), espaciados `0x80`
bytes entre sí: `DIR[8]`, `MASK[8]`, `PIN[8]`, `MPIN[8]`, `SET[8]`, `CLR[8]`, `NOT[8]`.

Por esto el driver no recibe una dirección base por instancia (todas las instancias
apuntan al mismo bloque de registros): el config de cada instancia guarda un **índice de
puerto** (`uint8_t port`, leído de `DT_INST_REG_ADDR(n)` — recordar que en esta placa
`reg` en el nodo de puerto es un índice, no una dirección, ver sección 4) y todas las
operaciones indexan los arreglos compartidos con ese puerto.

Interrupciones (`pin_interrupt_configure`, `manage_callback`) devuelven `-ENOTSUP`: no
implementadas todavía.

### `drivers/gpio/CMakeLists.txt`

Trivial: una librería con un solo archivo fuente.

---

## 8. Driver de UART

Único UART habilitado en esta placa: `uart2` (USART2), por las razones de cableado
explicadas en la sección 5. El driver es de **polling puro** (`poll_in`/`poll_out`
únicamente) — sin interrupciones, sin runtime-configure, sin async. Probado extremo a
extremo contra un terminal serie por el canal B del FT2232H on-board.

### `dts/bindings/serial/nxp,lpc43xx-uart.yaml`

El UART/USART del LPC43xx es una IP estilo 16550 (misma familia que el UART del LPC17xx),
**no** un Flexcomm como el de otras familias LPC más nuevas (LPC84x, LPC54xxx, LPC55xxx) —
importante no copiar el binding de esas familias como plantilla: un Flexcomm tiene un
`clock-source` seleccionable por instancia (`fro`/`frg0`/`frg1`/...) que no existe acá. El
binding incluye `uart-controller.yaml` (trae `current-speed`, `parity`, `stop-bits`,
`data-bits` gratis) y `pinctrl-device.yaml` (trae `pinctrl-0`..`pinctrl-4` y
`pinctrl-names` — preferido sobre declarar esas propiedades a mano, como hacen algunos
bindings LPC más viejos). No declara `clocks`: el clock de este UART no es un consumidor
de un `clock_control` device de Zephyr, es un registro de la CGU que el propio driver
prende directamente (ver más abajo).

### `drivers/serial/serial_nxp_lpc43xx.c`

Registros usados (offsets relativos a la dirección de cada instancia — **no confundir
con un bloque compartido tipo GPIO_PORT**: a diferencia del GPIO, cada UART tiene su
propia dirección base independiente, así que `DT_INST_REG_ADDR(n)` ya es la dirección
completa de esa instancia, sin necesidad de índice):

| Offset | Registro(s) | Uso en este driver |
|---|---|---|
| `0x00` | `RBR`/`THR`/`DLL` | mismo registro físico, 3 significados según DLAB (bit 7 de LCR) y dirección de acceso |
| `0x04` | `IER`/`DLM` | solo se usa `DLM` (byte alto del divisor de baudrate) |
| `0x08` | `FCR`/`IIR` | solo se usa `FCR`, de solo escritura (control de FIFO) |
| `0x0C` | `LCR` | formato de trama + bit DLAB |
| `0x10` | `MCR` | sin usar — presente solo para que `LSR` caiga en el offset correcto |
| `0x14` | `LSR` | bit 0 = dato recibido listo, bit 5 = THR vacío |

`uart_lpc43xx_init()` hace, en este orden, y el orden importa:

1. **Habilita el "branch clock" de la CGU para esta instancia** (`BASE_UART0_CLK` en
   `0x4005009C`, `BASE_UART1_CLK` en `0x400500A0`, `BASE_UART2_CLK` en `0x400500A4`,
   `BASE_UART3_CLK` en `0x400500A8` — mismo layout de campo que `BASE_M4_CLK` en `soc.c`,
   `CLK_SEL` en bits `[28:24]`, `0x09` = PLL1). **Este paso es la lección más cara de
   todo este driver**: `soc.c` solo prende `BASE_M4_CLK` (el clock del núcleo). Ningún
   otro periférico tiene su branch clock activo por default — a diferencia de MCUs más
   simples, en la CGU del LPC43xx cada periférico necesita su propio enable explícito.
   Sin este paso, el driver compila, flashea, y `poll_out` se cuelga para siempre
   esperando `LSR & THRE`, porque el periférico nunca reacciona a nada — ni siquiera a
   los propios `LCR`/`DLL`/`FCR` que el resto del `init()` escribe. Como los cuatro
   offsets no son una progresión aritmética a partir de la dirección del periférico,
   el driver los resuelve con una función que mapea `regs` (la dirección física de la
   instancia) al offset correcto — no se pueden derivar por cálculo.
2. Aplica el estado de pinctrl (`pinctrl_apply_state()`), igual que en cualquier otro
   driver que use pines.
3. Calcula el divisor de baudrate: `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / (16 * baudrate)`.
   Válido porque `soc.c` deja el clock del núcleo (y, ahora, el de este UART) en la
   misma PLL1 a 204 MHz — si algún día alguno de los dos se reconfigura a una fuente o
   divisor distinto, este cálculo queda desactualizado sin ningún error de compilación
   ni de runtime, solo un baudrate incorrecto.
4. Habilita y resetea los FIFOs (`FCR`).

### `drivers/serial/CMakeLists.txt`

Trivial: una librería con un solo archivo fuente.

### `soc/nxp/lpc43xx/Kconfig` — `config UART_LPC43XX`

A diferencia de GPIO/pinctrl, UART sí necesita un símbolo de habilitación dedicado
(`depends on DT_HAS_NXP_LPC43XX_UART_ENABLED`, `select SERIAL_HAS_DRIVER`) — no alcanza
con el genérico `CONFIG_SERIAL` de Zephyr. Motivo concreto: `CONFIG_UART_CONSOLE` (lo que
permite rutear `printk` por este UART) tiene `depends on SERIAL && SERIAL_HAS_DRIVER` en
el árbol de Kconfig de Zephyr, y `SERIAL_HAS_DRIVER` es un bool oculto que **ningún**
backend activa automáticamente — tiene que seleccionarlo explícitamente el driver
concreto. Sin este símbolo, `UART_CONSOLE` queda inseleccionable para siempre, sin
importar qué más esté prendido.

---

## 9. Flasheo

### Build

```sh
west build -b edu_ciaa_nxp app/
```

### El debugger on-board no es un LPC-Link2

La EDU-CIAA-NXP trae su propio programador/debugger: un **FTDI FT2232H** (U6 en el
esquemático), cableado como adaptador **JTAG** — no SWD — vía la interfaz genérica
`interface ftdi`/`adapter driver ftdi` de OpenOCD. Nada de lo que se suele asumir para un
LPC4337 "genérico" (LPC-Link2, CMSIS-DAP, SWD, VID/PID `0x1fc9`) aplica acá, salvo que se
conecte un probe externo por el header de debug P3 (Cortex-Debug, 10 pines, 1.27 mm) en
vez de usar el USB on-board. pyOCD tampoco sirve (no tiene target integrado para
LPC4337) — **OpenOCD es la única vía de flasheo con el USB on-board de esta placa**.

`boards/ciaa/edu_ciaa_nxp/support/openocd.cfg` está adaptado del script oficial del
proyecto CIAA (`github.com/ciaa/firmware_v1`,
`.../openocd/cfg/cortexM4/lpc43xx/lpc4337/ciaa-nxp.cfg`). Parámetros clave:

| Parámetro | Valor | Motivo |
|---|---|---|
| Interfaz/VID:PID | `ftdi`, `0x0403:0x6010` | FT2232H on-board, no NXP |
| Transporte | `jtag` | El FT2232H está cableado como JTAG, no SWD |
| TAP IDs | M4=`0x4ba00477`, M0=`0x0ba01477` | Los dos deben declararse para que el scan de la cadena JTAG sea correcto (ver nota de M0 abajo) |
| Work-area de flasheo | `0x20000000`, 8 KB (SRAM AHB banco 1) | El algoritmo del driver `lpc2000` necesita RAM de trabajo; se usa AHB SRAM en vez de `0x10000000` para no competir con `zephyr,sram` |
| Banco de flash | `lpc2000`/`lpc4300`, `0x1a000000`, `0x80000` (512 KB), clock `96000` kHz | Se declara como una sola región aunque el chip tiene físicamente dos bancos de 256 KB — así lo hace el script oficial de CIAA y así funciona en la práctica |
| Reset | `reset_config none` + `cortex_m reset_config vectreset` | TRST/SRST no están cableados más allá del buffer en esta placa; no hay nRESET real para usar `connect_assert_srst` |

### La nota importante sobre el M0: por qué no hay un `target` para él

El script declara los **dos** TAP de la cadena JTAG (`jtag newtap ... m4`, `... m0`)
porque el M0 es parte física de la cadena y omitirlo rompe el scan (OpenOCD necesita ver
todos los TAP para calcular los offsets de IR/DR correctamente). Pero solo se crea un
`target` (DAP + `cortex_m`) para el M4. Si se intenta crear también un target para el M0,
cada `reset halt` hace timeout (`TARGET: lpc4337.m0 - Not halted`), porque **el M0 no
tiene clock ni sale de reset hasta que el M4 lo habilita explícitamente en tiempo de
ejecución** (vía CREG/RGU) — y como el firmware actual es de un solo núcleo, el M4 nunca
hace eso. No es un bug de la config de OpenOCD: es el comportamiento esperado hasta que
exista bring-up del M0. Ver la sección 11 para qué falta implementar.

### Comando de flasheo

```sh
west flash --runner openocd
```

### Protección de lectura de código (CRP)

Igual que en otras familias LPC, el offset de CRP es `0x2FC` desde el inicio del banco de
flash en uso (UM10503 §6.6, tabla 31):

| Valor | Efecto |
|---|---|
| `0x12345678` (CRP1) | Deshabilita JTAG/SWD; permite actualización parcial de flash vía ISP |
| `0x87654321` (CRP2) | Como CRP1, pero solo permite borrado completo de chip vía ISP |
| `0x43218765` (CRP3) | Como CRP2, y además deshabilita la entrada a ISP por el pin P2_7 si hay código de usuario válido (bloqueo total) |
| `0x4E697370` (NO_ISP) | Deshabilita solo el pedido de ISP por P2_7 (sin protección de lectura; JTAG/SWD siguen funcionando) |

El linker actual no protege explícitamente esta dirección — agregar una sección si se
necesita.

### Recuperación por ISP UART

El boot ROM tiene un modo ISP por UART que funciona sin importar el estado de JTAG o si
hay CRP1/CRP2 activo: mantener **P2_7** (activo en bajo) durante el reset entra en ISP
sobre USART0 (P2_0/P2_1). Dos advertencias específicas de esta placa:

1. **P2_0/P2_1 no están expuestos en un conector accesible**: están soldados al LED RGB
   (LEDR/LEDG, ver sección 5). El procedimiento estándar (cablear un adaptador USB-UART
   externo a esos pines) no es practicable sin soldar directo al LED o al chip. En la
   práctica, para esta placa conviene agotar primero la recuperación por JTAG (que
   además es la vía normal de flasheo) antes de intentar ISP por UART.
2. El boot ROM también samplea P2_9, P2_8, P1_2 y P1_1 para elegir la fuente de boot
   cuando P2_7 está en bajo — y **TEC2 (P1_1) y TEC3 (P1_2) son dos de esos cuatro
   pines**. Mantener presionado TEC2 o TEC3 durante un reset con P2_7 en bajo puede
   alterar la fuente de boot elegida por el ISP (no afecta el arranque normal, con P2_7
   en alto).

Herramientas compatibles con LPC43xx: [Flash Magic](https://www.flashmagictool.com)
(gratuita, de NXP) o `lpc21isp` **[verificar compatibilidad de la versión usada]**.

---

## 10. Cómo agregar un driver de periférico nuevo

Checklist genérico, usando UART como ejemplo — pero aplica igual a SPI, I2C, ADC, etc.
Se marca **CREAR** para archivos nuevos y **MODIFICAR** para archivos ya existentes que
hay que tocar. La sección 8 (Driver de UART) es este mismo checklist ya resuelto para un
caso real, con las dos trampas concretas que costaron más tiempo — vale la pena leerla
antes de repetir el proceso para otro periférico.

1. **CREAR** `dts/bindings/<subsistema>/nxp,lpc43xx-<periferico>.yaml` — el binding de
   Zephyr para el nuevo compatible (p. ej. `nxp,lpc43xx-uart`). Incluir el binding base
   del subsistema (`uart-controller.yaml`, `spi-controller.yaml`, etc.) más `base.yaml`,
   y declarar las propiedades requeridas (`reg`, `interrupts`, y las que aplique:
   `current-speed` para UART, `pinctrl-0`/`pinctrl-names` si el periférico usa pines).

2. **MODIFICAR** `dts/arm/nxp/nxp_lpc4337.dtsi` — agregar el nodo del periférico bajo
   `soc { ... }` con su `reg` (dirección real del chip) e `interrupts` (número de IRQ real
   — ver la tabla de `IRQn_Type` citada en la sección 3), con `status = "disabled"` por
   default (la placa concreta lo habilita si lo necesita).

3. **MODIFICAR** el `.dts` de la placa (`boards/ciaa/edu_ciaa_nxp/edu_ciaa_nxp_lpc4337.dts`)
   — agregar el grupo de pines en `&pinctrl` con los pines reales **de esta placa**
   (chequeados contra la tabla de la sAPI, sección 5 — no asumir que un pin "genérico"
   del chip está libre acá) y habilitar el nodo (`status = "okay"`, `pinctrl-0`,
   propiedades específicas del periférico).

4. **MODIFICAR** `cmake/CMakeLists.txt` — si el driver necesita una fuente de LPCOpen,
   agregarla con `zephyr_library_sources_ifdef(CONFIG_<PERIFERICO>_LPC43XX ...)` a la
   librería `hal_lpc43xx` existente (no crear una librería nueva).

5. **CREAR** `drivers/<subsistema>/<nombre>_nxp_lpc43xx.c` + su `CMakeLists.txt` — el
   driver de Zephyr en sí. Puede incluir `chip.h` de LPCOpen sin conflicto porque se
   compila como librería separada de la de Zephyr (no hace falta
   `zephyr_library_include_directories()` acá: ya lo scopea el propio
   `add_subdirectory()` que se agrega en el paso 7).

   **Antes de dar el driver por terminado, confirmar si el periférico tiene su propio
   "branch clock" en la CGU** (un registro `BASE_<PERIFERICO>_CLK`, en el mismo bloque
   que `BASE_M4_CLK`). `soc.c` solo prende el clock del núcleo — nada más viene
   habilitado por default. El síntoma de saltearse esto no es un error de compilación
   ni un fault obvio: el periférico simplemente no reacciona a nada (registros que no
   cambian, flags de estado que nunca se setean), como pasó con UART (sección 8) hasta
   agregar el enable de `BASE_UART2_CLK` en el propio `init()` del driver.

6. **CREAR** (o **MODIFICAR** si ya existe uno para el subsistema)
   `drivers/<subsistema>/Kconfig` — un símbolo de habilitación dedicado, por ejemplo:
   ```
   config UART_LPC43XX
       bool "..."
       default y
       depends on DT_HAS_NXP_LPC43XX_UART_ENABLED
       select SERIAL_HAS_DRIVER
   ```
   No siempre hace falta: para GPIO y pinctrl no se creó un símbolo dedicado porque los
   símbolos genéricos de Zephyr (`CONFIG_GPIO`, `CONFIG_PINCTRL`) ya alcanzan — solo hay
   un backend posible y la instanciación es automática vía
   `DT_INST_FOREACH_STATUS_OKAY`. Un símbolo dedicado hace falta cuando el subsistema de
   Zephyr no tiene ya una forma genérica de preguntar "¿hay un dispositivo de este
   compatible habilitado?", o cuando se necesita comportamiento condicional a nivel
   placa.

7. **MODIFICAR** `cmake/CMakeLists.txt` — agregar el bloque
   `if(CONFIG_<PERIFERICO>_LPC43XX) add_subdirectory(.../drivers/<subsistema> ...) endif()`,
   siguiendo el patrón ya usado para `pinctrl`/`gpio`.

8. **MODIFICAR** `boards/ciaa/edu_ciaa_nxp/edu_ciaa_nxp_lpc4337_defconfig` (y
   `Kconfig.defconfig` si hace falta un default a nivel placa) — habilitar el símbolo de
   Zephyr correspondiente (`CONFIG_SERIAL=y`, `CONFIG_UART_CONSOLE=y`, etc.) y el símbolo
   dedicado del paso 6 si se creó uno.

---

## 11. Porting de un SoC con múltiples procesadores (AMP)

El LPC4337 es un caso real, no hipotético, de este escenario: Cortex-M4 principal +
coprocesador Cortex-M0 (`M0APP` en la tabla de IRQ del M4, IRQ 1). Este port hoy solo
implementa el M4 — lo que ya se ve reflejado en la práctica en el problema descrito en la
sección 9: el `target` M0 de OpenOCD hace timeout porque nada lo saca de reset todavía.

### Qué falta para arrancar el M0

1. **`soc.yml` multi-núcleo**: declarar `cpuclusters` (`m4`, `m0app`) bajo el SoC
   `lpc4337`, y un `board.yml` que mapee cada cluster a un target de build
   (`cpucluster: m4` / `cpucluster: m0app`).
2. **DTS de placa por cluster**: `edu_ciaa_nxp_lpc4337_m4.dts` y
   `edu_ciaa_nxp_lpc4337_m0app.dts`, cada uno incluyendo el mismo `.dtsi` de SoC pero
   apuntando `zephyr,sram`/`zephyr,flash` a regiones distintas.
3. **Linker del M0**: la imagen del M0 se enlaza para ejecutar desde SRAM (banco 2 de
   SRAM local, `0x10080000`, 40 KB — ver la tabla de memoria en la sección 3), no desde
   flash directamente.
4. **Código de bring-up en el M4** (lo que hoy no existe y es la pieza central): antes de
   que el M0 pueda arrancar, el M4 tiene que copiar la imagen del M0 a la SRAM donde va a
   ejecutar, escribir la dirección de entrada en el registro de remapeo de boot del M0
   (`M0APPMEMMAP`, en el bloque CREG) y sacar al M0 de reset vía la RGU (Reset Generation
   Unit) — desasertando el bit de reset correspondiente en `RESET_CTRL0`/`RESET_CTRL1`.
   **[verificar]** los offsets exactos de estos registros no están confirmados contra
   UM10503 en esta pasada — revisar el capítulo de arranque multi-core antes de
   implementar.
5. **IPC M4↔M0**: el LPC43xx no tiene una IP de mailbox genérica de ARM. El mecanismo
   típico es memoria compartida (en una región de SRAM AHB) más las interrupciones
   cruzadas entre núcleos — el M4 ve al M0 como fuente de IRQ `M0APP`. Implementar esto
   como backend de `ipm`/`mbox` de Zephyr requeriría un driver específico para LPC43xx;
   el mecanismo exacto de señalización más allá de esa IRQ no está confirmado
   **[verificar]**.
6. **OpenOCD**: una vez que el M4 haga el paso 4, se puede agregar un segundo `target`
   para el M0 en `support/openocd.cfg` (`dap create $_CHIPNAME.m0.dap ...`,
   `target create $_CHIPNAME.m0 cortex_m ...`) y debería dejar de hacer timeout en
   `reset halt` — porque en ese punto el core ya tiene clock y salió de reset. Hasta
   entonces, mantenerlo sin `target` (solo el `jtag newtap`) es lo correcto, no un
   workaround temporal a "arreglar después" en el archivo de OpenOCD en sí.

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
| Bindings de DTS fuera de `dts/bindings/` (p. ej. bajo `dts/arm/nxp/bindings/`) | Error de validación de devicetree al no encontrar el binding | `dts_root` siempre escanea `dts/bindings/` en la raíz del módulo, sin importar dónde vivan los `.dtsi` |
| Un solo nodo GPIO por puerto con `reg` como dirección de memoria | Direcciones duplicadas/inválidas en el DTS | El LPC43xx comparte un único bloque de registros (`GPIO_PORT`) entre los 8 puertos; usar `reg` como índice de puerto (`#size-cells = <0>`), no como dirección |
| Asumir el pin de ISP de otras familias LPC (p. ej. P2.10) | La placa nunca entra en modo ISP | El LPC4337 usa **P2_7** activo en bajo |
| nRESET no cableado al probe | Se cuelga el MEM-AP incluso durante SRST | Cablear el pin 10 del probe a RESET de la placa; revisar el pin 1 (VTref) — no aplica en la EDU-CIAA-NXP, que no tiene nRESET cableado (ver `reset_config none`) |
| `boards/nxp/<placa>` para una placa que no diseña NXP | Confusión sobre a quién pertenece el layer | `vendor` en `board.yml` es quien diseña la placa (`ciaa`), no quien fabrica el SoC (`soc/nxp/` sigue siendo NXP) |
| `BOARD_<NOMBRE>` no coincide con el `name` de `board.yml` en mayúsculas | La placa compila pero `Kconfig.defconfig` nunca se activa (falla silenciosa) | Para `name: edu_ciaa_nxp` el símbolo es `BOARD_EDU_CIAA_NXP` |
| Asumir un LPC-Link2/CMSIS-DAP + SWD genérico para el debugger | OpenOCD no encuentra el DP, o falla con "unable to find CMSIS-DAP" | La EDU-CIAA-NXP trae un FT2232H on-board por **JTAG** (VID:PID `0x0403:0x6010`), no un probe NXP por SWD |
| Crear un `target` de OpenOCD para el M0 sin haber implementado su bring-up | Timeout en cada `reset halt` (`Not halted`) | El M0 no tiene clock hasta que el M4 lo habilita vía CREG/RGU en tiempo de ejecución (sección 11); hasta entonces, declarar solo el `jtag newtap`, sin `target` |
| Reusar pines "genéricos" del chip (p. ej. P2_0/P2_1 para UART0) sin chequear la placa | El periférico nunca responde, o se pisa con otra función ya cableada (acá, el LED RGB) | Confirmar cada pin contra la tabla de la sAPI/esquemático de la EDU-CIAA-NXP antes de asumir que está libre |
| Extrapolar la dirección de una instancia de periférico a partir de otra (p. ej. `uart2` = `uart1` + `0x1000`) | Compila y flashea sin error; el periférico nunca responde (`poll_out` cuelga para siempre) | Las instancias de un mismo periférico no son necesariamente contiguas — USART2/USART3 del LPC43xx están en `0x400C1000`/`0x400C2000`, un bloque de bus completo aparte de USART0/UART1 (`0x40081000`/`0x40082000`); confirmar cada dirección individualmente contra el header CMSIS |
| No habilitar el "branch clock" propio del periférico en la CGU (`BASE_<PERIFERICO>_CLK`) | Mismo síntoma que el de arriba — cuelgue esperando un bit de estado que nunca cambia, aun con la dirección correcta | `soc.c` solo prende `BASE_M4_CLK` (el clock del núcleo); cada periférico necesita su propio enable explícito (`CLK_SEL` = PLL1) antes de que sus registros reaccionen a algo — hacerlo en el `init()` del driver, no en `soc.c` |
| Nodo de DTS con hex en mayúsculas en la unit-address (p. ej. `uart@400C1000`) | Warning de `dtc`: `simple_bus_reg: unit address format error` | La unit-address del nombre del nodo debe ser hex en minúscula, igual que el valor canónico de `reg` |
| Un header de LPCOpen (p. ej. `config_43xx/sys_config.h`) con un `#define` incondicional que ya se define globalmente en `cmake/CMakeLists.txt` | Warning del compilador: `"CHIP_LPC43XX" redefined` | Guardar el `#define` en el header con `#ifndef` — `sys_config.h` es justamente el archivo que LPCOpen espera que el integrador edite, a diferencia del resto de la HAL |
| `select CLOCK_CONTROL` (u otro subsistema de Zephyr) sin un driver real detrás | Warning de CMake: `No SOURCES given to Zephyr library: drivers__<subsistema>` — no es un error, pero es ruido evitable | Sacar el `select` si nada en el port llama a la API de ese subsistema (acá, `CLOCK_CONTROL` quedó sin uso porque el clock se maneja con registros crudos) |
