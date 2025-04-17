#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pigpio.h>

#define MAX_COMMAND_SIZE 512
#define MAX_PULSES 12000

static inline void addPulse(uint32_t onPins, uint32_t offPins, uint32_t duration, gpioPulse_t *irSignal, int *pulseCount)
{
    int index = *pulseCount;

    irSignal[index].gpioOn = onPins;
    irSignal[index].gpioOff = offPins;
    irSignal[index].usDelay = duration;

    (*pulseCount)++;
}

// Generates a square wave for duration (microseconds) at frequency (Hz)
// on GPIO pin outPin. dutyCycle is a floating value between 0 and 1.
static inline void carrierFrequency(uint32_t outPin, double frequency, double dutyCycle, double duration, gpioPulse_t *irSignal, int *pulseCount)
{
    double oneCycleTime = 1000000.0 / frequency; // 1000000 microseconds in a second
    int onDuration = (int)round(oneCycleTime * dutyCycle);
    int offDuration = (int)round(oneCycleTime * (1.0 - dutyCycle));

    int totalCycles = (int)round(duration / oneCycleTime);
    int totalPulses = totalCycles * 2;

    int i;
    for (i = 0; i < totalPulses; i++)
    {
        if (i % 2 == 0)
        {
            // High pulse
            addPulse(1 << outPin, 0, onDuration, irSignal, pulseCount);
        }
        else
        {
            // Low pulse
            addPulse(0, 1 << outPin, offDuration, irSignal, pulseCount);
        }
    }
}

// Generates a low signal gap for duration, in microseconds, on GPIO pin outPin
static inline void gap(uint32_t outPin, double duration, gpioPulse_t *irSignal, int *pulseCount)
{
    addPulse(0, 0, duration, irSignal, pulseCount);
}

static inline int irSling(uint32_t outPin,
                          int frequency,
                          double dutyCycle,
                          int leadingPulseDuration,
                          int leadingGapDuration,
                          int onePulse,
                          int zeroPulse,
                          int oneGap,
                          int zeroGap,
                          int sendTrailingPulse,
                          const char *code)
{
    if (outPin > 31)
    {
        // Invalid pin number
        return 1;
    }

    size_t codeLen = strlen(code);

    if (codeLen > MAX_COMMAND_SIZE)
    {
        // Command is too big
        return 1;
    }

    gpioPulse_t irSignal[MAX_PULSES];
    int pulseCount = 0;

    // Generate Code
    carrierFrequency(outPin, frequency, dutyCycle, leadingPulseDuration, irSignal, &pulseCount);
    gap(outPin, leadingGapDuration, irSignal, &pulseCount);

    int i;
    for (i = 0; i < codeLen; i++)
    {
        if (code[i] == '0')
        {
            carrierFrequency(outPin, frequency, dutyCycle, zeroPulse, irSignal, &pulseCount);
            gap(outPin, zeroGap, irSignal, &pulseCount);
        }
        else if (code[i] == '1')
        {
            carrierFrequency(outPin, frequency, dutyCycle, onePulse, irSignal, &pulseCount);
            gap(outPin, oneGap, irSignal, &pulseCount);
        }
        else
        {
            printf("Warning: Non-binary digit in command\n");
        }
    }

    if (sendTrailingPulse)
    {
        carrierFrequency(outPin, frequency, dutyCycle, onePulse, irSignal, &pulseCount);
    }


    // Init pigpio
    if (gpioInitialise() < 0)
    {
        // Initialization failed
        printf("GPIO Initialization failed\n");
        return 1;
    }

    // Setup the GPIO pin as an output pin
    gpioSetMode(outPin, PI_OUTPUT);

    // Start a new wave
    gpioWaveClear();

    gpioWaveAddGeneric(pulseCount, irSignal);
    int waveID = gpioWaveCreate();

    if (waveID >= 0)
    {
        int result = gpioWaveTxSend(waveID, PI_WAVE_MODE_ONE_SHOT);
    }

    // Wait for the wave to finish transmitting
    while (gpioWaveTxBusy())
    {
        time_sleep(0.1);
    }

    // Delete the wave if it exists
    if (waveID >= 0)
    {
        gpioWaveDelete(waveID);
    }

    // Cleanup
    gpioTerminate();

    return 0;
}

