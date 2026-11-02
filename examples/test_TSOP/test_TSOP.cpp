/*
 * test_TSOP.cpp - bare-metal ATmega8 TSOP signal diagnostic.
 *
 * This test does not use IRMP's protocol decoder. It captures every logical
 * transition on PD2 with INT0 and measures the time between transitions with
 * Timer1. After the input has been quiet for 10 ms, it prints the captured
 * low/high durations over the hardware UART on PD1 (9600 baud, 8N1).
 *
 * TSOP receivers normally idle HIGH and drive LOW while receiving the
 * demodulated IR carrier. Therefore "L" in the output is an active TSOP mark.
 *
 * IRMP_MEASURE_TIMING is not a raw TSOP waveform logger: that option toggles
 * a test output around IRMP's decoder ISR to measure ISR execution time.
 * This independent edge-capture test is intended to establish whether the
 * TSOP output changes at all when a remote control is used.
 *
 * Build this .cpp file with avr-g++ for an ATmega8 clocked at 12 MHz.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#if !defined(F_CPU) || (F_CPU != 12000000UL)
#error "Build with -DF_CPU=12000000UL for a 12 MHz ATmega8"
#endif

#define UART_BAUD_RATE       9600UL
#define UART_UBRR_VALUE      ((F_CPU / (16UL * UART_BAUD_RATE)) - 1UL)

/* Timer1 ticks at F_CPU / 8 = 1.5 MHz, or 2/3 microseconds per tick. */
#define TIMER1_PRESCALER     8UL
#define QUIET_TIMEOUT_TICKS  ((F_CPU / TIMER1_PRESCALER) / 100UL)
#define CAPTURE_CAPACITY     96U

/*
 * Each capture entry describes the input level and its duration up to the
 * following transition. Ticks are converted to microseconds when printed.
 */
typedef struct
{
    uint16_t ticks;
    uint8_t level;
} CaptureEntry;

/* Written by INT0 and read by main; volatile because it is shared with ISR. */
static volatile CaptureEntry captures[CAPTURE_CAPACITY];
static volatile uint8_t capture_count = 0;
static volatile uint8_t capture_overflow = 0;
static volatile uint16_t last_edge_ticks = 0;
static volatile uint8_t previous_level = 1;

/* Send one byte over the hardware UART, waiting for the transmit buffer. */
static void uart_putc(char value)
{
    while ((UCSRA & _BV(UDRE)) == 0)
    {
        /* Wait until UDR is ready for the next byte. */
    }

    UDR = (uint8_t)value;
}

/* Send a zero-terminated string stored in program flash. */
static void uart_puts_P(PGM_P text)
{
    char value;

    while ((value = (char)pgm_read_byte(text++)) != '\0')
    {
        uart_putc(value);
    }
}

/* Print a 16-bit unsigned number in decimal without printf/stdio overhead. */
static void uart_put_u16(uint16_t value)
{
    char digits[5];
    uint8_t count = 0;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (value != 0U);

    while (count != 0U)
    {
        uart_putc(digits[--count]);
    }
}

/* Initialize asynchronous UART TX at 9600 baud, 8 data bits, no parity, 1 stop. */
static void uart_init(void)
{
    /* Enable transmitter; no UART receive functionality is needed for this test. */
    UCSRB = _BV(TXEN);

    /* URSEL is required when writing UCSRC on the ATmega8. */
    UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);

    /* Configure normal-speed asynchronous baud rate. */
    UCSRA = 0;
    UBRRH = (uint8_t)(UART_UBRR_VALUE >> 8);
    UBRRL = (uint8_t)UART_UBRR_VALUE;

    /* PD1 is the ATmega8 hardware UART TXD pin. */
    DDRD |= _BV(PD1);
}

/*
 * INT0 fires on both edges of PD2. Record the level that lasted until this
 * transition and the elapsed Timer1 ticks since the preceding transition.
 * The ISR does no UART work, keeping transition capture short and reliable.
 */
ISR(INT0_vect)
{
    uint16_t now = TCNT1;
    uint16_t elapsed = (uint16_t)(now - last_edge_ticks);
    uint8_t new_level = (PIND & _BV(PD2)) ? 1U : 0U;

    if (capture_count < CAPTURE_CAPACITY)
    {
        captures[capture_count].ticks = elapsed;
        captures[capture_count].level = previous_level;
        capture_count++;
    }
    else
    {
        /* Mark that this burst exceeded the fixed RAM buffer. */
        capture_overflow = 1;
    }

    last_edge_ticks = now;
    previous_level = new_level;
}

