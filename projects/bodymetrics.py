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

database = sys.argv[1]
print("database: ", database)
con = sqlite3.connect(database)
cursor = con.cursor()

#cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
#print(cursor.fetchall())

cursor.execute("SELECT * FROM scaleMeasurements")
rows = cursor.fetchall()
for row in rows:
  #print(row)
  #print(row[3:5])
  weight = row[4]
  timestamp = row[3] / 1000
  output_date = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M')
  print((output_date, weight))

