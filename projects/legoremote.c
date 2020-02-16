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
    int leadingPulseDuration = 156;  // The duration of the beginning pulse in microseconds
    int leadingGapDuration = 1014;   // The duration of the gap in microseconds after the leading pulse
    int onePulse = 156;              // The duration of a pulse in microseconds when sending a logical 1
    int zeroPulse = 156;             // The duration of a pulse in microseconds when sending a logical 0
    int oneGap = 546;                // The duration of the gap in microseconds when sending a logical 1
    int zeroGap = 260;               // The duration of the gap in microseconds when sending a logical 0
    int sendTrailingPulse = 1;       // 1 = Send a trailing pulse with duration equal to "onePulse"
                                     // 0 = Don't send a trailing pulse

#ifdef IRGPIO
    outPin = atoi ( IRGPIO ); // if GPIO pin was given by preprocessor use that one
#endif

    char* curkey   = "0000010010000011"; // default behavior
    char* curkey2  = "0000010000001011"; // default behavoir
    int   bUseKey2 = 0;

    // parse single input parameter
    if ( argc == 2 )
    {
        if ( strcmp ( argv[1], "1R_0" ) == 0 )     curkey = "0000010000001011"; // 0x040B
        if ( strcmp ( argv[1], "1R_1" ) == 0 )     curkey = "0000010000011010"; // 0x041A
        if ( strcmp ( argv[1], "1R_2" ) == 0 )     curkey = "0000010000101001"; // 0x0429
        if ( strcmp ( argv[1], "1R_3" ) == 0 )     curkey = "0000010000111000"; // 0x0438
        if ( strcmp ( argv[1], "1R_4" ) == 0 )     curkey = "0000010001001111"; // 0x044F
        if ( strcmp ( argv[1], "1R_5" ) == 0 )     curkey = "0000010001011110"; // 0x045E
        if ( strcmp ( argv[1], "1R_6" ) == 0 )     curkey = "0000010001101101"; // 0x046D
        if ( strcmp ( argv[1], "1R_7" ) == 0 )     curkey = "0000010001111100"; // 0x047C
        if ( strcmp ( argv[1], "1R_BRAKE" ) == 0 ) curkey = "0000010010000011"; // 0x0483
        if ( strcmp ( argv[1], "1R_M7" ) == 0 )    curkey = "0000010010010010"; // 0x0492
        if ( strcmp ( argv[1], "1R_M6" ) == 0 )    curkey = "0000010010100001"; // 0x04A1
        if ( strcmp ( argv[1], "1R_M5" ) == 0 )    curkey = "0000010010110000"; // 0x04B0
        if ( strcmp ( argv[1], "1R_M4" ) == 0 )    curkey = "0000010011000111"; // 0x04C7
        if ( strcmp ( argv[1], "1R_M3" ) == 0 )    curkey = "0000010011010110"; // 0x04D6
        if ( strcmp ( argv[1], "1R_M2" ) == 0 )    curkey = "0000010011100101"; // 0x04E5
        if ( strcmp ( argv[1], "1R_M1" ) == 0 )    curkey = "0000010011110100"; // 0x04F4
        if ( strcmp ( argv[1], "1B_0" ) == 0 )     curkey = "0000010100001010"; // 0x050A
        if ( strcmp ( argv[1], "1B_1" ) == 0 )     curkey = "0000010100011011"; // 0x051B
        if ( strcmp ( argv[1], "1B_2" ) == 0 )     curkey = "0000010100101000"; // 0x0528
        if ( strcmp ( argv[1], "1B_3" ) == 0 )     curkey = "0000010100111001"; // 0x0539
        if ( strcmp ( argv[1], "1B_4" ) == 0 )     curkey = "0000010101001110"; // 0x054E
        if ( strcmp ( argv[1], "1B_5" ) == 0 )     curkey = "0000010101011111"; // 0x055F
        if ( strcmp ( argv[1], "1B_6" ) == 0 )     curkey = "0000010101101100"; // 0x056C
        if ( strcmp ( argv[1], "1B_7" ) == 0 )     curkey = "0000010101111101"; // 0x057D
        if ( strcmp ( argv[1], "1B_BRAKE" ) == 0 ) curkey = "0000010110000010"; // 0x0582
        if ( strcmp ( argv[1], "1B_M7" ) == 0 )    curkey = "0000010110010011"; // 0x0593
        if ( strcmp ( argv[1], "1B_M6" ) == 0 )    curkey = "0000010110100000"; // 0x05A0
        if ( strcmp ( argv[1], "1B_M5" ) == 0 )    curkey = "0000010110110001"; // 0x05B1
        if ( strcmp ( argv[1], "1B_M4" ) == 0 )    curkey = "0000010111000110"; // 0x05C6
        if ( strcmp ( argv[1], "1B_M3" ) == 0 )    curkey = "0000010111010111"; // 0x05D7
        if ( strcmp ( argv[1], "1B_M2" ) == 0 )    curkey = "0000010111100100"; // 0x05E4
        if ( strcmp ( argv[1], "1B_M1" ) == 0 )    curkey = "0000010111110101"; // 0x05F5

        if ( strcmp ( argv[1], "1R_3_1B_M3" ) == 0 )
	{
            curkey   = "0000010000111000"; // 0x0438
            curkey2  = "0000010111010111"; // 0x05D7
	    bUseKey2 = 1;
	}

        if ( strcmp ( argv[1], "1R_4_1B_M4" ) == 0 )
	{
            curkey   = "0000010001001111"; // 0x044F
            curkey2  = "0000010111000110"; // 0x05C6
	    bUseKey2 = 1;
	}

        if ( strcmp ( argv[1], "1R_BRAKE_1B_BRAKE" ) == 0 )
	{
            curkey   = "0000010010000011"; // 0x0483
            curkey2  = "0000010110000010"; // 0x0582
	    bUseKey2 = 1;
	}

        if ( strcmp ( argv[1], "1R_M3_1B_3" ) == 0 )
	{
            curkey   = "0000010011010110"; // 0x04D6
            curkey2  = "0000010100111001"; // 0x0539
	    bUseKey2 = 1;
	}

        if ( strcmp ( argv[1], "1R_M4_1B_4" ) == 0 )
	{
            curkey   = "0000010011000111"; // 0x04C7
            curkey2  = "0000010101001110"; // 0x054E
	    bUseKey2 = 1;
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

    if ( bUseKey2 )
    {
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
                             curkey2);
    }

    return result;
}
