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
database_bands = [path + "/Gadgetbridge"]#, path + "/Gadgetbridge_20240308-160840"]
database_scale = path + "/openScale.db"
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
     output_date = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M')
     data.append((output_date, rate, raw_intensity, None))

# Scale Measurements -----------------------------------------------------------
con = sqlite3.connect(database_scale)
cursor = con.cursor()

cursor.execute("SELECT * FROM scaleMeasurements")
rows = cursor.fetchall()
for row in rows:
  weight = row[4]
  if weight > min_scale:
    timestamp = row[3] / 1000
    output_date = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M')
    data.append((output_date, None, None, weight))

# Plot -------------------------------------------------------------------------
x, a, b, c = zip(*data)
x = dates.datestr2num(x)

plt.plot(x, b, 'k') # activity
plt.plot(x, a, 'b') # rate
plt.plot(x, c, 'r.') # scale
plt.gcf().autofmt_xdate()
plt.gca().xaxis.set_major_formatter(dates.DateFormatter('%Y-%m-%d'))
plt.title('All Data')
plt.grid()
plt.show()