int main(int argc, char *argv[])
{
    // Lirc .conf format:
    //  header       9079  4405    -> leadingPulseDuration, leadingGapDuration
    //  one           638  1612    -> onePulse,             oneGap
    //  zero          638   473    -> zeroPulse,            zeroGap
    //  ptrail        642          -> sendTrailingPulse
    //  repeat       9070  2159
    //  gap          107799
    //  toggle_bit      0

    uint32_t outPin    = 22;         // The Broadcom pin number the signal will be sent on
    int      frequency = 38000;      // The frequency of the IR signal in Hz
    double   dutyCycle = 0.5;        // The duty cycle of the IR signal. 0.5 means for every cycle,
                                     // the LED will turn on for half the cycle time, and off the other half

    // number are from LED stribe
    int leadingPulseDuration = 9000; // The duration of the beginning pulse in microseconds
    int leadingGapDuration   = 4500; // The duration of the gap in microseconds after the leading pulse
    int onePulse             = 641;  // The duration of a pulse in microseconds when sending a logical 1
    int zeroPulse            = 641;  // The duration of a pulse in microseconds when sending a logical 0
    int oneGap               = 1613; // The duration of the gap in microseconds when sending a logical 1
    int zeroGap              = 485;  // The duration of the gap in microseconds when sending a logical 0
    int sendTrailingPulse    = 1;    // 1 = Send a trailing pulse with duration equal to "onePulse"
                                     // 0 = Don't send a trailing pulse

#ifdef IRGPIO
    outPin = atoi ( IRGPIO ); // if GPIO pin was given by preprocessor use that one
#endif

    char* curkey = "00000000111101111100000000111111"; // default behavior

    if ( argc == 2 )
    {
        // parse single input parameter -> LED stribe
        if ( strcmp ( argv[1], "KEY_BRIGHTNESSUP" ) == 0 )   curkey = "00000000111101110000000011111111"; // 0x00FF
        if ( strcmp ( argv[1], "KEY_BRIGHTNESSDOWN" ) == 0 ) curkey = "00000000111101111000000001111111"; // 0x807F
        if ( strcmp ( argv[1], "KEY_POWEROFF" ) == 0 )       curkey = "00000000111101110100000010111111"; // 0x40BF
        if ( strcmp ( argv[1], "KEY_POWERON" ) == 0 )        curkey = "00000000111101111100000000111111"; // 0xC03F
        if ( strcmp ( argv[1], "KEY_RED" ) == 0 )            curkey = "00000000111101110010000011011111"; // 0x20DF
        if ( strcmp ( argv[1], "KEY_GREEN" ) == 0 )          curkey = "00000000111101111010000001011111"; // 0xA05F
        if ( strcmp ( argv[1], "KEY_BLUE" ) == 0 )           curkey = "00000000111101110110000010011111"; // 0x609F
        if ( strcmp ( argv[1], "KEY_WHITE" ) == 0 )          curkey = "00000000111101111110000000011111"; // 0xE01F
        if ( strcmp ( argv[1], "KEY_ORANGE" ) == 0 )         curkey = "00000000111101110001000011101111"; // 0x10EF
        if ( strcmp ( argv[1], "BTN_2" ) == 0 )              curkey = "00000000111101111001000001101111"; // 0x906F
        if ( strcmp ( argv[1], "BTN_3" ) == 0 )              curkey = "00000000111101110101000010101111"; // 0x50AF
        if ( strcmp ( argv[1], "BTN_4" ) == 0 )              curkey = "00000000111101110011000011001111"; // 0x30CF
        if ( strcmp ( argv[1], "BTN_5" ) == 0 )              curkey = "00000000111101111011000001001111"; // 0xB04F
        if ( strcmp ( argv[1], "BTN_6" ) == 0 )              curkey = "00000000111101110111000010001111"; // 0x708F
        if ( strcmp ( argv[1], "BTN_7" ) == 0 )              curkey = "00000000111101110000100011110111"; // 0x08F7
        if ( strcmp ( argv[1], "BTN_8" ) == 0 )              curkey = "00000000111101111000100001110111"; // 0x8877
        if ( strcmp ( argv[1], "BTN_9" ) == 0 )              curkey = "00000000111101110100100010110111"; // 0x48B7
        if ( strcmp ( argv[1], "KEY_YELLOW" ) == 0 )         curkey = "00000000111101110010100011010111"; // 0x28D7
        if ( strcmp ( argv[1], "BTN_A" ) == 0 )              curkey = "00000000111101111010100001010111"; // 0xA857
        if ( strcmp ( argv[1], "BTN_B" ) == 0 )              curkey = "00000000111101110110100010010111"; // 0x6897
        if ( strcmp ( argv[1], "KEY_PROG1" ) == 0 )          curkey = "00000000111101111101000000101111"; // 0xD02F
        if ( strcmp ( argv[1], "KEY_PROG2" ) == 0 )          curkey = "00000000111101111111000000001111"; // 0xF00F
        if ( strcmp ( argv[1], "KEY_PROG3" ) == 0 )          curkey = "00000000111101111100100000110111"; // 0xC837
        if ( strcmp ( argv[1], "KEY_PROG4" ) == 0 )          curkey = "00000000111101111110100000010111"; // 0xE817
    }
    else if ( argc == 3 )
    {
        // parse touple input parameter: [device] [command]
        if ( strcmp ( argv[1], "BAR" ) == 0 ) // Philips soundbar HTL2163B
        {
// TEST RM-D591 for testing lirc
// lirc_options.conf: --->
// For transmit only:
// driver          = default
// device          = /dev/lirc0
// For receive only:
// driver          = default
// device          = /dev/lirc1
// /boot/firmware/config.txt: --->
// dtoverlay=gpio-ir,gpio_pin=18
// dtoverlay=gpio-ir-tx,gpio_pin=22
leadingPulseDuration = 2422;
leadingGapDuration   = 571;
onePulse             = 1224;
zeroPulse            = 625;
oneGap               = 570;
zeroGap              = 570;
sendTrailingPulse    = 1;
if ( strcmp ( argv[2], "PLAY" ) == 0 )  curkey = "010011010001"; // 0x4D1
if ( strcmp ( argv[2], "STOP" ) == 0 )  curkey = "000111010001"; // 0x1D1
if ( strcmp ( argv[2], "MINUS" ) == 0 ) curkey = "110010010001"; // 0xC91
if ( strcmp ( argv[2], "PLUS" ) == 0 )  curkey = "010010010001"; // 0x491

        }
        else if ( strcmp ( argv[1], "TV" ) == 0 ) // Toshiba TV 42XV635D
        {
            leadingPulseDuration = 9000;
            leadingGapDuration   = 4500;
            onePulse             = 528;
            zeroPulse            = 528;
            oneGap               = 1699;
            zeroGap              = 599;
            sendTrailingPulse    = 1;
            if ( strcmp ( argv[2], "POWER" ) == 0 )      curkey = "00000000000000000100100010110111"; // 0x48B7
            if ( strcmp ( argv[2], "OK" ) == 0 )         curkey = "00000000000000001000010001111011"; // 0x847B
            if ( strcmp ( argv[2], "UP" ) == 0 )         curkey = "00000000000000001001100001100111"; // 0x9867
            if ( strcmp ( argv[2], "DOWN" ) == 0 )       curkey = "00000000000000001011100001000111"; // 0xB847
            if ( strcmp ( argv[2], "VOLUMEUP" ) == 0 )   curkey = "00000000000000000101100010100111"; // 0x58A7
            if ( strcmp ( argv[2], "VOLUMEDOWN" ) == 0 ) curkey = "00000000000000000111100010000111"; // 0x7887
            if ( strcmp ( argv[2], "1" ) == 0 )          curkey = "00000000000000001000000001111111"; // 0x807F
            if ( strcmp ( argv[2], "2" ) == 0 )          curkey = "00000000000000000100000010111111"; // 0x40BF
            if ( strcmp ( argv[2], "3" ) == 0 )          curkey = "00000000000000001100000000111111"; // 0xC03F
            if ( strcmp ( argv[2], "4" ) == 0 )          curkey = "00000000000000000010000011011111"; // 0x20DF
            if ( strcmp ( argv[2], "5" ) == 0 )          curkey = "00000000000000001010000001011111"; // 0xA05F
            if ( strcmp ( argv[2], "6" ) == 0 )          curkey = "00000000000000000110000010011111"; // 0x609F
            if ( strcmp ( argv[2], "7" ) == 0 )          curkey = "00000000000000001110000000011111"; // 0xE01F
            if ( strcmp ( argv[2], "8" ) == 0 )          curkey = "00000000000000000001000011101111"; // 0x10EF
            if ( strcmp ( argv[2], "9" ) == 0 )          curkey = "00000000000000001001000001101111"; // 0x906F
            if ( strcmp ( argv[2], "0" ) == 0 )          curkey = "00000000000000000000000011111111"; // 0x00FF
            if ( strcmp ( argv[2], "MENU" ) == 0 )       curkey = "00000000000000001101101000100101"; // 0xDA25
            if ( strcmp ( argv[2], "BACK" ) == 0 )       curkey = "00000000000000000010011011011001"; // 0x26D9
            if ( strcmp ( argv[2], "EXIT" ) == 0 )       curkey = "00000000000000001100001000111101"; // 0xC23D
            if ( strcmp ( argv[2], "RIGHT" ) == 0 )      curkey = "00000000000000000000001011111101"; // 0x02FD
            if ( strcmp ( argv[2], "LEFT" ) == 0 )       curkey = "00000000000000000100001010111101"; // 0x42BD
            if ( strcmp ( argv[2], "TEXT" ) == 0 )       curkey = "00000000000000001110100000010111"; // 0xE817
            if ( strcmp ( argv[2], "LIST" ) == 0 )       curkey = "00000000000000001110001000011101"; // 0xE21D
            if ( strcmp ( argv[2], "TV" ) == 0 )         curkey = "00000000000000000010001011011101"; // 0x22DD
        }
        else if ( strcmp ( argv[1], "BEAM" ) == 0 ) // Ultimea projector Apollo P20
        {
// TODO
        }
        else if ( strcmp ( argv[1], "DVD" ) == 0 ) // Sony blue-ray player BDP-S185
        {
            leadingPulseDuration = 2426;
            leadingGapDuration   = 565;
            onePulse             = 1221;
            zeroPulse            = 627;
            oneGap               = 569;
            zeroGap              = 569;
            sendTrailingPulse    = 1;
            if ( strcmp ( argv[2], "POWER" ) == 0 )     curkey = "00000000000000000000101010001011"; // 0xA8B
            if ( strcmp ( argv[2], "OK" ) == 0 )        curkey = "00000000000000000000101111001011"; // 0xBCB
            if ( strcmp ( argv[2], "UP" ) == 0 )        curkey = "00000000000000000000100111001011"; // 0x9CB
            if ( strcmp ( argv[2], "DOWN" ) == 0 )      curkey = "00000000000000000000010111001011"; // 0x5CB
            if ( strcmp ( argv[2], "AUDIO" ) == 0 )     curkey = "00000000000000000000001001101011"; // 0x26B
            if ( strcmp ( argv[2], "SUBTITLE" ) == 0 )  curkey = "00000000000000000000110001101011"; // 0xC6B
            if ( strcmp ( argv[2], "1" ) == 0 )         curkey = "00000000000000000000000000001011"; // 0x00B
            if ( strcmp ( argv[2], "2" ) == 0 )         curkey = "00000000000000000000100000001011"; // 0x80B
            if ( strcmp ( argv[2], "3" ) == 0 )         curkey = "00000000000000000000010000001011"; // 0x40B
            if ( strcmp ( argv[2], "4" ) == 0 )         curkey = "00000000000000000000110000001011"; // 0xC0B
            if ( strcmp ( argv[2], "5" ) == 0 )         curkey = "00000000000000000000001000001011"; // 0x20B
            if ( strcmp ( argv[2], "6" ) == 0 )         curkey = "00000000000000000000101000001011"; // 0xA0B
            if ( strcmp ( argv[2], "7" ) == 0 )         curkey = "00000000000000000000011000001011"; // 0x60B
            if ( strcmp ( argv[2], "8" ) == 0 )         curkey = "00000000000000000000111000001011"; // 0xE0B
            if ( strcmp ( argv[2], "9" ) == 0 )         curkey = "00000000000000000000000100001011"; // 0x10B
            if ( strcmp ( argv[2], "0" ) == 0 )         curkey = "00000000000000000000100100001011"; // 0x90B
            if ( strcmp ( argv[2], "MENU" ) == 0 )      curkey = "00000000000000000000001101001011"; // 0x34B
            if ( strcmp ( argv[2], "RETURN" ) == 0 )    curkey = "00000000000000000000110000101011"; // 0xC2B
            if ( strcmp ( argv[2], "PLAY" ) == 0 )      curkey = "00000000000000000000010110001011"; // 0x58B
            if ( strcmp ( argv[2], "RIGHT" ) == 0 )     curkey = "00000000000000000000001111001011"; // 0x3CB
            if ( strcmp ( argv[2], "LEFT" ) == 0 )      curkey = "00000000000000000000110111001011"; // 0xDCB
            if ( strcmp ( argv[2], "STOP" ) == 0 )      curkey = "00000000000000000000000110001011"; // 0x18B
            if ( strcmp ( argv[2], "HOME" ) == 0 )      curkey = "00000000000000000000010000101011"; // 0x42B
            if ( strcmp ( argv[2], "POPUPMENU" ) == 0 ) curkey = "00000000000000000000100101001011"; // 0x94B
        }
        else if ( strcmp ( argv[1], "LED" ) == 0 ) // Osram LED stribe
        {
            leadingPulseDuration = 9000;
            leadingGapDuration   = 4500;
            onePulse             = 553;
            zeroPulse            = 553;
            oneGap               = 1689;
            zeroGap              = 568;
            sendTrailingPulse    = 1;
            if ( strcmp ( argv[2], "ON" ) == 0 )            curkey = "00000000111111111110000000011111"; // 0xFFE01F
            if ( strcmp ( argv[2], "OFF" ) == 0 )           curkey = "00000000111111110110000010011111"; // 0xFF609F
            if ( strcmp ( argv[2], "BRIGHTER" ) == 0 )      curkey = "00000000111111110000000011111111"; // 0xFF00FF
            if ( strcmp ( argv[2], "DIMMER" ) == 0 )        curkey = "00000000111111110100000010111111"; // 0xFF40BF
            if ( strcmp ( argv[2], "WHITE" ) == 0 )         curkey = "00000000111111111100000000111111"; // 0xFFC03F
            if ( strcmp ( argv[2], "BLUE" ) == 0 )          curkey = "00000000111111110101000010101111"; // 0xFF50AF
            if ( strcmp ( argv[2], "GREEN" ) == 0 )         curkey = "00000000111111111001000001101111"; // 0xFF906F
            if ( strcmp ( argv[2], "RED" ) == 0 )           curkey = "00000000111111110001000011101111"; // 0xFF10EF
            if ( strcmp ( argv[2], "FLASH" ) == 0 )         curkey = "00000000111111111111000000001111"; // 0xFFF00F
            if ( strcmp ( argv[2], "STROBE" ) == 0 )        curkey = "00000000111111111100100000110111"; // 0xFFC837
            if ( strcmp ( argv[2], "SMOOTH" ) == 0 )        curkey = "00000000111111111110100000010111"; // 0xFFE817
            if ( strcmp ( argv[2], "MODE" ) == 0 )          curkey = "00000000111111111101100000100111"; // 0xFFD827
            if ( strcmp ( argv[2], "ORANGE" ) == 0 )        curkey = "00000000111111110011000011001111"; // 0xFF30CF
            if ( strcmp ( argv[2], "LIGHTERORANGE" ) == 0 ) curkey = "00000000111111110000100011110111"; // 0xFF08F7
            if ( strcmp ( argv[2], "CORAL" ) == 0 )         curkey = "00000000111111110010100011010111"; // 0xFF28D7
            if ( strcmp ( argv[2], "YELLOW" ) == 0 )        curkey = "00000000111111110001100011100111"; // 0xFF18E7
            if ( strcmp ( argv[2], "LIGHTERGREEN" ) == 0 )  curkey = "00000000111111111011000001001111"; // 0xFFB04F
            if ( strcmp ( argv[2], "TURQUOISE" ) == 0 )     curkey = "00000000111111111000100001110111"; // 0xFF8877
            if ( strcmp ( argv[2], "AQUA" ) == 0 )          curkey = "00000000111111111010100001010111"; // 0xFFA857
            if ( strcmp ( argv[2], "NAVY" ) == 0 )          curkey = "00000000111111111001100001100111"; // 0xFF9867
            if ( strcmp ( argv[2], "BLUEGRAY" ) == 0 )      curkey = "00000000111111110111000010001111"; // 0xFF708F
            if ( strcmp ( argv[2], "PEACH" ) == 0 )         curkey = "00000000111111110100100010110111"; // 0xFF48B7
            if ( strcmp ( argv[2], "LIGHTERPINK" ) == 0 )   curkey = "00000000111111110110100010010111"; // 0xFF6897
            if ( strcmp ( argv[2], "PINK" ) == 0 )          curkey = "00000000111111110101100010100111"; // 0xFF58A7
        }
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
