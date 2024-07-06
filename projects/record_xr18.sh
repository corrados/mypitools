#!/bin/bash

# sudo apt install libsox-fmt-all
#
# create /mnt/usb directory and add the following line to /etc/fstab to mount USB stick:
# /dev/sda1       /mnt/usb        auto    defaults,nofail,sync,uid=1000,gid=1000,umask=022        0       0
#
# in rc.local:
# su pi -c 'cd /home/pi;./record_xr18.sh' &

while [ 1 ]
do

  # sleep first to make sure on boot USB drive is correctly mounted
  #sleep 10

  # check for XR18 connected
  if [ $(aplay -l|grep -c "XR18") -eq 1 ]; then
  #if [ $(aplay -l|grep -c "S3") -eq 1 ]; then

    # XR18 connected, we are no longer in edrumulus mode -> kill all edrumulus processes
    # note that this is important to free audio device for recording
    killall drumgizmo >/dev/null 2>&1
    killall ttymidi >/dev/null 2>&1
    sudo systemctl stop pigpiod
    killall ecasound >/dev/null 2>&1
    killall jackd >/dev/null 2>&1

    # record multi-channel audio from XR18
    FILENAME=/mnt/usb/$(date +"%Y%m%d_%H%M").ogg

    # TESTS using jack audio
    #echo recording to $FILENAME
    #echo "TEST: starting recording"
    #jackd -R -T -P70 -t2000 -d alsa -dhw:XR18 -p 2048 -n 6 -r 48000 -s &
    
    
    # starting jack audio
    #JACK_NO_AUDIO_RESERVATION=1 jackd -R -T -P70 -t2000 -d alsa -dhw:S3 -p 2048 -n 6 -r 48000 -s >/dev/null 2>&1 &
    JACK_NO_AUDIO_RESERVATION=1 jackd -R -T -P70 -t2000 -d alsa -dhw:XR18 -p 2048 -n 6 -r 48000 -s >/dev/null 2>&1 &

    #JACK_NO_AUDIO_RESERVATION=1 jackd -R -T -P70 -t2000 -d alsa -dhw:S3 -p 2048 -n 6 -r 48000 -s >log.txt 2>log.txt &
    #jackd -R -T -P70 -t2000 -d alsa -dhw:S3 -p 2048 -n 6 -r 48000 -s >log.txt 2>log.txt &
    #jackd -R -T -P70 -t2000 -d alsa -dhw:XR18 -p 2048 -n 6 -r 48000 -s >/dev/null 2>&1 &
    sleep 2


    # TESTS using jack_capture 
    #echo play|jack_transport #>/dev/null 2>&1
    #jack_capture --jack-transport --silent --disable-console --port system:capture* $FILENAME >/dev/null 2>&1
    #jack_capture --jack-transport --port system:capture* $FILENAME #>/dev/null 2>&1
    #echo play|jack_transport >/dev/null 2>&1
    #jack_capture --jack-transport --silent --disable-console --port system:capture* $FILENAME &#>/dev/null 2>&1
    #sleep 2
    #echo play|jack_transport #>/dev/null 2>&1

    # TESTS using rec
    ##export AUDIODEV="hw:XR18,0"; rec --buffer 262144 -c 18 -b 24 $FILENAME
    #export AUDIODEV="hw:S3,0"; rec --buffer 262144 -c 18 -b 24 $FILENAME remix 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2
    

    # starting jack_capture
    # NOTE: Disabled pthread_create in start_keypress_thread to make it work!
    /home/corrados/jack_capture/jack_capture -f ogg --silent --disable-console --port system:capture* $FILENAME >/dev/null 2>&1
    #/home/corrados/jack_capture/jack_capture --verbose --disable-console --port system:capture* $FILENAME >log2.txt 2>log2.txt

    exit 0
    #echo "TEST: recording stopped"
    
  fi

  sleep 10

done

