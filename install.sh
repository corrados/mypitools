#!/bin/bash

# compile ledremote tool
gcc ledremote.c -lm -lpigpio -pthread -lrt

# install ledremote tool in user bin directory
sudo cp ledremote /usr/bin

# create cron tab entries for the LED stribe (note that the original file will be deleted!)
read -p "Your crontab will be overwritten. Are you sure? " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]
then
	{ echo '0  17    * * *       sudo ledremote KEY_POWERON && sudo ledremote KEY_GREEN'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  20    * * *       sudo ledremote KEY_ORANGE'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  21    * * *       sudo ledremote KEY_WHITE'; } | crontab -u pi -
	{ crontab -l -u pi; echo '1  21    * * *       sudo ledremote KEY_BRIGHTNESSUP'; } | crontab -u pi -
	{ crontab -l -u pi; echo '2  21    * * *       sudo ledremote KEY_BRIGHTNESSUP'; } | crontab -u pi -
	{ crontab -l -u pi; echo '3  21    * * *       sudo ledremote KEY_BRIGHTNESSUP'; } | crontab -u pi -
	{ crontab -l -u pi; echo '4  21    * * *       sudo ledremote KEY_BRIGHTNESSUP'; } | crontab -u pi -
	{ crontab -l -u pi; echo '5  21    * * *       sudo ledremote KEY_BRIGHTNESSUP'; } | crontab -u pi -
	{ crontab -l -u pi; echo '6  21    * * *       sudo ledremote KEY_BRIGHTNESSUP'; } | crontab -u pi -
	{ crontab -l -u pi; echo '7  21    * * *       sudo ledremote KEY_BRIGHTNESSUP'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN'; } | crontab -u pi -
	{ crontab -l -u pi; echo '9  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN'; } | crontab -u pi -
	{ crontab -l -u pi; echo '20 22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  23    * * *       sudo ledremote KEY_ORANGE'; } | crontab -u pi -
	{ crontab -l -u pi; echo '10 23    * * *       sudo ledremote KEY_BRIGHTNESSDOWN'; } | crontab -u pi -
	{ crontab -l -u pi; echo '30 23    * * *       sudo ledremote KEY_POWEROFF'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  0     * * *       sudo ledremote KEY_POWEROFF'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  1     * * *       sudo ledremote KEY_POWEROFF'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  2     * * *       sudo ledremote KEY_POWEROFF'; } | crontab -u pi -
	{ crontab -l -u pi; echo '0  3     * * *       sudo ledremote KEY_POWEROFF'; } | crontab -u pi -
fi
