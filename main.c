#include <stdint.h>

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/adc.h"
#include "driverlib/fpu.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/uart.h"

#define ADC_CHANNEL_COUNT 9
#define ADC_SEQUENCE0_COUNT 8
#define ADC_SAMPLE_COUNT 10

uint32_t diode_current_limit;
uint32_t diode_current_actual;
uint32_t diode_temp_set;
uint32_t diode_temp_actual;
uint32_t diode_tec_err;
uint32_t crystal_temp_set;
uint32_t crystal_temp_actual;
uint32_t crystal_tec_err;
uint32_t fault;

static void UART0Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) {
    }
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0)) {
    }

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

static void UART0WriteUInt(uint32_t value)
{
    char buffer[10];
    int index = 0;

    if (value == 0) {
        UARTCharPut(UART0_BASE, '0');
        return;
    }

    while (value > 0) {
        buffer[index++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (index > 0) {
        UARTCharPut(UART0_BASE, buffer[--index]);
    }
}

static void UART0WriteValues(const uint32_t values[ADC_CHANNEL_COUNT])
{
    UART0WriteString("/*");

    for (uint32_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
        UART0WriteUInt(values[i]);

        if (i + 1 < ADC_CHANNEL_COUNT) {
            UARTCharPut(UART0_BASE, ',');
        }
    }

    UART0WriteString("*/\r\n");
}

static void ADC0InitNineInputs(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {
    }
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD)) {
    }
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0)) {
    }

    GPIOPinTypeADC(GPIO_PORTE_BASE,
                   GPIO_PIN_5 | GPIO_PIN_3 | GPIO_PIN_2 |
                   GPIO_PIN_1 | GPIO_PIN_0);
    GPIOPinTypeADC(GPIO_PORTD_BASE,
                   GPIO_PIN_3 | GPIO_PIN_2 | GPIO_PIN_1 | GPIO_PIN_0);

    ADCSequenceDisable(ADC0_BASE, 0);
    ADCSequenceDisable(ADC0_BASE, 1);

    ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH0);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH1);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH2);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_CH3);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 4, ADC_CTL_CH4);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 5, ADC_CTL_CH5);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 6, ADC_CTL_CH6);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 7,
                             ADC_CTL_CH7 | ADC_CTL_IE | ADC_CTL_END);

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
    uint32_t sequence0Values[ADC_SEQUENCE0_COUNT];
    uint32_t sequence1Value;

    ADCProcessorTrigger(ADC0_BASE, 0);
    while (!ADCIntStatus(ADC0_BASE, 0, false)) {
    }
    ADCIntClear(ADC0_BASE, 0);
    ADCSequenceDataGet(ADC0_BASE, 0, sequence0Values);

    ADCProcessorTrigger(ADC0_BASE, 1);
    while (!ADCIntStatus(ADC0_BASE, 1, false)) {
    }
    ADCIntClear(ADC0_BASE, 1);
    ADCSequenceDataGet(ADC0_BASE, 1, &sequence1Value);

    for (uint32_t i = 0; i < ADC_SEQUENCE0_COUNT; i++) {
        values[i] = sequence0Values[i];
    }
    values[8] = sequence1Value;
}

static void ADC0ReadAveraged(uint32_t values[ADC_CHANNEL_COUNT])
{
    uint32_t sums[ADC_CHANNEL_COUNT];
    uint32_t sampleValues[ADC_CHANNEL_COUNT];

    for (uint32_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
        sums[i] = 0;
    }

    for (uint32_t sample = 0; sample < ADC_SAMPLE_COUNT; sample++) {
        ADC0ReadOnce(sampleValues);

        for (uint32_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
            sums[i] += sampleValues[i];
        }
    }

    for (uint32_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
        values[i] = sums[i] / ADC_SAMPLE_COUNT;
    }
}

static void StoreNamedValues(const uint32_t values[ADC_CHANNEL_COUNT])
{
    diode_current_limit = values[0];
    diode_current_actual = values[1];
    diode_temp_set = values[2];
    diode_temp_actual = values[3];
    diode_tec_err = values[4];
    crystal_temp_set = values[5];
    crystal_temp_actual = values[6];
    crystal_tec_err = values[7];
    fault = values[8];
}


// Assume reference voltage is 3.3V and 12-bit ADC (4096 steps)
#define ADC_REF_VOLTAGE 3.3f
#define ADC_MAX_VALUE 4095.0f

// User must set these scaling factors as per hardware
#define DIODE_CURRENT_SCALE 1.0f   // Amps per Volt (set as per your circuit)
#define TEMP_SCALE 0.1f            // deg C per mV

static void ProcessAndBuildOutputValues(uint32_t values[ADC_CHANNEL_COUNT])
{
    // Convert ADC reading to voltage (V)
    float v0 = (diode_current_limit / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    float v1 = (diode_current_actual / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;

    // Diode current (A)
    values[0] = (uint32_t)(v0 * DIODE_CURRENT_SCALE * 1000); // Output in mA for integer
    values[1] = (uint32_t)(v1 * DIODE_CURRENT_SCALE * 1000); // Output in mA for integer

    // Temperature readings (deg C)
    // If ADC input is in mV, convert voltage to mV first
    float t2 = ((diode_temp_set / ADC_MAX_VALUE) * ADC_REF_VOLTAGE) * 1000 * TEMP_SCALE;
    float t3 = ((diode_temp_actual / ADC_MAX_VALUE) * ADC_REF_VOLTAGE) * 1000 * TEMP_SCALE;
    float t5 = ((crystal_temp_set / ADC_MAX_VALUE) * ADC_REF_VOLTAGE) * 1000 * TEMP_SCALE;
    float t6 = ((crystal_temp_actual / ADC_MAX_VALUE) * ADC_REF_VOLTAGE) * 1000 * TEMP_SCALE;
    values[2] = (uint32_t)t2;
    values[3] = (uint32_t)t3;
    values[4] = diode_tec_err;
    values[5] = (uint32_t)t5;
    values[6] = (uint32_t)t6;
    values[7] = crystal_tec_err;

    // Fault: output 0 if below midpoint, else 1
    values[8] = (fault < (ADC_MAX_VALUE / 2)) ? 0 : 1;
}

int main(void)
{
    FPUEnable();
    FPULazyStackingEnable();

    // Set clock to 40 MHz
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);

    // Enable GPIOF
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {
    }

    // Set PF1 as output
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);

    UART0Init();
    ADC0InitNineInputs();

    while(1)
    {
        uint32_t values[ADC_CHANNEL_COUNT];

        ADC0ReadAveraged(values);
        StoreNamedValues(values);

        ProcessAndBuildOutputValues(values);
        UART0WriteValues(values);

        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1,
                     GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_1) ^ GPIO_PIN_1);
        SysCtlDelay(200000);
    }
}
