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

# Code generated with assistance from OpenAI's ChatGPT

import xml.etree.ElementTree as ET
from datetime import datetime
import matplotlib.pyplot as plt

def extract_heart_rate(file_path):
    """
    Extracts heart rate data from Apple Health export XML file.
    
    Args:
    file_path (str): Path to the export.xml file.
    
    Returns:
    list of tuple: A list of (unix_timestamp, heart_rate) tuples.
    """
    tree = ET.parse(file_path)
    root = tree.getroot()

    heart_rate_data = []

    # Iterate through all records
    for record in root.findall('Record'):
        if record.attrib.get('type') == "HKQuantityTypeIdentifierHeartRate":
            timestamp = record.attrib.get('startDate')
            value = record.attrib.get('value')

            # Convert to UNIX timestamp
            unix_timestamp = datetime.strptime(timestamp, "%Y-%m-%d %H:%M:%S %z").timestamp()

            # Append the data to the list
            heart_rate_data.append((unix_timestamp, float(value)))  # Heart rate values are in BPM
    
    return heart_rate_data

def plot_heart_rate(data):
    """
    Plots heart rate data using UNIX timestamps.
    
    Args:
    data (list of tuple): A list of (unix_timestamp, heart_rate) tuples.
    """
    timestamps, heart_rates = zip(*data)

    plt.figure(figsize=(10, 6))
    plt.plot(timestamps, heart_rates, label='Heart Rate', color='red', alpha=0.7)
    plt.xlabel("Time (UNIX Timestamp)")
    plt.ylabel("Heart Rate (BPM)")
    plt.title("Heart Rate Over Time")
    plt.grid(True)
    plt.legend()
    plt.show()

# Example usage:
file_path = '/home/corrados/Schreibtisch/apple_health_export/Export.xml'
heart_rate_data = extract_heart_rate(file_path)
plot_heart_rate(heart_rate_data)

