/*
 * TSOP_REP_MAPPING - ATmega8 receiver with NEC-to-NEC command mapping.
 *
 * A TSOP receiver is connected to PD2. NEC commands from the universal remote
 * (address 0x0004) are looked up in the mapping table and retransmitted as NEC
 * commands for the target device (address 0xEF00). Other NEC frames pass
 * through unchanged. All received and mapped values are reported on UART TXD
 * (PD1) at 9600 baud, 8N1.
 */

/* IRMP native AVR input mapping: TSOP output -> PD2. */
#define IRMP_PORT_LETTER D
#define IRMP_BIT_NUMBER  2

#define IRMP_PROTOCOL_NAMES 1
#define IRMP_ATMEGA8_NEC_ONLY 1

/* Receive at 19 kHz; transmit the NEC carrier at about 38 kHz. */
#define F_INTERRUPTS 19000UL
#define IRSND_IR_FREQUENCY 38000UL
#define IRSND_INTERRUPT_FREQUENCY (IRSND_IR_FREQUENCY * 2UL)

/* IRMP receive and IRSND transmit share Timer2 on this MCU. */
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

typedef struct
{
    uint16_t source_address;
    uint16_t source_command;
    uint16_t target_address;
    uint16_t target_command;
} ir_mapping_t;

/*
 * Philips universal remote mappings from mapping.cpp.
 * The duplicated source 0x0004/0x0020 entry was resolved as OFF -> 0x0002.
 */
static const ir_mapping_t ir_mappings[] PROGMEM =
{
    { 0x0004, 0x0008, 0xEF00, 0x0003 }, /* ON/OFF -> ON */
    { 0x0004, 0x0020, 0xEF00, 0x0002 }, /* OFF */
    { 0x0004, 0x0000, 0xEF00, 0x0000 }, /* UP */
    { 0x0004, 0x0001, 0xEF00, 0x0001 }, /* DOWN */
    { 0x0004, 0x0011, 0xEF00, 0x0004 }, /* 1 -> red */
    { 0x0004, 0x0014, 0xEF00, 0x0008 }, /* 4 -> orange */
    { 0x0004, 0x0017, 0xEF00, 0x000C }, /* 7 -> light orange */
    { 0x0004, 0x000B, 0xEF00, 0x0014 }, /* A/V -> yellow */
    { 0x0004, 0x0012, 0xEF00, 0x0005 }, /* 2 -> green */
    { 0x0004, 0x0015, 0xEF00, 0x0009 }, /* 5 -> light green */
    { 0x0004, 0x0018, 0xEF00, 0x000D }, /* 8 -> turquoise */
    { 0x0004, 0x0010, 0xEF00, 0x0015 }, /* 0 -> blue turquoise */
    { 0x0004, 0x0013, 0xEF00, 0x0006 }, /* 3 -> blue */
    { 0x0004, 0x0016, 0xEF00, 0x000A }, /* 6 -> light purple */
    { 0x0004, 0x0019, 0xEF00, 0x000E }, /* 9 -> purple */
    { 0x0004, 0x0053, 0xEF00, 0x0016 }, /* * -> light purple */
    { 0x0004, 0x001A, 0xEF00, 0x000F }, /* # -> white */
    { 0x0004, 0x0009, 0xEF00, 0x0007 }, /* Mute -> white */
    { 0x0004, 0x0079, 0xEF00, 0x000F }, /* Menu -> strobe */
    { 0x0004, 0x0002, 0xEF00, 0x0013 }  /* Menu -> fade */
};

#define IR_MAPPING_COUNT (sizeof(ir_mappings) / sizeof(ir_mappings[0]))

static IRMP_DATA received_data;
static IRMP_DATA transmit_data;

