# TSOP_REPEATER

Bare-metal ATmega8 NEC repeater. It receives NEC with IRMP on **PD2**, prints
decoded frames over **PD1/TXD at 9600 baud, 8N1**, and retransmits them with
IRSND through **OC2 on PB3**. PB3 is physical pin 17 on the 28-pin ATmega8.

The program retransmits full NEC frames and translates IRMP repeat flags into
IRSND raw NEC repeat frames. The receive and transmit sides share Timer2:
sampling pauses while a frame is transmitted, then IRMP's receive timer is
restored automatically.

Connect the IR LED to PB3 through a suitable current-limiting transistor driver
and resistor; do not drive a high-current IR LED directly from the MCU pin.
Keep the TSOP output on PD2 and use a TTL-level UART adapter on PD1. Join the
adapter and ATmega8 grounds.

Build and flash from the IRMP library directory:

```sh
avr-g++ -std=gnu++11 -mmcu=atmega8 -DF_CPU=12000000UL \
  -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
  -Isrc examples/TSOP_REPEATER/TSOP_REPEATER.cpp \
  -Wl,--gc-sections \
  -o examples/TSOP_REPEATER/TSOP_REPEATER.elf

avr-size -C --mcu=atmega8 examples/TSOP_REPEATER/TSOP_REPEATER.elf

avr-objcopy -O ihex -R .eeprom \
  examples/TSOP_REPEATER/TSOP_REPEATER.elf \
  examples/TSOP_REPEATER/TSOP_REPEATER.hex

sudo avrdude -c usbasp -p m8 \
  -U flash:w:examples/TSOP_REPEATER/TSOP_REPEATER.hex:i
```

Open the UART terminal at **9600 baud, 8N1**. The build uses a 19 kHz IRMP
sampling rate and a nominal 38 kHz transmit carrier.
