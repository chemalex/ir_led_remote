# all_remote_protocols

Bare-metal ATmega8 IRMP receiver based on `ATmega8UARTReceiver`. It samples a
TSOP receiver on **PD2** and prints each decoded protocol, address, command,
and flags on **PD1/TXD** at **9600 baud, 8N1**.

The ATmega8 common-protocol profile enables NEC (including NEC16/NEC42),
Samsung, Sony/SIRCS, Kaseikyo, JVC, Matsushita, Denon, and RC5/RC6. Less
common protocols are left disabled to keep the firmware within the ATmega8's
8 KB flash. This is not every decoder in IRMP. The selected sampling rate is
15 kHz; protocols that require a higher rate or wider command storage are
not enabled.

## Connections

- TSOP receiver `OUT` -> ATmega8 `PD2` (physical pin 4 on the 28-pin package)
- USB-to-UART adapter `RX` -> ATmega8 `PD1` (physical pin 3)
- Join the adapter and ATmega8 grounds.
- Use a TTL-level UART adapter; do not connect PD1 directly to RS-232.

## Build and flash

Run these commands from the IRMP library directory:

```sh
avr-g++ -std=gnu++11 -mmcu=atmega8 -DF_CPU=12000000UL \
  -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
  -Isrc examples/all_remote_protocols/all_remote_protocols.cpp \
  -Wl,--gc-sections \
  -o examples/all_remote_protocols/all_remote_protocols.elf

avr-size -C --mcu=atmega8 examples/all_remote_protocols/all_remote_protocols.elf

avr-objcopy -O ihex -R .eeprom \
  examples/all_remote_protocols/all_remote_protocols.elf \
  examples/all_remote_protocols/all_remote_protocols.hex

sudo avrdude -c usbasp -p m8 \
  -U flash:w:examples/all_remote_protocols/all_remote_protocols.hex:i
```

Open the UART terminal at **9600 baud, 8N1**. A decoded frame is printed like:

```text
PROTO=NEC (02) ADDR=0xEF00 CMD=0x000A FLAGS=0x00
```