/*
 * Snapshot the capture buffer while INT0 is disabled, then print it. INT0 is
 * re-enabled after printing; a new baseline is taken to prevent the UART
 * output time from being counted as one long input pulse.
 */
static void print_capture_burst(void)
{
    CaptureEntry local_captures[CAPTURE_CAPACITY];
    uint8_t local_count;
    uint8_t local_overflow;
    uint8_t i;
    uint8_t saved_sreg = SREG;

    cli();
    GICR &= (uint8_t)~_BV(INT0);

    local_count = capture_count;
    local_overflow = capture_overflow;

    for (i = 0; i < local_count; i++)
    {
        local_captures[i].ticks = captures[i].ticks;
        local_captures[i].level = captures[i].level;
    }

    /* Clear the old burst and establish a fresh idle/input baseline. */
    capture_count = 0;
    capture_overflow = 0;
    last_edge_ticks = TCNT1;
    previous_level = (PIND & _BV(PD2)) ? 1U : 0U;
    GIFR = _BV(INTF0);

    GICR |= _BV(INT0);
    SREG = saved_sreg;

    uart_puts_P(PSTR("\r\nEdges captured: "));
    uart_put_u16(local_count);
    uart_puts_P(PSTR("\r\n"));

    for (i = 0; i < local_count; i++)
    {
        /* Convert 1.5 MHz timer ticks into rounded microseconds. */
        uint16_t microseconds =
                (uint16_t)(((uint32_t)local_captures[i].ticks * 2UL + 1UL) / 3UL);

        uart_putc(local_captures[i].level ? 'H' : 'L');
        uart_putc('=');
        uart_put_u16(microseconds);
        uart_puts_P(PSTR(" us\r\n"));
    }

    if (local_overflow)
    {
        uart_puts_P(PSTR("WARNING: capture buffer full; remaining edges were dropped.\r\n"));
    }

    uart_puts_P(PSTR("Waiting for TSOP data...\r\n"));
}

int main(void)
{
    uint8_t saved_sreg;

    uart_init();

    /* PD2 is a digital input; enable its internal pull-up for a defined idle. */
    DDRD &= (uint8_t)~_BV(PD2);
    PORTD |= _BV(PD2);

    /* Configure Timer1 as a free-running counter at 12 MHz / 8 = 1.5 MHz. */
    TCCR1A = 0;
    TCCR1B = _BV(CS11);
    TCNT1 = 0;

    /* Initialize capture state from the TSOP's current output level. */
    previous_level = (PIND & _BV(PD2)) ? 1U : 0U;
    last_edge_ticks = TCNT1;

    /* INT0 is on PD2: ISC01:ISC00 = 01 selects any logical change. */
    MCUCR = (MCUCR & (uint8_t)~(_BV(ISC01) | _BV(ISC00))) | _BV(ISC00);
    GIFR = _BV(INTF0);
    GICR |= _BV(INT0);

    uart_puts_P(PSTR("\r\nATmega8 TSOP edge test\r\n"));
    uart_puts_P(PSTR("IR input: PD2 / INT0; UART TXD: PD1; 9600 baud, 8N1\r\n"));
    uart_puts_P(PSTR("Timer1 resolution: about 0.67 us per tick\r\n"));
    uart_puts_P(PSTR("Aim a remote at the TSOP and press a button.\r\n"));
    uart_puts_P(PSTR("Waiting for TSOP data...\r\n"));

    sei();

    for (;;)
    {
        uint16_t now;
        uint16_t edge_time;
        uint8_t edges;
        uint8_t saved_sreg_snapshot = SREG;

        /* Take a consistent snapshot of the 16-bit timestamp and edge count. */
        cli();
        now = TCNT1;
        edge_time = last_edge_ticks;
        edges = capture_count;
        SREG = saved_sreg_snapshot;

        /*
         * Once at least one transition was seen, a 10 ms quiet period ends
         * the burst. Unsigned subtraction remains correct across Timer1 wrap.
         */
        if (edges != 0U && (uint16_t)(now - edge_time) >= QUIET_TIMEOUT_TICKS)
        {
            print_capture_burst();
        }
    }
}
