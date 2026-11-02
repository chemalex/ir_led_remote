/*
 * ATmega8 UART IR receiver using IRMP.
 *
 * The TSOP-style receiver is connected to PD2. IRMP samples that pin from
 * its Timer2 interrupt; PD2 does not need to be configured as INT0.
 *
 * Decoded protocol, address, command, and flags are printed on the ATmega8
 * hardware UART at 9600 baud. The UART TXD signal is PD1.
 *
 * IRMP 3.x is implemented in C++ headers, so this file must be compiled with
 * avr-g++, not avr-gcc. The application itself uses only AVR register APIs.
 */

/* Set IRMP's native AVR input mapping before its configuration is included. */
#define IRMP_PORT_LETTER D
#define IRMP_BIT_NUMBER  2

/* Include protocol-name strings so the protocol is printed as text. */
#define IRMP_PROTOCOL_NAMES 1

/*
 * The captured signal matches NEC very closely. Test with NEC alone first,
 * to rule out interaction with other protocol decoders.
 * Define IRMP_ATMEGA8_ALL_IR_PROTOCOLS instead to restore the broad profile.
 */
#define IRMP_ATMEGA8_NEC_ONLY 1
// #define IRMP_ATMEGA8_ALL_IR_PROTOCOLS 1

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#include <irmp.hpp>
/*
 * The non-Arduino IRMP initialization configures the input pin but does not
 * start the sampling timer. Include the timer implementation explicitly and
 * call initIRTimerForReceive() after irmp_init().
 */
#include <IRTimer.hpp>

/* The crystal and build-time clock definition must both be 12 MHz. */
#if !defined(F_CPU) || (F_CPU != 12000000UL)
#error "Build with -DF_CPU=12000000UL for a 12 MHz ATmega8"
#endif

#define UART_BAUD_RATE 9600UL

/*
 * In asynchronous normal-speed mode, baud = F_CPU / (16 * (UBRR + 1)).
 * UBRR=77 gives about 9615 baud at 12 MHz (approximately +0.16% error).
 */
#define UART_UBRR_VALUE ((F_CPU / (16UL * UART_BAUD_RATE)) - 1UL)

/* Send one byte and wait until the UART data register can accept it. */
static void uart_putc(char value)
{
    while ((UCSRA & _BV(UDRE)) == 0)
    {
        /* Wait for the previous character to finish moving into the shift register. */
    }

    UDR = (uint8_t)value;
}

/* Send a zero-terminated string stored in SRAM. */
static void uart_puts(const char *text)
{
    while (*text != '\0')
    {
        uart_putc(*text++);
    }
}

/* Send a zero-terminated string stored in flash (program memory). */
static void uart_puts_P(PGM_P text)
{
    char value;

    while ((value = (char)pgm_read_byte(text++)) != '\0')
    {
        uart_putc(value);
    }
}

/* Print the low four bits as one uppercase hexadecimal digit. */
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

/* Print a 16-bit value as four hexadecimal digits, most significant first. */
static void uart_put_hex16(uint16_t value)
{
    uart_put_hex_digit((uint8_t)(value >> 12));
    uart_put_hex_digit((uint8_t)(value >> 8));
    uart_put_hex_digit((uint8_t)(value >> 4));
    uart_put_hex_digit((uint8_t)value);
}

/* Print the protocol name IRMP decoded from the flash-resident name table. */
static void uart_print_protocol(uint8_t protocol)
{
    if (protocol <= IRMP_N_PROTOCOLS)
    {
        /*
         * Both the pointer table and the protocol strings live in flash on AVR:
         * first read the pointer from the table, then read the string via pgmspace.
         */
        PGM_P name = (PGM_P)pgm_read_word(&irmp_protocol_names[protocol]);
        uart_puts_P(name);
    }
    else
    {
        /* Defensive fallback in case data is corrupted or the table changes. */
        uart_puts("UNKNOWN");
    }
}

/* Configure the ATmega8 USART for 9600 baud, 8 data bits, no parity, 1 stop bit. */
static void uart_init(void)
{
    /* Enable the hardware transmitter; the receiver is not needed here. */
    UCSRB = _BV(TXEN);

    /* ATmega8 needs URSEL set to write the UCSRC register. */
    UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);

    /* Use normal asynchronous speed and set the calculated baud divisor. */
    UCSRA = 0;
    UBRRH = (uint8_t)(UART_UBRR_VALUE >> 8);
    UBRRL = (uint8_t)UART_UBRR_VALUE;

    /* PD1 is the ATmega8 hardware UART TXD output. */
    DDRD |= _BV(PD1);
}

/* Print one decoded frame in a line-oriented format suitable for a terminal. */
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

    /* Initialize the UART before sending any diagnostic text. */
    uart_init();

    /* The receiver's output is normally active-low; configure IRMP and Timer2. */
    irmp_init();

    /* Start IRMP's 15 kHz Timer2 sampling interrupt in bare-metal mode. */
    initIRTimerForReceive();

    /* Allow the Timer2 interrupt to call irmp_ISR() and sample PD2. */
    sei();

    uart_puts("\r\nATmega8 IRMP receiver ready\r\n");
    uart_puts("IR input: PD2; UART TXD: PD1; 9600 baud, 8N1\r\n");
    uart_puts("Waiting for TSOP data...\r\n");

    for (;;)
    {
        /*
         * irmp_ISR() runs from Timer2 at the configured sample rate.
         * irmp_get_data() copies a completed decode out of IRMP's shared buffer.
         * Printing here, outside interrupt context, keeps the sample ISR brief.
         */
        if (irmp_get_data(&received_data))
        {
            print_ir_data(&received_data);
        }
    }
}
