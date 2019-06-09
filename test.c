#include <stdio.h>
#include "irslinger.h"

int main(int argc, char *argv[])
{
    uint32_t outPin = 24;            // The Broadcom pin number the signal will be sent on
    int frequency = 38000;           // The frequency of the IR signal in Hz
    double dutyCycle = 0.5;          // The duty cycle of the IR signal. 0.5 means for every cycle,
                                     // the LED will turn on for half the cycle time, and off the other half
    int leadingPulseDuration = 9000; // The duration of the beginning pulse in microseconds
    int leadingGapDuration = 4500;   // The duration of the gap in microseconds after the leading pulse
    int onePulse = 641;              // The duration of a pulse in microseconds when sending a logical 1
    int zeroPulse = 641;             // The duration of a pulse in microseconds when sending a logical 0
    int oneGap = 1613;               // The duration of the gap in microseconds when sending a logical 1
    int zeroGap = 485;               // The duration of the gap in microseconds when sending a logical 0
    int sendTrailingPulse = 1;       // 1 = Send a trailing pulse with duration equal to "onePulse"
                                     // 0 = Don't send a trailing pulse

    char* curkey = "00000000111101111100000000111111"; // default behavior

    // parse single input parameter
    if ( argc == 2 )
    {
        if ( strcmp ( argv[1], "KEY_BRIGHTNESSUP  " ) == 0 ) curkey  = "00000000111101110000000011111111"; // 0x00FF
        if ( strcmp ( argv[1], "KEY_BRIGHTNESSDOWN" ) == 0 ) curkey  = "00000000111101111000000001111111"; // 0x807F
        if ( strcmp ( argv[1], "KEY_POWEROFF" ) == 0 )       curkey  = "00000000111101110100000010111111"; // 0x40BF
        if ( strcmp ( argv[1], "KEY_POWERON " ) == 0 )       curkey  = "00000000111101111100000000111111"; // 0xC03F
        if ( strcmp ( argv[1], "KEY_RED" ) == 0 )            curkey  = "00000000111101110010000011011111"; // 0x20DF
        if ( strcmp ( argv[1], "KEY_GREEN" ) == 0 )          curkey  = "00000000111101111010000001011111"; // 0xA05F
        if ( strcmp ( argv[1], "KEY_BLUE" ) == 0 )           curkey  = "00000000111101110110000010011111"; // 0x609F
        if ( strcmp ( argv[1], "KEY_WHITE" ) == 0 )          curkey  = "00000000111101111110000000011111"; // 0xE01F
        if ( strcmp ( argv[1], "KEY_ORANGE" ) == 0 )         curkey  = "00000000111101110001000011101111"; // 0x10EF
        if ( strcmp ( argv[1], "BTN_2" ) == 0 )              curkey  = "00000000111101111001000001101111"; // 0x906F
        if ( strcmp ( argv[1], "BTN_3" ) == 0 )              curkey  = "00000000111101110101000010101111"; // 0x50AF
        if ( strcmp ( argv[1], "BTN_4" ) == 0 )              curkey  = "00000000111101110011000011001111"; // 0x30CF
        if ( strcmp ( argv[1], "BTN_5" ) == 0 )              curkey  = "00000000111101111011000001001111"; // 0xB04F
        if ( strcmp ( argv[1], "BTN_6" ) == 0 )              curkey  = "00000000111101110111000010001111"; // 0x708F
        if ( strcmp ( argv[1], "BTN_7" ) == 0 )              curkey  = "00000000111101110000100011110111"; // 0x08F7
        if ( strcmp ( argv[1], "BTN_8" ) == 0 )              curkey  = "00000000111101111000100001110111"; // 0x8877
        if ( strcmp ( argv[1], "BTN_9" ) == 0 )              curkey  = "00000000111101110100100010110111"; // 0x48B7
        if ( strcmp ( argv[1], "KEY_YELLOW" ) == 0 )         curkey  = "00000000111101110010100011010111"; // 0x28D7
        if ( strcmp ( argv[1], "BTN_A" ) == 0 )              curkey  = "00000000111101111010100001010111"; // 0xA857
        if ( strcmp ( argv[1], "BTN_B" ) == 0 )              curkey  = "00000000111101110110100010010111"; // 0x6897
        if ( strcmp ( argv[1], "KEY_PROG1" ) == 0 )          curkey  = "00000000111101111101000000101111"; // 0xD02F
        if ( strcmp ( argv[1], "KEY_PROG2" ) == 0 )          curkey  = "00000000111101111111000000001111"; // 0xF00F
        if ( strcmp ( argv[1], "KEY_PROG3" ) == 0 )          curkey  = "00000000111101111100100000110111"; // 0xC837
        if ( strcmp ( argv[1], "KEY_PROG4" ) == 0 )          curkey  = "00000000111101111110100000010111"; // 0xE817
    }

    int result = irSling(outPin,
                         frequency,
                         dutyCycle,
                         leadingPulseDuration,
                         leadingGapDuration,
                         onePulse,
                         zeroPulse,
                         oneGap,
                         zeroGap,
                         sendTrailingPulse,
                         curkey);

    return result;
}
