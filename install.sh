#!/bin/bash

# settings
read -e -p "Please set GPIO number for IR LED: " -i "22" SET_IRGPIO
echo "GPIO number for IR LED is set to $SET_IRGPIO"

read -e -p "Please set GPIO number for DHT22 temperature sensor: " -i "4" TEMPSENSORGPIO
echo "GPIO number for IDHT22 temperature sensor is set to $TEMPSENSORGPIO"


echo "TODO MANUALLY: Use raspi-config to enable VNC."


# CRON TAB #####################################################################
# create cron tab entries for the LED stribe (note that the original file will be deleted!)
# note that ledremote and myrunscript must not start at the same time
# note that we start/stop multiple times to make sure these commands are received even if some fail
CRON_TABLE="1  17    * * *       sudo ledremote KEY_POWERON && sudo ledremote KEY_GREEN
2  17    * * *       sudo ledremote KEY_POWERON && sudo ledremote KEY_GREEN
3  17    * * *       sudo ledremote KEY_POWERON && sudo ledremote KEY_GREEN
1  20    * * *       sudo ledremote KEY_ORANGE
1  21    * * *       sudo ledremote KEY_WHITE
2  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
3  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
4  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
5  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
6  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
7  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
8  21    * * *       sudo ledremote KEY_BRIGHTNESSUP
1  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
9  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
20 22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
1  23    * * *       sudo ledremote KEY_ORANGE
10 23    * * *       sudo ledremote KEY_BRIGHTNESSDOWN
30 23    * * *       sudo ledremote KEY_POWEROFF
1  0     * * *       sudo ledremote KEY_POWEROFF
1  1     * * *       sudo ledremote KEY_POWEROFF
0   *    * * *       sudo myrunscript.py"

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
sudo apt-get update
sudo apt-get upgrade -y
sudo apt-get dist-upgrade -y
sudo apt-get install tvheadend gnuplot build-essential fail2ban git hdparm htop net-tools nethogs pigpio unattended-upgrades vim -y
sudo apt-get autoremove -y
sudo apt-get autoclean -y


# SSH ##########################################################################
if [ -d "/home/pi/.ssh" ]; then
  echo "The SSH directory is present, nothing to do."
else
  mkdir ~/.ssh
  chmod 700 ~/.ssh
  touch authorized_keys
  ssh-keygen
fi


# EXTERNAL USB HDD #############################################################
if grep -Fxq "/dev/sda1 /media/piarchiv ext4 defaults,nofail 0 0" /etc/fstab
then
	echo "SDA1 auto mount already set in /etc/fstab"
else
	echo "we append the SDA1 auto mount to /etc/fstab"
	sudo echo "# auto mount external HDD on SDA1" | sudo tee -a /etc/fstab >/dev/null
	sudo echo "/dev/sda1 /media/piarchiv ext4 defaults,nofail 0 0" | sudo tee -a /etc/fstab >/dev/null
fi


# USB TV CARD DRIVER ############################################################
if [ -f "/lib/firmware/dvb-usb-terratec-htc-stick-drxk.fw" ]
then
  echo "USB TV card driver already installed"
else
  wget https://github.com/OpenELEC/dvb-firmware/raw/master/firmware/dvb-usb-terratec-htc-stick-drxk.fw
  sudo mv dvb-usb-terratec-htc-stick-drxk.fw /lib/firmware
fi


# DISABLE SCREEN SAVER ##########################################################
if grep -Fxq "xserver-command=X -s 0 -p 0 -dpms" /etc/lightdm/lightdm.conf
then
	echo "screen saver already disabled in /etc/lightdm/lightdm.conf"
else
  echo "we change /etc/lightdm/lightdm.conf to disable the screen saver"
  sudo sed -i "s/#xserver-command=X.*/xserver-command=X -s 0 -p 0 -dpms/g" /etc/lightdm/lightdm.conf
fi


# FIX UNATTENDED UPGRADES FOR RASPIAN ##########################################
if test -f "/etc/apt/apt.conf.d/50unattended-upgrades"; then
	if grep -Fq "Raspbian" /etc/apt/apt.conf.d/50unattended-upgrades
	then
		echo "unattended upgrades configuration already fixed for Raspian"
	else
		echo "we fix the unattended upgrades configuration file"
		sudo sed -i '/Unattended-Upgrade::Origins-Pattern {/a "origin=Raspbian,codename=${distro_codename},label=Raspbian";' /etc/apt/apt.conf.d/50unattended-upgrades
		sudo sed -i '/Unattended-Upgrade::Origins-Pattern {/a "origin=Raspberry Pi Foundation,codename=${distro_codename},label=Raspberry Pi Foundation";' /etc/apt/apt.conf.d/50unattended-upgrades
	fi
fi


# LED REMOTE ###################################################################
# compile and install ledremote tool
echo "compile ledremote"
gcc ledremote.c -lm -lpigpio -pthread -lrt -o ledremote -DIRGPIO=\"$SET_IRGPIO\"
sudo mv ledremote /usr/local/bin


# TEMPERATURE READ TOOL ########################################################
# compile and install temperature read tool
echo "compile readtempsensor"
gcc -Wall -pthread -o readtempsensor readtempsensor.c -lpigpio -DIRGPIO=\"$TEMPSENSORGPIO\"
sudo mv readtempsensor /usr/local/bin


# AUDIO SETUP ##################################################################
# add the audio overlay
if grep -Fxq "dtoverlay=pwm-2chan,pin=18,func=2,pin2=13,func2=4" /boot/config.txt
then
	echo "audio overlay already set in /boot/config.txt"
else
	echo "we append the audio overlay to /boot/config.txt"
	sudo echo "# enable analog audio on pi zero" | sudo tee -a /boot/config.txt >/dev/null
	sudo echo "dtoverlay=pwm-2chan,pin=18,func=2,pin2=13,func2=4" | sudo tee -a /boot/config.txt >/dev/null
fi

# make sure the alsamixer level is correct for the audio output
amixer set Master 95%


# WEATHER DATA #################################################################
echo "create script data file in /var/log and make it writable by the current user $USER"
sudo touch /var/log/myrunscriptdata.csv
sudo chmod 664 /var/log/myrunscriptdata.csv
sudo chown pi:pi /var/log/myrunscriptdata.csv

# install run script in user bin directory
sudo cp myrunscript.py /usr/local/bin

