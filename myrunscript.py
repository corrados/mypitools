#!/usr/bin/env python    

import configparser
import requests
import sys
from datetime import datetime
import time
import subprocess

# get api key and location from config file
config = configparser.ConfigParser()
config.read('/home/pi/config.ini')
api_key  = config['openweathermap']['api']
location = config['openweathermap']['location']

# get local weather data
loctemphum     = subprocess.check_output("readtempsensor", shell=True)
loctemphumlist = loctemphum.split()
loctemp        = float(loctemphumlist[0])
lochum         = float(loctemphumlist[1])

# get remote weather data
try:
  url     = "https://api.openweathermap.org/data/2.5/weather?q={}&units=metric&appid={}".format(location, api_key)
  r       = requests.get(url)
  weather = r.json()
  temp    = weather["main"]["temp"]
  hum     = weather["main"]["humidity"]
  cloud   = weather["clouds"]["all"]
except:
  temp  = -1
  hum   = -1
  cloud = -1

# process weather data
curdate    = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
filestring = "{},{},{},{},{},{}\r\n".format(curdate, loctemp, lochum, temp, hum, cloud)

file = open("/var/log/myrunscriptdata.csv", "a+")
file.write(filestring)
file.close

#print("sunrise:        " + datetime.fromtimestamp(int(weather["sys"]["sunrise"])).strftime('%Y-%m-%d %H:%M:%S'))
#print("sunset:         " + datetime.fromtimestamp(int(weather["sys"]["sunset"])).strftime('%Y-%m-%d %H:%M:%S'))
#print("clouds:         " + str(weather["clouds"]["all"]))
#print("temp:           " + str(weather["main"]["temp"]))
#print("local temp:     " + str(loctemp))
#print("humidity:       " + str(weather["main"]["humidity"]))
#print("local humidity: " + str(lochum))
#print(weather["weather"][0]["main"])
#print(weather["weather"][0]["description"])
