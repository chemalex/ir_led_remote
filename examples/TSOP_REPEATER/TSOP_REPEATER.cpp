/*
 * TSOP_REPEATER - ATmega8 IR receiver, decoder, UART logger, and IR repeater.
 *
 * The TSOP receiver is connected to PD2. IRMP samples it from Timer2 at
 * 19 kHz and decodes NEC. Each received frame is printed over the hardware
 * UART on PD1 at 9600 baud, 8N1, then re-transmitted by IRSND on OC2 / PB3.
 *
 * On an ATmega8, Timer2 is shared by IRMP and IRSND. The receive timer is
 * saved before transmit, Timer2 is switched to the IRSND tick rate while the
 * frame is sent, and the receive configuration is restored by the shared ISR
 * when transmission completes.
 *
 * Compile with avr-g++ (IRMP/IRSND are C++ headers) and F_CPU=12000000UL.
 */

/* IRMP native AVR input mapping: TSOP output -> PD2. */
#define IRMP_PORT_LETTER D
#define IRMP_BIT_NUMBER  2

/* Print protocol names, and use NEC only while validating this repeater. */
#define IRMP_PROTOCOL_NAMES 1
#define IRMP_ATMEGA8_NEC_ONLY 1

/* Keep the receive and IRSND protocol timing constants consistent. */
#define F_INTERRUPTS 19000UL
#define IRSND_IR_FREQUENCY 38000UL
#define IRSND_INTERRUPT_FREQUENCY (IRSND_IR_FREQUENCY * 2UL)

/*
 * IRMP and IRSND share Timer2 and therefore need one combined Timer2 ISR.
 * The library's shared-timer mode also skips receive sampling while sending.
 */
#define USE_ONE_TIMER_FOR_IRMP_AND_IRSND
#define IRSND_ATMEGA8_NEC_ONLY 1

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#include <irmp.hpp>
#include <irsnd.hpp>
#include <IRTimer.hpp>

#if !defined(F_CPU) || (F_CPU != 12000000UL)
#error "Build with -DF_CPU=12000000UL for a 12 MHz ATmega8"
#endif

#define UART_BAUD_RATE 9600UL
#define UART_UBRR_VALUE ((F_CPU / (16UL * UART_BAUD_RATE)) - 1UL)

/* Data returned by IRMP and the copy passed to IRSND. */
static IRMP_DATA received_data;
static IRMP_DATA transmit_data;

/* Send one byte over the hardware UART. */
static void uart_putc(char value)
{
    while ((UCSRA & _BV(UDRE)) == 0)
    {
        /* Wait until UDR can accept another byte. */
    }

    UDR = (uint8_t)value;
}

/* Send an SRAM string over UART. */
static void uart_puts(const char *text)
{
    while (*text != '\0')
    {
        uart_putc(*text++);
    }
}

/* Send a flash-resident string over UART. */
static void uart_puts_P(PGM_P text)
{
    char value;

    while ((value = (char)pgm_read_byte(text++)) != '\0')
    {
        uart_putc(value);
    }
}

/* Print one hexadecimal digit. */
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

/* Set up 9600 baud, 8 data bits, no parity, and one stop bit. */
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

/* Print a decoded frame and indicate whether it is an NEC repeat frame. */
static void print_received_frame(const IRMP_DATA *data)
{
    PGM_P protocol_name = (PGM_P)pgm_read_word(&irmp_protocol_names[data->protocol]);

    uart_puts("RX PROTO=");
    uart_puts_P(protocol_name);
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

/*
 * Reconfigure Timer2 for IRSND's 76 kHz service interrupt. With OC2 toggling
 * at each compare match, the carrier on PB3 is about 38 kHz. The current IRMP
 * timer state must already have been saved by storeIRTimer().
 */
static void timer2_start_transmit(void)
{
    TCCR2 = _BV(WGM21) | _BV(CS20); /* CTC mode, clock divided by 1. */
    /* Match IRSND's integer 38 kHz compare calculation exactly. */
    OCR2 = (uint8_t)((F_CPU / IRSND_INTERRUPT_FREQUENCY) - 1UL);
    TCNT2 = 0;

    /* ATmega8 clears timer flags by writing a one to each flag bit. */
    TIFR = _BV(OCF2) | _BV(TOV2);
    TIMSK = _BV(OCIE2);
}

/*
 * Copy a decoded NEC frame and begin sending it. A decoded NEC repeat marker
 * is translated to IRSND's raw-repeat flag so held buttons remain held at the
 * target device rather than retransmitting the whole data frame each time.
 */
static void repeat_received_frame(const IRMP_DATA *data)
{
    transmit_data.protocol = data->protocol;
    transmit_data.address = data->address;
    transmit_data.command = data->command;
    transmit_data.flags = 0;

    if ((data->flags & IRMP_FLAG_REPETITION) != 0)
    {
        transmit_data.flags = IRSND_RAW_REPETITION_FRAME;
    }

    /*
     * The ISR must not run between starting IRSND and changing Timer2 into
     * transmit mode, or it could advance the frame using receive timing.
     */
    cli();
    if (irsnd_send_data(&transmit_data, FALSE))
    {
        storeIRTimer();
        timer2_start_transmit();
        sei();

        uart_puts("TX started on PB3\r\n");
    }
    else
    {
        sei();
        uart_puts("TX error: IRSND does not support this protocol or is busy\r\n");
    }
}

int main(void)
{
    uart_init();

    /*
     * Initialize IRSND first to configure its output on OC2 / PB3, then let
     * IRMP configure Timer2 for receive sampling. Sending reconfigures Timer2
     * temporarily and the shared ISR restores this receive timer afterward.
     */
    irsnd_init();
    irmp_init();
    initIRTimerForReceive();
    sei();

    uart_puts_P(PSTR("\r\nATmega8 TSOP repeater ready\r\n"));
    uart_puts_P(PSTR("RX: PD2; TX IR LED: PB3/OC2; UART: PD1, 9600 baud, 8N1\r\n"));
    uart_puts_P(PSTR("Protocol: NEC; IR carrier: about 38 kHz\r\n"));
    uart_puts_P(PSTR("Waiting for TSOP data...\r\n"));

    for (;;)
    {
        if (irmp_get_data(&received_data))
        {
            print_received_frame(&received_data);

            if (received_data.protocol == IRMP_NEC_PROTOCOL)
            {
                repeat_received_frame(&received_data);
            }
            else
            {
                uart_puts("TX skipped: this build only sends NEC\r\n");
            }
        }
    }
}
