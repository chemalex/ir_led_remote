/*
 * ATmega8 IRMP UART receiver with a broad set of IR protocol decoders.
 *
 * The TSOP-style receiver is connected to PD2. IRMP samples that pin from
 * its Timer2 interrupt. Decoded protocol, address, command, and flags are
 * printed on the hardware UART TXD pin (PD1) at 9600 baud.
 *
 * IRMP is configured with the common ATmega8 profile. Less common protocols
 * remain disabled to keep the firmware within the ATmega8's 8 KB flash.
 */

/* Configure IRMP's native AVR input mapping before its config is included. */
#define IRMP_PORT_LETTER D
#define IRMP_BIT_NUMBER  2

/* Add protocol-name strings to the binary for readable UART output. */
#define IRMP_PROTOCOL_NAMES 1

/* Select common, compatible IR decoders from irmpconfig.h. */
#define IRMP_ATMEGA8_COMMON_PROTOCOLS 1

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#include <irmp.hpp>
/*
 * In bare-metal builds irmp_init() sets up the input but does not start
 * sampling. Start IRMP's Timer2 interrupt explicitly after initialization.
 */
#include <IRTimer.hpp>

#if !defined(F_CPU) || (F_CPU != 12000000UL)
#error "Build with -DF_CPU=12000000UL for a 12 MHz ATmega8"
#endif

#define UART_BAUD_RATE 9600UL
#define UART_UBRR_VALUE ((F_CPU / (16UL * UART_BAUD_RATE)) - 1UL)

/* Send one byte over the ATmega8 hardware UART. */
static void uart_putc(char value)
{
    while ((UCSRA & _BV(UDRE)) == 0)
    {
        /* Wait until UDR can accept another byte. */
    }

    UDR = (uint8_t)value;
}

/* Send a zero-terminated SRAM string. */
static void uart_puts(const char *text)
{
    while (*text != '\0')
    {
        uart_putc(*text++);
    }
}

/* Send a zero-terminated string stored in program memory. */
static void uart_puts_P(PGM_P text)
{
    char value;

    while ((value = (char)pgm_read_byte(text++)) != '\0')
    {
        uart_putc(value);
    }
}

/* Print the low nibble as one uppercase hexadecimal digit. */
static void uart_put_hex_digit(uint8_t value)
{
    value &= 0x0F;
    uart_putc((char)(value < 10 ? ('0' + value) : ('A' + value - 10)));
}

/* Print an 8-bit value as two hexadecimal digits. */
static void uart_put_hex8(uint8_t value)
{
    uart_put_hex_digit(value >> 4);
    uart_put_hex_digit(value);
}

/* Print a 16-bit value as four hexadecimal digits. */
static void uart_put_hex16(uint16_t value)
{
    uart_put_hex_digit((uint8_t)(value >> 12));
    uart_put_hex_digit((uint8_t)(value >> 8));
    uart_put_hex_digit((uint8_t)(value >> 4));
    uart_put_hex_digit((uint8_t)value);
}

/* Print the readable protocol name from IRMP's flash-resident name table. */
static void uart_print_protocol(uint8_t protocol)
{
    if (protocol <= IRMP_N_PROTOCOLS)
    {
        PGM_P name = (PGM_P)pgm_read_word(&irmp_protocol_names[protocol]);
        uart_puts_P(name);
    }
    else
    {
        uart_puts("UNKNOWN");
    }
}

/* Configure the hardware UART: 9600 baud, 8 data bits, no parity, 1 stop bit. */
static void uart_init(void)
{
    UCSRB = _BV(TXEN);
    UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);
    UCSRA = 0;
    UBRRH = (uint8_t)(UART_UBRR_VALUE >> 8);
    UBRRL = (uint8_t)UART_UBRR_VALUE;

    /* UART TXD is PD1. */
    DDRD |= _BV(PD1);
}

/* Print one decoded frame in a line-oriented UART format. */
static void print_ir_data(const IRMP_DATA *data)
{
    uart_puts("PROTO=");
    uart_print_protocol(data->protocol);
    uart_puts(" (");
    uart_put_hex8(data->protocol);
    uart_puts(") ADDR=0x");
    uart_put_hex16(data->address);
    uart_puts(" CMD=0x");
    uart_put_hex16(data->command);
    uart_puts(" FLAGS=0x");
    uart_put_hex8(data->flags);

    if ((data->flags & IRMP_FLAG_REPETITION) != 0)
    {
        uart_puts(" REPEAT");
    }

    uart_puts("\r\n");
}

int main(void)
{
    IRMP_DATA received_data;

    uart_init();
    irmp_init();
    initIRTimerForReceive();
    sei();

    uart_puts_P(PSTR("\r\nATmega8 IRMP broad-protocol receiver ready\r\n"));
    uart_puts_P(PSTR("IR input: PD2; UART TXD: PD1; 9600 baud, 8N1\r\n"));
    uart_puts_P(PSTR("IRMP sampling: 15 kHz; compatible IR protocols enabled\r\n"));
    uart_puts_P(PSTR("Waiting for TSOP data...\r\n"));

    for (;;)
    {
        if (irmp_get_data(&received_data))
        {
            print_ir_data(&received_data);
        }
    }
}
