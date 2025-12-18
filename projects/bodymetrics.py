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

import os, sys, glob, sqlite3, datetime, warnings
import numpy as np
from scipy.signal import medfilt, lfilter
from matplotlib.dates import date2num
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as dates

def read_and_plot(path, do_pdf=False):
  # Band Data/Scale Measurements
  (band_x, band_r, band_i) = ([], [], [])
  (scale_x, scale_y, running_workouts) = ([], [], [])
  cursor1 = sqlite3.connect(path + "/Gadgetbridge").cursor().execute("""
    SELECT TIMESTAMP, RAW_INTENSITY, HEART_RATE FROM MI_BAND_ACTIVITY_SAMPLE UNION ALL
    SELECT TIMESTAMP, RAW_INTENSITY, HEART_RATE FROM XIAOMI_ACTIVITY_SAMPLE ORDER BY TIMESTAMP ASC""")
  cursor2 = sqlite3.connect(path + "/Gadgetbridge").cursor().execute("SELECT * FROM MI_SCALE_WEIGHT_SAMPLE")
  cursor3 = sqlite3.connect(path + "/Gadgetbridge").cursor().execute("SELECT START_TIME FROM BASE_ACTIVITY_SUMMARY WHERE ACTIVITY_KIND IN (16, 67109041)")
  for row in cursor1.fetchall():
    rate = row[2]
    if rate < 250 and rate > 20:
      band_x.append(datetime.datetime.fromtimestamp(row[0]))
      band_r.append(rate)
      band_i.append(row[1] / 255 * 40) # convert range to 0 to 40
  for row in cursor2.fetchall():
    weight = row[3]
    if weight > 72: # min scale
      scale_x.append(datetime.datetime.fromtimestamp(row[0] / 1000))
      scale_y.append(weight)
  for row in cursor3.fetchall():
    running_workouts.append(datetime.datetime.fromtimestamp(row[0] / 1000))

  # Pressure
  (pressure_x, pressure_y) = ([], [])
  with open(path + "/pressure.txt", 'r') as file:
    for line in file:
      if line.strip():
        parts = line.split(',')
        for reading in [reading.strip() for reading in parts[1:]]:
          pressure_x.append(datetime.datetime.strptime(parts[0].strip(), '%Y-%m-%d %H:%M:%S'))
          pressure_y.append(int(reading.split('/')[0]))

  # Special, Comparison
  (special_x, special_y, comparison_x, comparison_y) = ([], [], [], [])
  special, comparison = load_rr(path, last_num_plots=0, do_plot=False, create_pdf=do_pdf)
  for cur_s in special:
    special_x.append(cur_s[0])
    special_y.append(100 / float(cur_s[1]))
  for cur_c in comparison:
    if int(cur_c[1]) > 20: # only plausible values
      comparison_x.append(cur_c[0])
      comparison_y.append(int(cur_c[1]))

  # scale: polynomial fitting
  warnings.simplefilter("ignore", np.RankWarning)
  (scale_x_red, scale_y_red) = (scale_x[-600:], scale_y[-600:])
  numeric_x   = date2num(scale_x_red)
  polynomial  = np.poly1d(np.polyfit(numeric_x, scale_y_red, 10))
  scale_x_fit = np.linspace(min(numeric_x), max(numeric_x), 500)
  scale_y_fit = polynomial(scale_x_fit)

  # band: moving window minimum with additional IIR low pass filtering
  window_size     = 60 * 24
  alpha           = 0.0001
  band_r_median   = medfilt(band_r, kernel_size=3) # remove erroneous single spikes
  moving_min      = pd.Series(band_r_median).rolling(window_size, center=True).min()
  zi              = [moving_min[int(window_size / 2):4 * window_size].mean() * (1 - alpha)]
  iir_filtered, _ = lfilter([alpha], [1, alpha - 1], moving_min.bfill(), zi=zi)

  # restrict displayed data to a defined limit of days to consider
  time_limit_hist        = datetime.datetime.now() - datetime.timedelta(days=500)
  time_limit             = datetime.datetime.now() - datetime.timedelta(days=1000)
  pressure_y_hist        =      [y      for x, y in zip(pressure_x, pressure_y) if x >= time_limit_hist]
  pressure_x, pressure_y = zip(*[(x, y) for x, y in zip(pressure_x, pressure_y) if x >= time_limit])
  scale_x, scale_y       = zip(*[(x, y) for x, y in zip(scale_x,    scale_y)    if x >= time_limit])

  # pressure: split before/after 10AM
  pressure_x1, pressure_y1 = zip(*[(x, y) for x, y in zip(pressure_x, pressure_y) if x.hour < 10])
  pressure_x2, pressure_y2 = zip(*[(x, y) for x, y in zip(pressure_x, pressure_y) if x.hour >= 10])

  # Plot
  fig, (ax1, ax2) = plt.subplots(1, 2, gridspec_kw={'width_ratios': [6, 1]}, figsize=(10, 8))
  #ax1.plot(band_x,       band_i,        'k', linewidth=1)
  #ax1.plot(band_x,       band_r,        'b', linewidth=1)
  #ax1.plot(band_x,       band_r_median, 'g', linewidth=1)
  #ax1.plot(band_x,       moving_min,    'b', linewidth=1)
  ax1.plot(band_x, iir_filtered,   'b', linewidth=2)
  #ax1.plot(comparison_x, comparison_y,  'b.')
  ax1.plot(scale_x,      scale_y,      'k.')
  ax1.plot(scale_x_fit,  scale_y_fit,  'g.')
  #ax1.plot(pressure_x,   pressure_y,    'r.')
  ax1.plot(pressure_x1,   pressure_y1,   'g.')
  ax1.plot(pressure_x2,   pressure_y2,   'r.')
  ax1.plot(special_x,    special_y,    'yD')
  ax1.plot(running_workouts, [0] * len(running_workouts), 'r^')
  ax1.hlines(79,  min(scale_x),    max(scale_x),    colors='k', linestyles='dashed', linewidths=1)
  ax1.hlines(120, min(pressure_x), max(pressure_x), colors='g', linestyles='dashed', linewidths=1)
  ax1.hlines(136, min(pressure_x), max(pressure_x), colors='r', linestyles='dashed', linewidths=1)
  #ax1.hlines(40,  min(band_x),     max(band_x),     colors='k', linestyles='solid',  linewidths=1)
  ax1.hlines(46,   min(band_x),    max(band_x),     colors='b', linestyles='dashed', linewidths=1)
  ax1.hlines(51.5, min(band_x),    max(band_x),     colors='r', linestyles='dashed', linewidths=1)
  ax1.set_title('All Data')
  ax1.grid()
  ax1.xaxis.set_major_formatter(dates.DateFormatter('%Y-%m-%d,%H'))
  ax2.hist(pressure_y_hist, bins=30)
  ax2.set_title('Pressure,last 200 days')
  fig.autofmt_xdate()
  plt.tight_layout()
  plt.show(block=not do_pdf)


