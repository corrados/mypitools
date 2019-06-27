#!/bin/bash

# TODO install required apt-get packages


# settings
read -e -p "Please set GPIO number for IR LED: " -i "22" SET_IRGPIO
echo "GPIO number for IR LED is set to $SET_IRGPIO"

# compile ledremote tool
gcc ledremote.c -lm -lpigpio -pthread -lrt -o ledremote -DIRGPIO=\"$SET_IRGPIO\"

# install ledremote tool in user bin directory
sudo cp ledremote /usr/bin

# create cron tab entries for the LED stribe (note that the original file will be deleted!)
CRON_TABLE="0  17    * * *       sudo ledremote KEY_POWERON && sudo ledremote KEY_GREEN
0  20    * * *       sudo ledremote KEY_ORANGE
0  21    * * *       sudo ledremote KEY_WHITE
1  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
2  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
3  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
4  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
5  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
6  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
7  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
0  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
9  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
20 22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
0  23    * * *       sudo ledremote KEY_ORANGE
10 23    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
30 23    * * *       sudo ledremote KEY_POWEROFF
0  0     * * *       sudo ledremote KEY_POWEROFF
0  1     * * *       sudo ledremote KEY_POWEROFF
0  2     * * *       sudo ledremote KEY_POWEROFF
0  3     * * *       sudo ledremote KEY_POWEROFF"

read -p "Your current crontab will be overwritten. Are you sure? " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]
then
	echo "These cron tab entries are written:"
	echo "$CRON_TABLE"
	{ echo "$CRON_TABLE"; } | crontab -u corrados -
else
	echo "Cancelled."
fi

# run pgpiod at system startup
systemctl enable pigpiod