/* Send one byte over the hardware UART. */
static void uart_putc(char value)
{
    while ((UCSRA & _BV(UDRE)) == 0)
    {
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

static void uart_put_hex_digit(uint8_t value)
{
    value &= 0x0F;
    uart_putc((char)(value < 10 ? ('0' + value) : ('A' + value - 10)));
}

static void uart_put_hex8(uint8_t value)
{
    uart_put_hex_digit(value >> 4);
    uart_put_hex_digit(value);
}

static void uart_put_hex16(uint16_t value)
{
    uart_put_hex_digit((uint8_t)(value >> 12));
    uart_put_hex_digit((uint8_t)(value >> 8));
    uart_put_hex_digit((uint8_t)(value >> 4));
    uart_put_hex_digit((uint8_t)value);
}

/* Configure UART TXD (PD1) for 9600 baud, 8N1. */
static void uart_init(void)
{
    UCSRB = _BV(TXEN);
    UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);
    UCSRA = 0;
    UBRRH = (uint8_t)(UART_UBRR_VALUE >> 8);
    UBRRL = (uint8_t)UART_UBRR_VALUE;
    DDRD |= _BV(PD1);
}

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

/* Log the original received protocol and code. */
static void print_received_frame(const IRMP_DATA *data)
{
    uart_puts("RX PROTO=");
    uart_print_protocol(data->protocol);
    uart_puts(" ADDR=0x");
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

/* Find a NEC input code in the mapping table stored in flash. */
static uint8_t find_mapping(const IRMP_DATA *source, IRMP_DATA *target)
{
    uint8_t index;

    if (source->protocol != IRMP_NEC_PROTOCOL)
    {
        return FALSE;
    }

    for (index = 0; index < IR_MAPPING_COUNT; ++index)
    {
        uint16_t source_address = pgm_read_word(&ir_mappings[index].source_address);
        uint16_t source_command = pgm_read_word(&ir_mappings[index].source_command);

        if (source->address == source_address && source->command == source_command)
        {
            target->protocol = IRMP_NEC_PROTOCOL;
            target->address = pgm_read_word(&ir_mappings[index].target_address);
            target->command = pgm_read_word(&ir_mappings[index].target_command);
            target->flags = 0;
            return TRUE;
        }
    }

    return FALSE;
}

/* Report source and mapped target codes for easier mapping verification. */
static void print_mapping(const IRMP_DATA *source, const IRMP_DATA *target)
{
    uart_puts("MAP SRC=NEC ADDR=0x");
    uart_put_hex16(source->address);
    uart_puts(" CMD=0x");
    uart_put_hex16(source->command);
    uart_puts(" -> TX=NEC ADDR=0x");
    uart_put_hex16(target->address);
    uart_puts(" CMD=0x");
    uart_put_hex16(target->command);
    uart_puts("\r\n");
}

/* Start IRSND after preparing the target frame and preserving receive timing. */
static void transmit_frame(const IRMP_DATA *source, const IRMP_DATA *target)
{
    transmit_data.protocol = target->protocol;
    transmit_data.address = target->address;
    transmit_data.command = target->command;
    transmit_data.flags = 0;

    if ((source->flags & IRMP_FLAG_REPETITION) != 0)
    {
        transmit_data.flags = IRSND_RAW_REPETITION_FRAME;
    }

    cli();
    if (irsnd_send_data(&transmit_data, FALSE))
    {
        storeIRTimer();

        TCCR2 = _BV(WGM21) | _BV(CS20);
        OCR2 = (uint8_t)((F_CPU / IRSND_INTERRUPT_FREQUENCY) - 1UL);
        TCNT2 = 0;
        TIFR = _BV(OCF2) | _BV(TOV2);
        TIMSK = _BV(OCIE2);
        sei();

        uart_puts("TX started on PB3\r\n");
    }
    else
    {
        sei();
        uart_puts("TX error: IRSND busy or protocol unsupported\r\n");
    }
}

int main(void)
{
    IRMP_DATA mapped_data;

    uart_init();
    irsnd_init();
    irmp_init();
    initIRTimerForReceive();
    sei();

    uart_puts_P(PSTR("\r\nATmega8 TSOP mapping repeater ready\r\n"));
    uart_puts_P(PSTR("RX: mapped NEC address 0x0004; other NEC frames pass through\r\n"));
    uart_puts_P(PSTR("TX: NEC on PB3/OC2; UART: PD1, 9600 baud, 8N1\r\n"));
    uart_puts_P(PSTR("Waiting for TSOP data...\r\n"));

    for (;;)
    {
        if (irmp_get_data(&received_data))
        {
            print_received_frame(&received_data);

            if (find_mapping(&received_data, &mapped_data))
            {
                print_mapping(&received_data, &mapped_data);
                transmit_frame(&received_data, &mapped_data);
            }
            else if (received_data.protocol == IRMP_NEC_PROTOCOL &&
                     received_data.address == 0x0004)
            {
                uart_puts("TX skipped: NEC command not in mapping table\r\n");
            }
            else if (received_data.protocol == IRMP_NEC_PROTOCOL)
            {
                transmit_frame(&received_data, &received_data);
            }
            else
            {
                uart_puts("TX skipped: protocol not supported by this mapper\r\n");
            }
        }
    }
}
