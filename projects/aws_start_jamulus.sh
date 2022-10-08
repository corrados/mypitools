#!/bin/bash

# install jamulus via ssh and start server

ssh ubuntu@$1 bash -c "'
sudo apt-get update
sudo apt-get -y dist-upgrade
sudo apt-get -y install git build-essential libjack-jackd2-dev qtbase5-dev qttools5-dev-tools qtmultimedia5-dev
rm -rf jamulus
git clone https://github.com/jamulussoftware/jamulus.git
cd jamulus
git checkout r3_9_0
qmake \"CONFIG+=nojsonrpc headless serveronly\"
make
'"

