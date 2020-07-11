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
    uint32_t outPin = 24;            // The Broadcom pin number the signal will be sent on
    int frequency = 38000;           // The frequency of the IR signal in Hz
    double dutyCycle = 0.5;          // The duty cycle of the IR signal. 0.5 means for every cycle,
                                     // the LED will turn on for half the cycle time, and off the other half
    int leadingPulseDuration = 8957;  // The duration of the beginning pulse in microseconds
    int leadingGapDuration = 4496;   // The duration of the gap in microseconds after the leading pulse
    int onePulse = 522;              // The duration of a pulse in microseconds when sending a logical 1
    int zeroPulse = 522;             // The duration of a pulse in microseconds when sending a logical 0
    int oneGap = 1711;                // The duration of the gap in microseconds when sending a logical 1
    int zeroGap = 594;               // The duration of the gap in microseconds when sending a logical 0
    int sendTrailingPulse = 1;       // 1 = Send a trailing pulse with duration equal to "onePulse"
                                     // 0 = Don't send a trailing pulse

#ifdef IRGPIO
    outPin = atoi ( IRGPIO ); // if GPIO pin was given by preprocessor use that one
#endif

    char* curkey = "0000010010000011"; // default behavior

    if ( argc == 2 )
    {
        if ( strcmp ( argv[1], "KEY_HOME" ) == 0 )      curkey = "0000010000001011"; // 0x040B
        if ( strcmp ( argv[1], "KEY_PLAYPAUSE" ) == 0 ) curkey = "0000010000011010"; // 0x041A
        if ( strcmp ( argv[1], "KEY_RIGHT" ) == 0 )     curkey = "0000010000101001"; // 0x0429
        if ( strcmp ( argv[1], "KEY_LEFT" ) == 0 )      curkey = "0000010000111000"; // 0x0438
        if ( strcmp ( argv[1], "KEY_UP" ) == 0 )        curkey = "0000010001001111"; // 0x044F
        if ( strcmp ( argv[1], "KEY_DOWN" ) == 0 )      curkey = "0000010001001111"; // 0x044F
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
