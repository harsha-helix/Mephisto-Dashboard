# Tiva Blink ADC Monitor

Bare-metal firmware for a Texas Instruments Tiva C Series TM4C123 board. The
program configures the system clock, toggles the onboard PF1 LED, samples nine
ADC inputs, averages the readings, and streams the values over UART0.

## Project Files

- `main.c` - application firmware, UART setup, ADC setup, sampling loop, and
  LED toggle.
- `startup_gcc.c` - minimal GCC startup code and interrupt vector table.
- `linker.ld` - linker script for 256 KB flash and 32 KB RAM.
- `Makefile` - build, flash, and clean targets.
- `read_adc.py` - helper script for reading serial ADC output from a PC.

## Hardware

This project is configured for `PART_TM4C123GH6PM`, commonly used on the
EK-TM4C123GXL LaunchPad.

UART0 uses the default debug serial pins:

- `PA0` - UART0 RX
- `PA1` - UART0 TX
- Baud rate: `115200`

ADC0 samples these analog channels:

| Output Index | Signal Name | ADC Channel |
| --- | --- | --- |
| 0 | `diode_current_limit` | AIN9 |
| 1 | `diode_current_actual` | AIN1 |
| 2 | `diode_temp_set` | AIN2 |
| 3 | `diode_temp_actual` | AIN3 |
| 4 | `diode_tec_err` | AIN4 |
| 5 | `crystal_temp_set` | AIN5 |
| 6 | `crystal_temp_actual` | AIN10 |
| 7 | `crystal_tec_err` | AIN7 |
| 8 | `fault` | AIN8 |

PF1 is configured as a digital output and toggles each sampling loop.

## Requirements

- ARM embedded GCC toolchain with `arm-none-eabi-gcc`
- TivaWare C Series installed at:
  `C:/ti/TivaWare_C_Series-2.2.0.295`
- OpenOCD installed at:
  `C:/OpenOCD-20260302-0.12.0`
- Python 3 and `pyserial` for `read_adc.py`

If your TivaWare or OpenOCD paths are different, update `TIVAWARE` or the
`flash` target in the `Makefile`.

## Build

```sh
make
```

This produces `main.elf`.

## Flash

Connect the board, then run:

```sh
make flash
```

The flash target uses OpenOCD with the `board/ti/ek-tm4c123gxl.cfg`
configuration.

## Serial Output

The firmware prints one line per sampling loop. Each line contains nine values
wrapped in `/* ... */`:

```text
/*805,402,165,166,112,164,165,161,0*/
```

The firmware first averages 100 raw ADC samples per channel, then post-processes
the averaged counts before printing:

| Output Index | Signal Name | Printed Unit / Meaning |
| --- | --- | --- |
| 0 | `diode_current_limit` | mA, using `DIODE_CURRENT_SCALE` amps/volt |
| 1 | `diode_current_actual` | mA, using `DIODE_CURRENT_SCALE` amps/volt |
| 2 | `diode_temp_set` | deg C, using `TEMP_SCALE` deg C/mV |
| 3 | `diode_temp_actual` | deg C, using `TEMP_SCALE` deg C/mV |
| 4 | `diode_tec_err` | raw averaged ADC count |
| 5 | `crystal_temp_set` | deg C, using `TEMP_SCALE` deg C/mV |
| 6 | `crystal_temp_actual` | deg C, using `TEMP_SCALE` deg C/mV |
| 7 | `crystal_tec_err` | raw averaged ADC count |
| 8 | `fault` | digital state: `0` below half-scale, `1` at or above half-scale |

The ADC voltage conversion is:

```text
volts = raw_count / 4095 * 3.3
```

With the current placeholder scaling constants, current channels use `1.0 A/V`
and temperature channels use `0.1 deg C/mV`. Update `DIODE_CURRENT_SCALE` and
`TEMP_SCALE` in `main.c` to match the actual analog front-end.

## Read ADC Data From a PC

Install `pyserial` if needed:

```sh
python -m pip install pyserial
```

List available serial ports:

```sh
python read_adc.py
```

Read from a specific port:

```sh
python read_adc.py COM3
```

On Linux, the port may look like `/dev/ttyACM0` or `/dev/ttyUSB0`.

`read_adc.py` accepts the firmware's `/* ... */` wrapper and prints the nine
processed fields with labels.

## Clean

```sh
make clean
```

This removes generated `.elf` files.
