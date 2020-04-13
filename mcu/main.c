#include "init.h"
#include <stdbool.h>
#include <stdint.h>

// Resistor screen pins - pin 1 on bottom
#define RES_BOTTOM 0
#define RES_TOP    1
#define RES_LEFT   2
#define RES_RIGHT  4

// Same as above - define AN analog number
#define AN_BOTTOM  0
#define AN_TOP     1
#define AN_LEFT    2
#define AN_RIGHT   3

#define IFACE_CLK  3
#define IFACE_DATA 5

union {
    uint32_t raw;
    struct {
        unsigned : 8;
        uint8_t dat2: 8;
        uint8_t dat1: 8;
        uint8_t dat0: 8;
    };
    struct {
        unsigned : 8;
        unsigned : 8;
        unsigned : 8;
        unsigned : 7;
        uint8_t msb: 1;
    };
} buffer;

_Bool interrupt_occurred = false;

typedef enum {
    START,
    PREPARE_X,
    CAPTURE_X,
    PREPARE_Y,
    CAPTURE_Y,
    READY_TO_TRANSMIT,
    TRANSMITTING,
    DONE
} State;

union {
    uint16_t raw;
    struct {
        uint8_t tx_count: 8;
        uint8_t state: 3;
        uint8_t checksum: 1;
        unsigned : 4;
    };
} absolute_state;
#define R() GPIO = 0; absolute_state.raw = 0;

void setup()
{
    R();

    // Make data pin output
    TRISIO = ~(1 << IFACE_DATA);

    // Set A/D timing
    ANSELbits.ADCS = 0b101;   // 4us @ 4MHz INTOSC

    // Trigger interrupt from the clock pin
    IOC = 1 << IFACE_CLK;
    GPIE = 1;
    GIE = 1;
}

void prepare_x()
{
    // Left -> VDD
    // Right -> GND
    // Bottom <- result

	// Make bottom pin analog and enable ADC
	ANSEL = 1 << AN_BOTTOM;
	ADCON0bits.CHS = AN_BOTTOM;
	ADON = 1;

    // Set voltages of left and right pins
    TRISIO = ~((1 << IFACE_DATA) | (1 << RES_RIGHT) | (1 << RES_LEFT));
    GPIO = 1 << RES_LEFT;
    
    // Disable pull up of non-outputs
    WPU = ~((1 << RES_TOP) | (1 << RES_BOTTOM));
}

void capture_x()
{
    // Fetch and copy result
    GO = 1;
    while (GO) {}
    buffer.dat0 = ADRESH; // 8/8
    buffer.dat1 = ADRESL; // 2/8

    // We're ready, disable ADC and make only data signal as output
    ADON = 0;
    ANSEL = 0;
    GPIO = 0;
    TRISIO = ~(1 << IFACE_DATA);
    WPU = ~0;
}

void prepare_y()
{
    // Top -> VDD
    // Bottom -> GND
    // Left <- in

	// Make left pin analog and enable ADC
	ANSEL = 1 << AN_LEFT;
	ADCON0bits.CHS = AN_LEFT;
	ADON = 1;

	// Set voltages of top and bottom pins
    TRISIO = ~((1 << IFACE_DATA) | (1 << RES_TOP) | (1 << RES_BOTTOM));
    GPIO = 1 << RES_TOP;
    
    // Disable pull-up of non-outputs
    WPU = ~((1 << RES_RIGHT) | (1 << RES_LEFT));
}

void capture_y()
{
    // Fetch and copy result
    GO = 1;
    while (GO) {}
    buffer.dat1 |= ADRESH >> 2;                  // 6/8
    buffer.dat2 = (ADRESH << 6) | (ADRESL >> 2); // (2+2)/8

    // We're ready, disable ADC and make only data signal as output
    ADON = 0;
    ANSEL = 0;
    GPIO = 0;
    TRISIO = ~(1 << IFACE_DATA);
    WPU = ~0;
}

__bit transmit_position()
{
    // If we've transmitted all 20 bytes, finish up with the checksum
    if (absolute_state.tx_count == 20)
    {
        GPIO = absolute_state.checksum << IFACE_DATA;
        return 1;
    }

    // Set the data to the MSB of the buffer byte and update the checksum
    char data = buffer.msb;
    GPIO = data << IFACE_DATA;
    absolute_state.checksum ^= data;

    // Shift left the buffer byte so we're ready for the next operation
    buffer.raw <<= 1;

    absolute_state.tx_count++;
    return 0;
}

void __interrupt() on_interrupt()
{
    switch (absolute_state.state)
    {
    case START: // dummy state
        absolute_state.state++;
        // fall through!
    case PREPARE_X:
        prepare_x();
        absolute_state.state++;
        break;
    case CAPTURE_X:
        capture_x();
        absolute_state.state++;
        break;
    case PREPARE_Y:
        prepare_y();
        absolute_state.state++;
        break;
    case CAPTURE_Y:
        capture_y();
        absolute_state.state++;
        break;
    case READY_TO_TRANSMIT:
        // Set data line to high to indicate we're ready
        GPIO = (1 << IFACE_DATA);
        absolute_state.state++;
        break;
    case TRANSMITTING:
        if (transmit_position())
        {
            absolute_state.state++;
        }
        break;
    case DONE:
        // Set to ready state
        R();
        break;
    }

    // Acknowledge peripheral interrupt
    interrupt_occurred = true;
    GPIF = 0;
}

void main()
{
    setup();
    while (1) {
        SLEEP();
        if (interrupt_occurred)
        {
            interrupt_occurred = false;
        }
        else
        {
            // If we're at this point, we haven't received a signal fast enough, i.e. timed out
            // "Reset" the peripheral
            R();
        }
    }
}
