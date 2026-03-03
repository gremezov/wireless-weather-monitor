#!/usr/bin/python3

# This file is a CGI script that processes the received sensor data and then
# stores it in an SQL database

import os
import cgi	# note: the cgi module will be removed in python 3.13
import sqlite3

from shared import sendStatus

from config import db_filename
from config import valid_sensors
from config import api_key

# check if method is not POST
if os.environ.get("REQUEST_METHOD") != "POST":
	sendStatus(405, "method not allowed")
	exit()

form = cgi.FieldStorage()

# check if incorrect API key
if form.getvalue("api_key") != api_key:
	sendStatus(401, "invalid api key")
	exit()

sensorname = form.getvalue("sensor_name")
entries_s = form.getvalue("entries")

# some sanity checks on the meta data
if sensorname == None or not(entries_s.isdigit()):
	sendStatus(500, "malformed meta data")
	exit()

# check if the sensor name is known
if sensorname not in valid_sensors:
	sendStatus(500, "unknown sensor")
	exit()

# check if file unwriteable
if not(os.access(db_filename, os.W_OK)):
	sendStatus(500, "database file unwriteable")
	exit()

# connect to database to write the data
with sqlite3.connect(db_filename) as conn:
	cur = conn.cursor()

	entries = int(entries_s)

	# form the data into a list
	data = [[form.getvalue("time"+str(i)), form.getvalue("pres"+str(i)), form.getvalue("temp"+str(i))] for i in range(entries)]

	# sort based on acquizition time (because the sensor sometimes sends readings not in time order)
	data.sort(key=lambda x:int(x[0]))

	# write data to database
	# note: sensorname is SQL injection safe because it was previously checked to be in valid_sensors
	cur.execute("CREATE TABLE IF NOT EXISTS "+sensorname+"(time INTEGER, pressure REAL, temperature REAL)")
	for d in data:
		cur.execute("INSERT INTO "+sensorname+" VALUES(?, ?, ?)", (int(d[0]), float(d[1]), float(d[2])))
	conn.commit()

	sendStatus(200, "OK")
