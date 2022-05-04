


# Debugging with X32 emulator by pmaillot

Compile and run X32 emulator:
```
cd X32-Behringer
make
make X32
cd build
./X32 -i127.0.0.1
```

Initialize Raspberry Pi Zero W
```
sudo apt-get update
sudo apt-get dist-upgrade
sudo apt-get install git python3-pip
python3 -m pip install alsa-midi
git clone https://github.com/corrados/mypitools.git
cd mypitools
git submodule update --init
cd projects/xairremote
python3 xairremote.py

optionally, insert the following line in rc.local:
su pi -c 'cd /home/pi/mypitools/projects/xairremote;sleep 15;python3 xairremote.py' &
```

