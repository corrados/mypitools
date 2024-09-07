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

def read_and_plot(path, do_pdf=False):
  db_band     = path + "/Gadgetbridge"
  db_scale    = path + "/openScale.db"
  db_pressure = path + "/pressure.txt"

  # Band Data ------------------------------------------------------------------
  (band_x, band_r, band_i) = ([], [], [])
  cursor = sqlite3.connect(db_band).cursor().execute("SELECT * FROM MI_BAND_ACTIVITY_SAMPLE")
  for row in cursor.fetchall():
    rate = row[6]
    if rate < 250 and rate > 20:
      band_x.append(datetime.datetime.fromtimestamp(row[0]))
      band_r.append(rate)
      band_i.append(row[3] / 255 * 40) # convert range to 0 to 40

  # Scale Measurements ---------------------------------------------------------
  (scale_x, scale_y) = ([], [])
  cursor = sqlite3.connect(db_scale).cursor().execute("SELECT * FROM scaleMeasurements")
  for row in cursor.fetchall():
    weight = row[4]
    if weight > 72: # min scale
      scale_x.append(datetime.datetime.fromtimestamp(row[3] / 1000))
      scale_y.append(weight)

  # Pressure -------------------------------------------------------------------
  (pressure_x, pressure_y) = ([], [])
  with open(db_pressure, 'r') as file:
    for line in file:
      if line.strip():
        parts = line.split(',')
        for reading in [reading.strip() for reading in parts[1:]]:
          pressure_x.append(datetime.datetime.strptime(parts[0].strip(), '%Y-%m-%d %H:%M:%S'))
          pressure_y.append(int(reading.split('/')[0]))

  # Special, Comparison --------------------------------------------------------
  (special_x, special_y, comparison_x, comparison_y) = ([], [], [], [])
  special, comparison = load_rr(path, last_num_plots=0, do_plot=False, create_pdf=do_pdf)
  for cur_s in special:
    special_x.append(cur_s[0])
    special_y.append(100 / float(cur_s[1]))
  for cur_c in comparison:
    if int(cur_c[1]) > 20: # only plausible values
      comparison_x.append(cur_c[0])
      comparison_y.append(int(cur_c[1]))

  # Plot -----------------------------------------------------------------------
  plt.plot(band_x,       band_i,       'k', linewidth=1)
  plt.plot(band_x,       band_r,       'b', linewidth=1)
  plt.plot(comparison_x, comparison_y, 'b.')
  plt.plot(scale_x,      scale_y,      'k.')
  plt.plot(pressure_x,   pressure_y,   'r.')
  plt.plot(special_x,    special_y,    'yD')
  plt.hlines(79,  min(scale_x),    max(scale_x),    colors='k', linestyles='dashed', linewidths=1)
  plt.hlines(120, min(pressure_x), max(pressure_x), colors='r', linestyles='dashed', linewidths=1)
  plt.hlines(40,  min(band_x),     max(band_x),     colors='k', linestyles='solid',  linewidths=1)
  plt.gcf().autofmt_xdate()
  plt.gca().xaxis.set_major_formatter(dates.DateFormatter('%Y-%m-%d,%H'))
  plt.title('All Data')
  plt.grid()
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

