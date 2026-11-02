# TSOP_REP_MAPPING

ATmega8 IR repeater with a Philips universal remote mapping table. IRMP
receives NEC on **PD2**. The universal remote's NEC commands at address
`0x0004` are looked up in the table and translated to target NEC frames sent
through **PB3/OC2**. Other NEC frames, including the target remote's address
`0xEF00`, pass through unchanged. The UART on **PD1** (9600 baud, 8N1) prints
the received frame, the source-to-target mapping, and the transmit status.

The mapping values are based on `/home/alex/ir_send_rcv/mapping.cpp`. The
duplicate source entry `ADDR=0x0004 CMD=0x0020` was resolved as target
`CMD=0x0002` (OFF); the conflicting DOWN mapping to `0x0001` was omitted.
The map assumes the Philips universal remote is decoded as **NEC** with
address `0x0004`, as shown by the received UART output. Check the UART
`RX PROTO`, address, and command values if commands do not match.

## Connections

- TSOP receiver `OUT` -> ATmega8 `PD2` (physical pin 4 on the 28-pin package)
- IR LED driver input -> `PB3/OC2` (physical pin 17)
- USB-to-UART adapter `RX` -> `PD1/TXD` (physical pin 3)
- Join all grounds. Use a current-limiting resistor and transistor driver for
  the IR LED; do not drive a high-current LED directly from PB3.

## Build and flash

Run these commands from the IRMP library directory:

```sh
avr-g++ -std=gnu++11 -mmcu=atmega8 -DF_CPU=12000000UL \
  -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
  -Isrc examples/TSOP_REP_MAPPING/TSOP_REP_MAPPING.cpp \
  -Wl,--gc-sections \
  -o examples/TSOP_REP_MAPPING/TSOP_REP_MAPPING.elf

avr-size -C --mcu=atmega8 examples/TSOP_REP_MAPPING/TSOP_REP_MAPPING.elf

avr-objcopy -O ihex -R .eeprom \
  examples/TSOP_REP_MAPPING/TSOP_REP_MAPPING.elf \
  examples/TSOP_REP_MAPPING/TSOP_REP_MAPPING.hex

sudo avrdude -c usbasp -p m8 \
  -U flash:w:examples/TSOP_REP_MAPPING/TSOP_REP_MAPPING.hex:i
```

For a mapped key, the UART prints `MAP SRC=NEC ... -> TX=NEC ...`, confirming
both the remote code received and the new NEC code selected for transmission.
