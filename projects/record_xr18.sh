#!/bin/bash

while [ 1 ]
do

  # check for XR18 connected
  if [ $(aplay -l|grep -c "X18XR18") -eq 1 ]; then

    # XR18 connected, we are no longer in edrumulus mode -> kill all edrumulus processes
    killall ecasound
    killall drumgizmo
    killall jackd

    FILENAME=/home/pi/$(date +"%Y%m%d_%H%M").wav
    echo recording to $FILENAME

    # record multi-channel audio from XR18
    AUDIODEV="hw:XR18,0"; rec -c 18 $FILENAME

  fi

  sleep 10

done

