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
#define GPIO_CLK (GPIO & (1 << IFACE_CLK))

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

typedef enum {
    INACTIVE,
    CAPTURE,
    TRANSMITTING
} State;

union {
    uint16_t raw;
    struct {
        uint8_t tx_count: 8;
        uint8_t state: 3;
        uint8_t checksum: 1;
        uint8_t last_clk: 1;
        uint8_t last_capture_was_nonzero: 1;
        unsigned : 2;
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

__bit capture()
{
    ////////// PREPARE X //////////
    
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

    ////////// CAPTURE X //////////
    
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

    ////////// PREPARE Y //////////
    
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

    ////////// CAPTURE Y //////////
    
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
    
    ////////// SHOULD WE SEND? //////////
    
    if (absolute_state.last_capture_was_nonzero || buffer.raw != 0)
    {
        absolute_state.last_capture_was_nonzero = buffer.raw != 0;
        return true;
    }
    return false;
}

__bit transmit_position()
{
    // If we've transmitted all 20 bytes, finish up with the checksum
    if (absolute_state.tx_count == 20)
    {
        GPIO = absolute_state.checksum << IFACE_DATA;
        return true;
    }

    // Set the data to the MSB of the buffer byte and update the checksum
    char data = buffer.msb;
    GPIO = data << IFACE_DATA;
    absolute_state.checksum ^= data;

    // Shift left the buffer byte so we're ready for the next operation
    buffer.raw <<= 1;

    absolute_state.tx_count++;
    return false;
}

void main()
{
    setup();
    while (1) {
        SLEEP();
        switch (absolute_state.state)
        {
        case INACTIVE:
            if (GPIO_CLK)
            {
                absolute_state.state = CAPTURE;
            }
            R();
            break;
        case CAPTURE:
            if (!GPIO_CLK)
            {
                absolute_state.state = INACTIVE;
            }
            else if (capture())
            {
                // Set data line high to indicate we're ready
                GPIO = (1 << IFACE_DATA);
                absolute_state.state = TRANSMITTING;
            }
            break;
        case TRANSMITTING:
            if (GPIO_CLK == absolute_state.last_clk)
            {
                // Possible timeout
                absolute_state.state = INACTIVE;
            }
            else
            {
                absolute_state.last_clk ^= 1;
                if (transmit_position())
                {
                    absolute_state.state = INACTIVE;
                }
            }
            break;
        }
    }
}
