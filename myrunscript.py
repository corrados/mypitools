#!/usr/bin/env python    

import configparser
import requests
import sys
from datetime import datetime
import time

# get api key and location from config file
config = configparser.ConfigParser()
config.read('config.ini')
api_key = config['openweathermap']['api']
location = config['openweathermap']['location']

# get weather data
url = "https://api.openweathermap.org/data/2.5/weather?q={}&units=metric&appid={}".format(location, api_key)
r = requests.get(url)
weather = r.json()

# process weather data
print("sunrise: " + datetime.fromtimestamp(int(weather["sys"]["sunrise"])).strftime('%Y-%m-%d %H:%M:%S'))
print("sunset:  " + datetime.fromtimestamp(int(weather["sys"]["sunset"])).strftime('%Y-%m-%d %H:%M:%S'))
print("clouds:  " + str(weather["clouds"]["all"]))
print("temp:    " + str(weather["main"]["temp"]))
print(weather["weather"][0]["main"])
print(weather["weather"][0]["description"])
