# ATmega8 IRMP UART receiver

This bare-metal example samples a TSOP-style IR receiver on **PD2** and prints
each decoded frame on the ATmega8 hardware UART's **TXD / PD1** pin at
**9600 baud, 8 data bits, no parity, 1 stop bit (8N1)**.

It reports the decoded protocol name and number, address, command, and IRMP
flags. Repeated key frames are marked with `REPEAT`.

The source currently enables **only NEC** as a diagnostic, because the captured
remote signal matches NEC timings. This helps rule out interactions between
multiple decoders. To restore the broader compatible profile, change the
`IRMP_ATMEGA8_NEC_ONLY` definition in the source to
`IRMP_ATMEGA8_ALL_IR_PROTOCOLS`.

The broader profile includes the Main 15 protocols and additional protocols
that can coexist with them. FAN, ORTEK, Roomba, RUWIDO, S100, ACP24, Panasonic
Beamer, Mitsubishi Heavy, IRMP16, and RCII are excluded due to conflicts. LEGO
and RCMM require at least 19 kHz sampling; Merlin requires 32-bit command
storage, so those three are disabled in this 15 kHz, 16-bit configuration.

## Important: compile as C++

The current IRMP library implementation is provided by its C++ headers. The
example is therefore a `.cpp` file and must be compiled with `avr-g++`; it
cannot be compiled as a C translation unit with `avr-gcc`.

## Connections

- TSOP receiver `OUT` -> ATmega8 `PD2` (physical pin 4 on the 28-pin package)
- TSOP receiver `VCC` and `GND` -> the supply specified for the receiver
- USB-to-UART adapter `RX` -> ATmega8 `PD1` (physical pin 3 on the 28-pin package)
- USB-to-UART adapter `GND` -> ATmega8 `GND`

Use a **TTL-level UART adapter** compatible with the ATmega8 supply voltage;
do not connect PD1 directly to an RS-232 port. The TSOP output is usually
active-low and idles high, which is the default polarity used by IRMP.

PD2 is sampled by IRMP's Timer2-driven routine; the example does not use the
ATmega8's INT0 interrupt. Because this is a bare-metal (non-Arduino) program,
it explicitly includes `IRTimer.hpp` and calls `initIRTimerForReceive()` after
`irmp_init()` to start Timer2 sampling. Timer2 is reserved for IRMP while this
application runs.

## Build

Run these commands from the IRMP library directory:

```sh
avr-g++ -std=gnu++11 -mmcu=atmega8 -DF_CPU=12000000UL \
  -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
  -Isrc examples/ATmega8UARTReceiver/ATmega8UARTReceiver.cpp \
  -Wl,--gc-sections \
  -o examples/ATmega8UARTReceiver/receiver.elf

avr-objcopy -O ihex -R .eeprom \
  examples/ATmega8UARTReceiver/receiver.elf \
  examples/ATmega8UARTReceiver/receiver.hex
```

The hardware must actually be clocked at 12 MHz, and the AVR clock fuses must
select the matching clock source and disable any unintended divide-by-8
setting. Set the terminal to **9600 baud, 8N1**.

## Example output

```text
ATmega8 IRMP receiver ready
IR input: PD2; UART TXD: PD1; 9600 baud, 8N1
Waiting for TSOP data...
PROTO=NEC (02) ADDR=00FF CMD=0045 FLAGS=00
PROTO=NEC (02) ADDR=00FF CMD=0045 FLAGS=01 REPEAT
```