def load_rr(path, last_num_plots=4, create_pdf=False, do_plot=True):
  files = glob.glob(path + '/*.csv')
  if last_num_plots > 0 and len(files) > last_num_plots:
    files = files[-last_num_plots:]

  (special_val, hr_all_time, hr_all_data) = ([], [], [])
  for i, file in enumerate(files):
    rr, approx_time_axis, pos, tot_min, cur_date, hr_time, hr_data = analyze(file)
    hr_all_time.extend(hr_time)
    hr_all_data.extend(hr_data)

    num_pos    = len(pos)
    ratio      = float('inf')
    title_text = ""
    if num_pos > 0:
      ratio      = tot_min / num_pos
      title_text = f", one peak per {round(ratio)} minutes"
    special_val.append([cur_date, ratio])

    # Plot
    if do_plot or create_pdf:
      num_plots = 4
      if i % num_plots == 0:
        fig, axs = plt.subplots(min(len(files) - i, num_plots), 1, figsize=(8, 10))
      if isinstance(axs, np.ndarray):
        ax = axs[i % num_plots]
      else:
        ax = axs # if only one plot, axs is not a list
      ax.plot(approx_time_axis, rr, linewidth=1)
      ax.plot(approx_time_axis[pos], rr[pos], 'r*', label=f"{num_pos} detected peaks")
      ax.set_title(f"{cur_date.strftime('%Y-%m-%d %H:%M')} RR" + title_text)
      ax.set_xlabel('minutes')
      ax.set_ylabel('RR/ms')
      ax.axis([0, approx_time_axis[-1], 0, 2000])
      ax.legend()
      ax.grid(True)
      fig.tight_layout()
  plt.show(block=not create_pdf)

  if create_pdf:
    for i in plt.get_fignums():
      plt.figure(i)
      plt.savefig(path + f'/rr{i}.pdf')
      plt.close()
  return special_val, zip(hr_all_time, hr_all_data)


def analyze(file):
  (data, hr_time, hr_data) = (np.array([]), [], [])
  with open(file, 'r') as f:
    for line in f:
      elements = line.strip().split(',')
      hr_time.append(datetime.datetime.strptime(elements[0].split('.')[0], '%Y-%m-%d %H:%M:%S'))
      hr_data.append(float(elements[1]))
      if len(elements[2].split()) > 0:
        data = np.append(data, list(map(float, elements[2].split())))

  y = data - medfilt(data, kernel_size=3)
  z = np.where(np.abs(y) > 100)[0] # only signal above noise level
  s = z[np.where(np.diff(z) == 1)[0]]

  tot_time_minutes = (hr_time[-1] - hr_time[0]).total_seconds() / 60
  approx_time_axis = np.linspace(0, tot_time_minutes, len(data))
  return data, approx_time_axis, s, round(tot_time_minutes), hr_time[0], hr_time, hr_data


if __name__ == "__main__":
  do_rr = len(sys.argv) > 2
  read_and_plot(sys.argv[1], do_rr)
  if do_rr:
    load_rr(sys.argv[1])

