My Raspberry Pi Tools
=====================

Infrared Transmitter based on IR Slinger
----------------------------------------

I want to thank bschwind@github.com for his great code. Since I want to have a simple tool
box for my Raspberry Pi, I ripped all out of the repo which is not necessary for my intended usage.

My LED stribe is controlled with cron and changes brightness and colors depending on the time.
To prepare the system, I run the test with `sudo ./runtest.sh` and copy the executable to the
usr/bin with `sudo cp ledremote /usr/bin`. With `crontab -e` I configure the settings as follows:

`0  17    * * *       sudo ledremote KEY_POWERON && sudo ledremote KEY_GREEN`

`0  20    * * *       sudo ledremote KEY_ORANGE`

`0  21    * * *       sudo ledremote KEY_WHITE`

`1  21    * * *       sudo ledremote KEY_BRIGHTNESSUP`

`2  21    * * *       sudo ledremote KEY_BRIGHTNESSUP`

`3  21    * * *       sudo ledremote KEY_BRIGHTNESSUP`

`4  21    * * *       sudo ledremote KEY_BRIGHTNESSUP`

`5  21    * * *       sudo ledremote KEY_BRIGHTNESSUP`

`6  21    * * *       sudo ledremote KEY_BRIGHTNESSUP`

`7  21    * * *       sudo ledremote KEY_BRIGHTNESSUP`

`0  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN`

`9  22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN`

`20 22    * * *       sudo ledremote KEY_BRIGHTNESSDOWN`

`0  23    * * *       sudo ledremote KEY_ORANGE`

`10 23    * * *       sudo ledremote KEY_BRIGHTNESSDOWN`

`15 23    * * *       sudo ledremote KEY_ORANGE`

`30 23    * * *       sudo ledremote KEY_POWEROFF`

`0  0     * * *       sudo ledremote KEY_POWEROFF`

`0  1     * * *       sudo ledremote KEY_POWEROFF`

`0  2     * * *       sudo ledremote KEY_POWEROFF`

`0  3     * * *       sudo ledremote KEY_POWEROFF`
