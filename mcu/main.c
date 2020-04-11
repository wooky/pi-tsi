#include "init.h"
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

uint8_t buffer[3];

typedef enum {
    CAPTURE,
    READY_TO_TRANSMIT,
    TRANSMITTING,
    DONE
} State;

void setup()
{
    // Make data pin output
    TRISIO = ~(1 << IFACE_DATA);
    GPIO = 0;

    // Make all analog pins... well, analog!
    ANSEL = 0b00001111;
    
    // Trigger interrupt from the clock pin
    IOC = 1 << IFACE_CLK;
    GIE = 1;
    GPIE = 1;
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
    
    buffer[0] = ADRESH;	// [7-0]
    buffer[1] = ADRESL; // [7-6]
    
    // Capture Y position - top to VDD (out), bottom to GND (out), sense left (in)
    TRISIO = ~((1 << IFACE_DATA) | (1 << RES_TOP) | (1 << RES_BOTTOM));
    GPIO = 1 << RES_TOP;
    ADCON0bits.CHS = AN_LEFT;
    GO = 1;
    while (GO) {}
    
    buffer[1] |= ADRESH >> 2;                  // [7-2] go into [5-0]
    buffer[2] = (ADRESH << 6) | (ADRESL >> 2); // [1-0] go into [7-6] ; [7-6] go into [5-4]
    
    // We're ready, disable ADC and make only data signal as output
    ADON = 0;
    TRISIO = ~(1 << IFACE_DATA);
}

char transmit_position()
{
    static char pos = 0;
    char addr_offset = pos / 8;
    
    // Set the data to the MSB of the buffer byte
    GPIO = (buffer[addr_offset] >> 7) << IFACE_DATA;
    
    // Check if we read the entire buffer, all 20 bits
    if (pos == 19)
    {
        pos = 0;
        return 1;
    }
    
    // Shift left the buffer byte so we're ready for the next operation
    buffer[addr_offset] <<= 1;
    
    pos++;
    return 0;
}

void __interrupt() on_interrupt()
{
    // Acknowledge peripheral interrupt
    GPIF = 0;

    static State state = CAPTURE;
    switch (state)
    {
    case CAPTURE:
        capture_position();
        state = READY_TO_TRANSMIT;
        break;
    case READY_TO_TRANSMIT:
        // Set data line to high to indicate we're ready
        GPIO = (1 << IFACE_DATA);
        state = TRANSMITTING;
        break;
    case TRANSMITTING:
        if (transmit_position())
        {
            state = DONE;
        }
        break;
    case DONE:
        // Set data line to low to acknowledge we're done
        GPIO = 0;
        state = CAPTURE;
        break;
    }
}

void main()
{
    setup();
    while (1) {
        SLEEP();
    }
}
