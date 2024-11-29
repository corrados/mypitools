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
import sqlite3

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

def create_database(db_name="heart_rate.db"):
    """
    Creates an SQLite database and heart_rate table if not exists.
    
    Args:
    db_name (str): Name of the SQLite database file.
    
    Returns:
    sqlite3.Connection: Connection object to the database.
    """
    conn = sqlite3.connect(db_name)
    cursor = conn.cursor()
    
    # Create table if not exists
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS heart_rate (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            unix_timestamp REAL NOT NULL,
            heart_rate REAL NOT NULL
        )
    ''')
    conn.commit()
    return conn

def store_heart_rate_data(conn, data):
    """
    Stores heart rate data into the SQLite database.
    
    Args:
    conn (sqlite3.Connection): Connection object to the database.
    data (list of tuple): List of (unix_timestamp, heart_rate) tuples.
    """
    cursor = conn.cursor()
    
    # Insert data into the heart_rate table
    cursor.executemany('''
        INSERT INTO heart_rate (unix_timestamp, heart_rate)
        VALUES (?, ?)
    ''', data)
    conn.commit()

# Example usage
file_path = '/home/corrados/Schreibtisch/apple_health_export/Export.xml'
db_name = 'heart_rate.db'

# Extract heart rate data
heart_rate_data = extract_heart_rate(file_path)

# Create database and store data
conn = create_database(db_name)
store_heart_rate_data(conn, heart_rate_data)

# Close the database connection
conn.close()

print(f"Stored {len(heart_rate_data)} heart rate records in {db_name}.")

