#include <stdint.h>
#include <stdio.h>

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/adc.h"
#include "driverlib/fpu.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/uart.h"

#define ADC_CHANNEL_COUNT   9
#define ADC_SEQUENCE0_COUNT 8
#define ADC_SAMPLE_COUNT    500     /* 5× more averaging → smoother + slower output */

/* ADC / scaling constants */
#define ADC_REF_VOLTAGE     3.3f        /* Reference voltage in volts           */
#define ADC_MAX_VALUE       4095.0f     /* 12-bit ADC full-scale count          */

/* 1 ADC count → mV: (3300 mV / 4095) ≈ 0.8058 mV/count */
#define MV_PER_ADC_COUNT    (ADC_REF_VOLTAGE * 1000.0f / ADC_MAX_VALUE)

/* LM35-style sensor: 10 mV/°C  →  0.1 °C/mV */
#define DEG_C_PER_MV        0.1f

#define DIODE_CURRENT_SCALE 1.0f        /* Amps per Volt (set per your circuit) */

/* Typed output union so we can send all nine channels as floats */
typedef struct {
    float diode_current_limit;   /* mA   */
    float diode_current_actual;  /* mA   */
    float diode_temp_set;        /* °C   */
    float diode_temp_actual;     /* °C   */
    float diode_tec_err;         /* mV   */
    float crystal_temp_set;      /* °C   */
    float crystal_temp_actual;   /* °C   */
    float crystal_tec_err;       /* mV   */
    float fault;                 /* 0.0 or 1.0 */
} LaserChannels;

/* Raw ADC store (averaged counts) */
static uint32_t g_adcCounts[ADC_CHANNEL_COUNT];

/* --------------------------------------------------------------------------
 * UART0
 * -------------------------------------------------------------------------- */

static void UART0Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0)) {}

    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);
}

static void UART0WriteString(const char *text)
{
    while (*text != '\0') {
        UARTCharPut(UART0_BASE, *text++);
    }
}

/* Write a float with two decimal places, e.g. "23.45" or "-1.20"
 * Avoids pulling in the full printf machinery. */
static void UART0WriteFloat(float value)
{
    char buf[16];
    int  len = 0;

    /* Handle negative */
    if (value < 0.0f) {
        UARTCharPut(UART0_BASE, '-');
        value = -value;
    }

    /* Round to 2 dp before splitting */
    value += 0.005f;

    uint32_t integer  = (uint32_t)value;
    uint32_t fraction = (uint32_t)((value - (float)integer) * 100.0f);

    /* Integer part (reverse into buf) */
    if (integer == 0) {
        buf[len++] = '0';
    } else {
        uint32_t tmp = integer;
        while (tmp > 0) {
            buf[len++] = (char)('0' + (tmp % 10));
            tmp /= 10;
        }
    }
    /* Reverse */
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
    }
    buf[len] = '\0';
    UART0WriteString(buf);

    /* Decimal part — always two digits */
    UARTCharPut(UART0_BASE, '.');
    UARTCharPut(UART0_BASE, (char)('0' + (fraction / 10)));
    UARTCharPut(UART0_BASE, (char)('0' + (fraction % 10)));
}

/*
 * Output format:  /*f0,f1,f2,f3,f4,f5,f6,f7,f8* /\r\n
 *
 * All values are floats with 2 decimal places:
 *   [0] diode_current_limit   – mA
 *   [1] diode_current_actual  – mA
 *   [2] diode_temp_set        – °C
 *   [3] diode_temp_actual     – °C
 *   [4] diode_tec_err         – mV
 *   [5] crystal_temp_set      – °C
 *   [6] crystal_temp_actual   – °C
 *   [7] crystal_tec_err       – mV
 *   [8] fault                 – 0.00 or 1.00
 */
static void UART0WriteChannels(const LaserChannels *ch)
{
    UART0WriteString("/*");
    UART0WriteFloat(ch->diode_current_limit);   UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->diode_current_actual);  UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->diode_temp_set);        UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->diode_temp_actual);     UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->diode_tec_err);         UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->crystal_temp_set);      UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->crystal_temp_actual);   UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->crystal_tec_err);       UARTCharPut(UART0_BASE, ',');
    UART0WriteFloat(ch->fault);
    UART0WriteString("*/\r\n");
}

/* --------------------------------------------------------------------------
 * ADC0 — nine-input configuration
 * -------------------------------------------------------------------------- */

static void ADC0InitNineInputs(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0))  {}

    GPIOPinTypeADC(GPIO_PORTE_BASE,
                   GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_2 |
                   GPIO_PIN_1 | GPIO_PIN_0);
    GPIOPinTypeADC(GPIO_PORTB_BASE, GPIO_PIN_4);
    GPIOPinTypeADC(GPIO_PORTD_BASE,
                   GPIO_PIN_3 | GPIO_PIN_2 | GPIO_PIN_1 | GPIO_PIN_0);

    ADCSequenceDisable(ADC0_BASE, 0);
    ADCSequenceDisable(ADC0_BASE, 1);

    /* Sequence 0: channels 0-7 (8 steps) */
    ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH9);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH1);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH2);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_CH3);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 4, ADC_CTL_CH4);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 5, ADC_CTL_CH5);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 6, ADC_CTL_CH10);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 7,
                             ADC_CTL_CH7 | ADC_CTL_IE | ADC_CTL_END);

    /* Sequence 1: channel 8 (1 step) */
    ADCSequenceConfigure(ADC0_BASE, 1, ADC_TRIGGER_PROCESSOR, 1);
    ADCSequenceStepConfigure(ADC0_BASE, 1, 0,
                             ADC_CTL_CH8 | ADC_CTL_IE | ADC_CTL_END);

    ADCSequenceEnable(ADC0_BASE, 0);
    ADCSequenceEnable(ADC0_BASE, 1);
    ADCIntClear(ADC0_BASE, 0);
    ADCIntClear(ADC0_BASE, 1);
}

