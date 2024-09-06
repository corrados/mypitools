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

import os, sys, glob, sqlite3, datetime
import numpy as np
from scipy.signal import medfilt
import matplotlib.pyplot as plt
import matplotlib.dates as dates

def read_and_plot(path):
  min_scale         = 72
  database_bands    = [path + "/Gadgetbridge"]
  database_scale    = path + "/openScale.db"
  database_pressure = path + "/pressure.txt"
  data              = []

  # Band Data ------------------------------------------------------------------
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
       data.append((output_date, rate, raw_intensity, None, None, None, None))

  # Scale Measurements ---------------------------------------------------------
  con = sqlite3.connect(database_scale)
  cursor = con.cursor()

  cursor.execute("SELECT * FROM scaleMeasurements")
  rows = cursor.fetchall()
  for row in rows:
    weight = row[4]
    if weight > min_scale:
      timestamp = row[3] / 1000
      output_date = datetime.datetime.fromtimestamp(timestamp)
      data.append((output_date, None, None, weight, None, None, None))

  # Pressure -------------------------------------------------------------------
  with open(database_pressure, 'r') as file:
    for line in file:
      if line.strip():
        parts = line.split(',')
        date_time_str = parts[0].strip()
        output_date = datetime.datetime.strptime(date_time_str, '%Y-%m-%d %H:%M:%S')
        readings = [reading.strip() for reading in parts[1:]]
        for reading in readings:
          pressure = int(reading.split('/')[0])
          data.append((output_date, None, None, None, pressure, None, None))

  # Special, Comparison --------------------------------------------------------
  special, comparison = load_rr(path, last_num_plots=0, do_plot=False)
  for cur_s in special:
    data.append((cur_s[0], None, None, None, None, 100 / float(cur_s[1]), None))
  for cur_c in comparison:
    data.append((cur_c[0], None, None, None, None, None, int(cur_c[1])))

  # Plot -----------------------------------------------------------------------
  x, a, b, c, d, e, f = zip(*data)
  plt.plot(x, b, 'k') # activity
  plt.plot(x, a, 'b') # rate
  plt.plot(x, f, 'g.') # comparison
  plt.plot(x, c, 'k.') # scale
  plt.plot(x, d, 'r.') # pressure
  plt.plot(x, e, 'y*') # special
  plt.gcf().autofmt_xdate()
  plt.gca().xaxis.set_major_formatter(dates.DateFormatter('%Y-%m-%d,%H'))
  plt.title('All Data')
  plt.grid()
  plt.show()


def load_rr(path, last_num_plots=4, create_pdf=False, do_plot=True):
  files = glob.glob(path + '/*.csv')
  if last_num_plots > 0 and len(files) > last_num_plots:
    files = files[-last_num_plots:]
  N = len(files)

  num_plots   = 4
  special_val = []
  hr_all_time = []
  hr_all_data = []
  for i, file in enumerate(files):
    x, approx_time_axis, s, tot_min, cur_date, hr_time, hr_data = analyze(file)
    hr_all_time.extend(hr_time)
    hr_all_data.extend(hr_data)

    num_s      = len(s)
    title_text = ""
    ratio      = float('inf')
    if num_s > 0:
      ratio      = tot_min / num_s
      title_text = f", one peak per {round(ratio)} minutes"
    special_val.append([cur_date, ratio])

    if do_plot:
      if i % num_plots == 0:
        fig, axs = plt.subplots(min(N - i, num_plots), 1, figsize=(8, 10))
      if isinstance(axs, np.ndarray):
        ax = axs[i % num_plots]
      else:
        ax = axs # if only one plot, axs is not a list
      ax.plot(approx_time_axis, x)
      ax.plot(approx_time_axis[s], x[s], 'r*')
      ax.set_title(f"{cur_date} RR" + title_text)
      ax.set_xlabel('minutes')
      ax.set_ylabel('RR/ms')
      ax.axis([0, approx_time_axis[-1], 0, 2000])
      ax.grid(True)
      fig.tight_layout()
  plt.show()

  if create_pdf:
    for i in plt.get_fignums():
      plt.figure(i)
      plt.savefig(f'rr{i}.pdf')
      plt.close()
  return special_val, zip(hr_all_time, hr_all_data)

def analyze(file):
  data    = []
  hr_time = []
  hr_data = []

  with open(file, 'r') as f:
    for line in f:
      elements = line.strip().split(',')
      cur_datetime = elements[0].split('.')[0]
      hr_time.append(cur_datetime)
      hr_data.append(float(elements[1]))
      if len(elements[2].split()) > 0:
        data_values = list(map(float, elements[2].split()))
        data.extend(data_values)

  data = np.array(data)
  y = data - medfilt(data, kernel_size=3)
  noise_limit = 100
  z = np.where(np.abs(y) > noise_limit)[0]
  s = z[np.where(np.diff(z) == 1)[0]]

  tot_time_minutes = (datetime.datetime.strptime(hr_time[-1], '%Y-%m-%d %H:%M:%S') -
                     datetime.datetime.strptime(hr_time[0], '%Y-%m-%d %H:%M:%S')).total_seconds() / 60
  approx_time_axis = np.linspace(0, tot_time_minutes, len(data))

  return data, approx_time_axis, s, round(tot_time_minutes), hr_time[0], hr_time, hr_data


if __name__ == "__main__":
  read_and_plot(sys.argv[1])
  #load_rr(sys.argv[1], last_num_plots=7)

