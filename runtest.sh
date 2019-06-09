#!/bin/bash

gcc test.c -lm -lpigpio -pthread -lrt -o ledremote

for ((i=0;i<=1;i++))
do
	sudo ./ledremote KEY_POWERON
	sleep 0.3
	sudo ./ledremote KEY_POWEROFF
	sleep 0.3
	sudo ./ledremote KEY_POWERON
	sleep 0.3
	sudo ./ledremote KEY_RED
	sleep 0.3
	sudo ./ledremote KEY_GREEN
	sleep 0.3
	sudo ./ledremote KEY_BLUE
	sleep 0.3
	sudo ./ledremote KEY_WHITE
	sleep 0.3
	sudo ./ledremote BTN_2
	sleep 0.3
	sudo ./ledremote BTN_3
	sleep 0.3
	sudo ./ledremote BTN_4
	sleep 0.3
	sudo ./ledremote BTN_5
	sleep 0.3
	sudo ./ledremote BTN_6
	sleep 0.3
	sudo ./ledremote BTN_7
	sleep 0.3
	sudo ./ledremote BTN_8
	sleep 0.3
	sudo ./ledremote BTN_9
	sleep 0.3
	sudo ./ledremote KEY_YELLOW
	sleep 0.3
	sudo ./ledremote BTN_A
	sleep 0.3
	sudo ./ledremote BTN_B
	sleep 0.3
	sudo ./ledremote KEY_POWEROFF
	sleep 0.3
done