static void ADC0ReadOnce(uint32_t values[ADC_CHANNEL_COUNT])
{
    uint32_t seq0[ADC_SEQUENCE0_COUNT];
    uint32_t seq1;

    ADCProcessorTrigger(ADC0_BASE, 0);
    while (!ADCIntStatus(ADC0_BASE, 0, false)) {}
    ADCIntClear(ADC0_BASE, 0);
    ADCSequenceDataGet(ADC0_BASE, 0, seq0);

    ADCProcessorTrigger(ADC0_BASE, 1);
    while (!ADCIntStatus(ADC0_BASE, 1, false)) {}
    ADCIntClear(ADC0_BASE, 1);
    ADCSequenceDataGet(ADC0_BASE, 1, &seq1);

    for (uint32_t i = 0; i < ADC_SEQUENCE0_COUNT; i++) {
        values[i] = seq0[i];
    }
    values[8] = seq1;
}

static void ADC0ReadAveraged(uint32_t values[ADC_CHANNEL_COUNT])
{
    uint32_t sums[ADC_CHANNEL_COUNT];
    uint32_t sample[ADC_CHANNEL_COUNT];
    uint32_t i;

    for (i = 0; i < ADC_CHANNEL_COUNT; i++) {
        sums[i] = 0;
    }

    for (uint32_t n = 0; n < ADC_SAMPLE_COUNT; n++) {
        ADC0ReadOnce(sample);
        for (uint32_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
            sums[i] += sample[i];
        }
        /* ~1 ms dead time between samples at 40 MHz (40000 / 3 cycles per loop) */
        SysCtlDelay(13333);
    }

    for (uint32_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
        values[i] = sums[i] / ADC_SAMPLE_COUNT;
    }
}

/* --------------------------------------------------------------------------
 * Engineering-unit conversion
 *
 * All paths work in floats from count to final unit:
 *
 *   Temperature:
 *     count × MV_PER_ADC_COUNT           → mV
 *     mV    × DEG_C_PER_MV  (0.1 °C/mV) → °C   (correct for LM35-style)
 *
 *   Current:
 *     count / ADC_MAX_VALUE × ADC_REF_VOLTAGE → V
 *     V     × DIODE_CURRENT_SCALE             → A
 *     A     × 1000                            → mA
 *
 *   TEC error:
 *     count × MV_PER_ADC_COUNT → mV  (raw signal, sent as-is)
 *
 *   Fault:
 *     0.0 below mid-scale, 1.0 at or above
 * -------------------------------------------------------------------------- */

static void ConvertToEngUnits(const uint32_t counts[ADC_CHANNEL_COUNT],
                               LaserChannels *ch)
{
    /* Helper lambdas as inline expressions */
    #define COUNT_TO_MV(c)  ((float)(c) * MV_PER_ADC_COUNT)
    #define MV_TO_DEG(mv)   ((mv) * DEG_C_PER_MV)
    #define COUNT_TO_DEG(c) MV_TO_DEG(COUNT_TO_MV(c))

    /* Current: count → V → A → mA */
    float v0 = ((float)counts[0] / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    float v1 = ((float)counts[1] / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    ch->diode_current_limit  = v0 * DIODE_CURRENT_SCALE * 1000.0f;
    ch->diode_current_actual = v1 * DIODE_CURRENT_SCALE * 1000.0f;

    /* Temperature: count → mV → °C */
    ch->diode_temp_set      = COUNT_TO_DEG(counts[2]);
    ch->diode_temp_actual   = COUNT_TO_DEG(counts[3]);
    ch->crystal_temp_set    = COUNT_TO_DEG(counts[5]);
    ch->crystal_temp_actual = COUNT_TO_DEG(counts[6]);

    /* TEC error: count → mV */
    ch->diode_tec_err   = COUNT_TO_MV(counts[4]);
    ch->crystal_tec_err = COUNT_TO_MV(counts[7]);

    /* Fault: digital threshold at mid-scale */
    ch->fault = (counts[8] >= (uint32_t)(ADC_MAX_VALUE / 2.0f)) ? 1.0f : 0.0f;

    #undef COUNT_TO_MV
    #undef MV_TO_DEG
    #undef COUNT_TO_DEG
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(void)
{
    FPUEnable();
    FPULazyStackingEnable();

    /* 40 MHz system clock */
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);

    /* PF1 – heartbeat LED */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {}
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);

    UART0Init();
    ADC0InitNineInputs();

    while (1)
    {
        LaserChannels ch;

        ADC0ReadAveraged(g_adcCounts);
        ConvertToEngUnits(g_adcCounts, &ch);
        UART0WriteChannels(&ch);

        /* Toggle heartbeat LED */
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1,
                     GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_1) ^ GPIO_PIN_1);

        /* ~1 s dead time after each output frame (40 MHz / 3 cycles per loop) */
        SysCtlDelay(1333333);
    }
}