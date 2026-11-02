# test_TSOP: raw TSOP edge diagnostic

This bare-metal ATmega8 test does not decode IR protocols. It records both
edges of the TSOP output on **PD2 / INT0**, measures the duration of each
high/low level using Timer1, and sends the measurements over **PD1 / TXD at
9600 baud, 8N1** after the input has been quiet for 10 ms.

The output uses `L=... us` for a low period and `H=... us` for a high period.
TSOP modules normally idle high and produce low-going pulses while receiving
infrared bursts. Point a remote at the receiver and press a button; expected
output is a sequence of low and high durations, generally hundreds to
thousands of microseconds.

This test uses the ATmega8's INT0 and Timer1 peripherals. It does not use
IRMP's protocol timer (Timer2), so it can be built and tested independently
of the protocol receiver. IRMP's `IRMP_MEASURE_TIMING` option measures the
duration of IRMP's own timer ISR by toggling a test pin; it does not print raw
TSOP input timing.

## Connections

- TSOP `OUT` -> ATmega8 `PD2` (physical pin 4 on the 28-pin package)
- TSOP `VCC` and `GND` -> the supply specified for the receiver
- USB-to-UART adapter `RX` -> ATmega8 `PD1` (physical pin 3 on the 28-pin package)
- USB-to-UART adapter `GND` -> ATmega8 `GND`

Use a TTL-level adapter compatible with the ATmega8 supply voltage; do not
connect PD1 directly to an RS-232 port.

## Build

Run from the IRMP library directory:

```sh
avr-g++ -std=gnu++11 -mmcu=atmega8 -DF_CPU=12000000UL \
  -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
  examples/test_TSOP/test_TSOP.cpp \
  -Wl,--gc-sections \
  -o examples/test_TSOP/test_TSOP.elf

avr-size -C --mcu=atmega8 examples/test_TSOP/test_TSOP.elf

avr-objcopy -O ihex -R .eeprom \
  examples/test_TSOP/test_TSOP.elf \
  examples/test_TSOP/test_TSOP.hex
```

Flash `examples/test_TSOP/test_TSOP.hex` with the programmer used for your
board. The crystal and clock fuse configuration must match 12 MHz. Open the
UART terminal at **9600 baud, 8N1**.

If a remote is pressed and the output remains only at `Waiting for TSOP data`,
check the TSOP supply, ground, pin order, and that its output is connected to
PD2. If transitions appear here but the protocol receiver does not decode
them, the TSOP is responding and the issue is more likely signal strength,
protocol support, or decoder timing.
