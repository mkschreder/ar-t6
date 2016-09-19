SUBDIRS=peripherals system

bin_PROGRAMS=ar-t6-firmware
ar_t6_firmware_SOURCES=eeprom.c gui.c icons.c keypad.c lcd.c main.c mixer.c pulses.c sound.c sticks.c strings.c tasks.c 
ar_t6_firmware_CFLAGS=$(LIBSTM32F10X_CFLAGS) -std=c99 
ar_t6_firmware_LDFLAGS=$(LIBSTM32F10X_LIBS) -lc -lgcc
ar_t6_firmware_disabled_CFLAGS=$(CODE_COVERAGE_CFLAGS) -std=gnu99 -Wall -Werror -Wno-format-y2k -W -Wstrict-prototypes -Wmissing-prototypes \
-Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch \
-Wshadow -Wcast-align -Wchar-subscripts -Winline \
-Wnested-externs -Wredundant-decls -Wmissing-field-initializers -Wextra \
-Wformat=2 -Wno-format-nonliteral -Wpointer-arith -Wno-missing-braces \
-Wno-unused-parameter -Wno-unused-variable -Wno-inline

