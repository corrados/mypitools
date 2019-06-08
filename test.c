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

	char key_poweron[]  = "00000000111101111100000000111111"; // 0xC03F
	char key_poweroff[] = "00000000111101110100000010111111"; // 0x40BF
	char* curkey        = key_poweroff; // default behavior

	// parse single input parameter
	if ( argc == 2 )
	{
		if ( strcmp ( argv[1], "KEY_POWERON" ) == 0 )
		{
			curkey = key_poweron;
		}
		else if ( strcmp ( argv[1], "KEY_POWEROFF" ) == 0 )
		{
			curkey = key_poweroff;
		}
	}

	int result = irSling(
		outPin,
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
