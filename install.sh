#!/bin/bash

# TODO do not use user pi but create a different one (note that 'pi' is hard coded below!)

# settings
read -e -p "Please set GPIO number for IR LED: " -i "22" SET_IRGPIO
echo "GPIO number for IR LED is set to $SET_IRGPIO"

read -e -p "Please set GPIO number for DHT22 temperature sensor: " -i "4" TEMPSENSORGPIO
echo "GPIO number for IDHT22 temperature sensor is set to $TEMPSENSORGPIO"


# CRON TAB #####################################################################
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
0  3     * * *       sudo ledremote KEY_POWEROFF
0   *    * * *       myrunscript.py"

read -p "Your current crontab will be overwritten. Are you sure? " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]
then
	echo "These cron tab entries are written:"
	echo "$CRON_TABLE"
	{ echo "$CRON_TABLE"; } | crontab -u pi -
else
	echo "Cancelled."
fi


# SYSTEM UPDATE/INSTALL PACKAGES ###############################################
# update system and install required packages
echo "first we update the system and install the required packages"
apt-get update
apt-get upgrade -y
apt-get dist-upgrade -y
apt-get install build-essential fail2ban git hdparm htop libjack-jackd2-dev net-tools nethogs pigpio qjackctl qt5-default unattended-upgrades python-configparser vim -y
apt-get autoremove -y
apt-get autoclean -y


# LED REMOTE ###################################################################
# compile ledremote tool
echo "compile ledremote"
gcc ledremote.c -lm -lpigpiod_if2 -pthread -lrt -o ledremote -DIRGPIO=\"$SET_IRGPIO\"

# install ledremote tool in user bin directory
sudo mv ledremote /usr/local/bin


# TEMPERATURE READ TOOL ########################################################
# compile temperature read tool
echo "compile readtempsensor"
gcc -Wall -pthread -o readtempsensor readtempsensor.c -lpigpiod_if2 -DIRGPIO=\"$TEMPSENSORGPIO\"

# install temperature read tool in user bin directory
sudo mv readtempsensor /usr/local/bin


# DEAMONS ######################################################################
# run pgpiod at system startup
systemctl enable pigpiod


# AUDIO SETUP ##################################################################
# add the audio overlay
if grep -Fxq "dtoverlay=pwm-2chan,pin=18,func=2,pin2=13,func2=4" /boot/config.txt
then
	echo "audio overlay already set in /boot/config.txt"
else
	echo "we append the audio overlay to /boot/config.txt"
	echo "# enable analog audio on pi zero" >> /boot/config.txt
	echo "dtoverlay=pwm-2chan,pin=18,func=2,pin2=13,func2=4" >> /boot/config.txt
fi

# make sure the alsamixer level is correct for the audio output
amixer set PCM 95%


# WEATHER DATA #################################################################
echo "create script data file in /var/log and make it writable by the current user $USER"
sudo touch /var/log/myrunscriptdata.csv
sudo chmod 664 /var/log/myrunscriptdata.csv
sudo chown pi:pi /var/log/myrunscriptdata.csv

# install run script in user bin directory
sudo cp myrunscript.py /usr/local/bin
