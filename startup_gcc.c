#include <stdint.h>

extern int main(void);

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

void Default_Handler(void) {
    while (1) {
    }
}

void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata) {
        *dst++ = *src++;
    }

    for (dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0;
    }

    main();

    while (1) {
    }
}

__attribute__ ((section(".isr_vector")))
const uintptr_t vector_table[] = {
    (uintptr_t)&_estack,
    (uintptr_t)Reset_Handler,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
    0,
    0,
    0,
    0,
    0,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
    0,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
};
