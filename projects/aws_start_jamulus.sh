#!/bin/bash

# install jamulus via ssh and start server

ssh ubuntu@$1 bash -c "'
sudo apt-get update
sudo apt-get -y dist-upgrade
sudo apt-get -y install git build-essential libjack-jackd2-dev qtbase5-dev qttools5-dev-tools qtmultimedia5-dev
git clone https://github.com/jamulussoftware/jamulus.git
cd jamulus
git checkout r3_9_0
qmake \"CONFIG+=nojsonrpc headless serveronly\"
make
sudo /usr/bin/ionice -c1 /usr/bin/nice -n -20 ./Jamulus -s -n -F -u 30 -e jamulus.fischvolk.de:11214 -o \"AWS Server;FFM;82\"
'"

