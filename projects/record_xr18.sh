#!/bin/bash

# sudo apt install libsox-fmt-all
#
# create /mnt/usb directory and add the following line to /etc/fstab to mount USB stick:
# /dev/sda1       /mnt/usb        auto    defaults,nofail,sync,uid=1000,gid=1000,umask=022        0       0
#
# in rc.local:
# su pi -c 'cd /home/pi;./record_xr18.sh' &

monitor_xr18() {
  while [ 1 ]
  do
    #if [ $(aplay -l | grep -c "XR18") -eq 0 ]; then
    if [ $(aplay -l | grep -c "S3") -eq 0 ]; then
      sudo shutdown -h now
    fi
    sleep 1
  done
}

while [ 1 ]
do

  # check for XR18 connected
  #if [ $(aplay -l|grep -c "XR18") -eq 1 ]; then
  if [ $(aplay -l|grep -c "S3") -eq 1 ]; then

    # monitor sound card connection in the background
    monitor_xr18 &

    # XR18 connected, we are no longer in edrumulus mode -> kill all edrumulus processes
    # note that this is important to free audio device for recording
    killall drumgizmo >/dev/null 2>&1
    killall ttymidi >/dev/null 2>&1
    sudo systemctl stop pigpiod
    killall ecasound >/dev/null 2>&1
    killall jackd >/dev/null 2>&1

    # record multi-channel audio from XR18
    FILENAME=/mnt/usb/$(date +"%Y%m%d_%H%M").ogg

    # starting jack audio
    JACK_NO_AUDIO_RESERVATION=1 jackd -R -T -P70 -t2000 -d alsa -dhw:S3 -p 2048 -n 6 -r 48000 -s >/dev/null 2>&1 &
    #JACK_NO_AUDIO_RESERVATION=1 jackd -R -T -P70 -t2000 -d alsa -dhw:XR18 -p 2048 -n 6 -r 48000 -s >/dev/null 2>&1 &
    sleep 2

    # starting jack_capture, NOTE: disabled pthread_create in start_keypress_thread to make it work
    /home/corrados/jack_capture/jack_capture -f ogg --silent --disable-console --port system:capture* $FILENAME >/dev/null 2>&1
    #/home/corrados/jack_capture/jack_capture --verbose --disable-console --port system:capture* $FILENAME >log2.txt 2>log2.txt

  fi

  sleep 10

done

