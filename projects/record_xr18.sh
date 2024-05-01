#!/bin/bash

# sudo apt install libsox-fmt-all
#
# create /mnt/usb directory and add the following line to /etc/fstab to mount USB stick:
# /dev/sda1       /mnt/usb        auto    defaults,nofail,sync,uid=1000,gid=1000,umask=022        0       0

while [ 1 ]
do

  # check for XR18 connected
  if [ $(aplay -l|grep -c "XR18") -eq 1 ]; then
  #if [ $(aplay -l|grep -c "S3") -eq 1 ]; then

    # XR18 connected, we are no longer in edrumulus mode -> kill all edrumulus processes
    # note that this is important to free audio device for recording
    killall drumgizmo
    killall ttymidi
    sudo systemctl stop pigpiod
    killall ecasound
    killall jackd

    FILENAME=/mnt/usb/$(date +"%Y%m%d_%H%M").wav
    echo recording to $FILENAME

    # record multi-channel audio from XR18

    # TEST using jack audio
    #jackd -R -T -P70 -t2000 -d alsa -dhw:S3 -p 2048 -n 6 -r 48000 -s &
    jackd -R -T -P70 -t2000 -d alsa -dhw:XR18 -p 2048 -n 6 -r 48000 -s &
    sleep 3
    jack_capture --port system:capture*  $FILENAME

    ##export AUDIODEV="hw:XR18,0"; rec --buffer 262144 -c 18 -b 24 $FILENAME
    #export AUDIODEV="hw:S3,0"; rec --buffer 262144 -c 18 -b 24 $FILENAME remix 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2

  fi

  sleep 10

done

