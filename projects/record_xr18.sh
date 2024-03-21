#!/bin/bash

# create /mnt/usb directory and add the following line to /etc/fstab to mount USB stick:
# /dev/sda1       /mnt/usb        auto    defaults,nofail,sync,uid=1000,gid=1000,umask=022        0       0

while [ 1 ]
do

  # check for XR18 connected
  if [ $(aplay -l|grep -c "X18XR18") -eq 1 ]; then

    ## XR18 connected, we are no longer in edrumulus mode -> kill all edrumulus processes
    #killall ecasound
    #killall drumgizmo
    #killall jackd

    FILENAME=/mnt/usb/$(date +"%Y%m%d_%H%M").wav
    echo recording to $FILENAME

    # record multi-channel audio from XR18
    AUDIODEV="hw:XR18,0"; rec -c 18 $FILENAME

  fi

  sleep 10

done

