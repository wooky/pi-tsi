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
    CAPTURE,
    READY_TO_TRANSMIT,
    TRANSMITTING,
    DONE
} State;

union {
    uint16_t raw;
    struct {
        uint8_t preamble_count: 4;
        uint8_t preamble_bit: 1;
        unsigned : 3;
        uint8_t tx_count: 5;
        uint8_t state: 2;
        uint8_t checksum: 1;
    };
} absolute_state;
#define R() absolute_state.raw = 0;

void setup()
{
    R();

    // Make data pin output
    TRISIO = ~(1 << IFACE_DATA);
    GPIO = 0;

    // Make all analog pins... well, analog!
    ANSEL = 0b00001111;
    
    // Trigger interrupt from the clock pin
    IOC = 1 << IFACE_CLK;
    GPIE = 1;
    GIE = 1;
}

void capture_position()
{
	// Enable ADC
	ADON = 1;
	
    // Capture X position - left to VDD (out), right to GND (out), sense bottom (in)
    TRISIO = ~((1 << IFACE_DATA) | (1 << RES_RIGHT) | (1 << RES_LEFT));
    GPIO = 1 << RES_LEFT;
    ADCON0bits.CHS = AN_BOTTOM;
    GO = 1;
    while (GO) {}

    buffer.dat0 = ADRESH; // 8/8
    buffer.dat1 = ADRESL; // 2/8
    
    // Capture Y position - top to VDD (out), bottom to GND (out), sense left (in)
    TRISIO = ~((1 << IFACE_DATA) | (1 << RES_TOP) | (1 << RES_BOTTOM));
    GPIO = 1 << RES_TOP;
    ADCON0bits.CHS = AN_LEFT;
    GO = 1;
    while (GO) {}

    buffer.dat1 |= ADRESH >> 2;                  // 6/8
    buffer.dat2 = (ADRESH << 6) | (ADRESL >> 2); // (2+2)/8
    
    // We're ready, disable ADC and make only data signal as output
    ADON = 0;
    TRISIO = ~(1 << IFACE_DATA);
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
    case CAPTURE:
        capture_position();
        absolute_state.state = READY_TO_TRANSMIT;
        break;
    case READY_TO_TRANSMIT:
        // Set data line to high to indicate we're ready
        GPIO = (1 << IFACE_DATA);
        absolute_state.state = TRANSMITTING;
        break;
    case TRANSMITTING:
        if (transmit_position())
        {
            absolute_state.state = DONE;
        }
        break;
    case DONE:
        // Set data line to low to acknowledge we're done
        GPIO = 0;
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
            GPIO = 0;
            R();
        }
    }
}
