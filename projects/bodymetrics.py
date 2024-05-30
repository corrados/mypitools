#!/usr/bin/env python3

#*******************************************************************************
# Copyright (c) 2024-2024
# Author(s): Volker Fischer
#*******************************************************************************
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#*******************************************************************************

import sys, sqlite3, datetime
import matplotlib.pyplot as plt
import matplotlib.dates as dates

# settings/initializations
target_scale = 79
min_scale = 72
path = sys.argv[1]
database_bands = [path + "/Gadgetbridge"]
database_scale = path + "/openScale.db"
database_pressure = path + "/Blutdruck.txt"
data = []

# Band Data --------------------------------------------------------------------
for database_band in database_bands:
 con = sqlite3.connect(database_band)
 cursor = con.cursor()
 cursor.execute("SELECT * FROM MI_BAND_ACTIVITY_SAMPLE")
 rows = cursor.fetchall()
 for row in rows:
   rate = row[6]
   if rate < 250 and rate > 0:
     timestamp = row[0]
     raw_intensity = row[3] / 255 * 40 # convert range to 0 to 40
     output_date = datetime.datetime.fromtimestamp(timestamp)
     data.append((output_date, rate, raw_intensity, None, None))

# Scale Measurements -----------------------------------------------------------
con = sqlite3.connect(database_scale)
cursor = con.cursor()

cursor.execute("SELECT * FROM scaleMeasurements")
rows = cursor.fetchall()
for row in rows:
  weight = row[4]
  if weight > min_scale:
    timestamp = row[3] / 1000
    output_date = datetime.datetime.fromtimestamp(timestamp)
    data.append((output_date, None, None, weight, None))

# Pressure ---------------------------------------------------------------------
blood_pressure_data = []
with open(database_pressure, 'r') as file:
  for line in file:
    if line.strip():
      parts = line.split(',')
      date_time_str = parts[0].strip()
      output_date = datetime.datetime.strptime(date_time_str, '%Y-%m-%d %H:%M:%S')
      readings = [reading.strip() for reading in parts[1:]]
      for reading in readings:
        pressure = int(reading.split('/')[0])
        data.append((output_date, None, None, None, pressure))

# Plot -------------------------------------------------------------------------
x, a, b, c, d = zip(*data)
plt.plot(x, b, 'k') # activity
plt.plot(x, a, 'b') # rate
plt.plot(x, c, 'r.') # scale
plt.plot(x, d, 'g.') # pressure
plt.gcf().autofmt_xdate()
plt.gca().xaxis.set_major_formatter(dates.DateFormatter('%Y-%m-%d'))
plt.title('All Data')
plt.grid()
plt.show()

