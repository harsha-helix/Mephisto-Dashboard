CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy
TIVAWARE=C:/ti/TivaWare_C_Series-2.2.0.295

CFLAGS=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O0 -g -I$(TIVAWARE) -DPART_TM4C123GH6PM -DTARGET_IS_TM4C123_RB1
LDFLAGS=-T linker.ld -nostdlib -Wl,--gc-sections
LDLIBS=-L$(TIVAWARE)/driverlib/gcc -ldriver -lgcc

all: main.elf

main.elf: main.c startup_gcc.c $(TIVAWARE)/driverlib/gcc/libdriver.a
	$(CC) $(CFLAGS) $(LDFLAGS) main.c startup_gcc.c $(LDLIBS) -o $@

flash:
	openocd -s C:\OpenOCD-20260302-0.12.0\scripts -f board/ti/ek-tm4c123gxl.cfg -c "program main.elf verify reset exit"

clean:
	del *.elf
