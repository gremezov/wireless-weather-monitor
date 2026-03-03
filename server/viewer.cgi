#!/usr/bin/python3

# This file is a CGI script that processes user requests for downloading sensor
# data and also presents the user with an interface for data selection

import os
import cgi	# note: the cgi module will be removed in python 3.13
import sqlite3
import io
import sys
import xlsxwriter
from urllib.parse import parse_qs

from shared import sendStatus

from config import db_filename
from config import valid_sensors

valid_varnames = {"time", "pressure", "temperature"}
valid_formats = ["excel", "text"]	# keep as array to preserve order in HTML form selection

if os.environ.get("REQUEST_METHOD") != "GET":
	sendStatus(405, "method not allowed")
	exit()

query_str = os.environ.get("QUERY_STRING")

if query_str != None and query_str != "":
	# if the request method is GET and the query string is present,
	# then send the data as requested in the query string

	params = parse_qs(query_str)
	varnames = params['v'][0].split(',')
	sensorname = params['s'][0]
	format = params['f'][0]

	# checks in case sensor name, format type, or variable names are invalid
	if not(set(varnames).issubset(valid_varnames)):
		sendStatus(500, "invalid variable names")
		exit()
	if sensorname not in valid_sensors:
		sendStatus(500, "unknown sensor")
		exit()
	if format not in valid_formats:
		sendStatus(500, "invalid format")
		exit()

	# connect to the database, read required data, and then send it in the required format
	with sqlite3.connect(db_filename) as conn:
		cur = conn.cursor()
		cur.execute("SELECT "+", ".join(varnames)+" FROM "+sensorname)
		data = cur.fetchall()

		if format == "text":
			print("Content-Type: text/plain\n")
			for row in data:
				for item in row:
					print(str(item)+" ", end="")
				print("")
		elif format == "excel":
			sys.stdout.write("Content-Type: application/vnd.openxmlformats-officedocument.spreadsheetml.sheet\n")
			sys.stdout.write("Content-Disposition: attachment; filename=\"weather_data.xlsx\";\n\n")

			# If sys.stdout.flush() is not added, then the above headers will not appear in the response.
			# Alternatively sys.stdout.buffer.write(b"headers...") can be used and then sys.stdout.flush()
			# is no longer needed.
			sys.stdout.flush()

			stream = io.BytesIO()	# allocate memory to handle excel file
			workbook = xlsxwriter.Workbook(stream, {'in-memory': True})
			worksheet = workbook.add_worksheet()

			worksheet.write(0, 0, "time (unix epoch time)")
			worksheet.write(0, 1, "pressure (pa)")
			worksheet.write(0, 2, "temperature (deg C)")

			for i,row in enumerate(data):
				for j,item in enumerate(row):
					worksheet.write(i+1, j, item)

			workbook.close()

			stream.seek(0)
			sys.stdout.buffer.write(stream.read())	# send binary stream over stdout

		conn.commit()
else:
	# if the request method is GET and there is no query string,
	# then present the user with a web form to select sensor data

	print("Content-Type: text/html\n")
	print("<html>")
	print("  <head>")
	print("    <meta charset='utf-8'>")
	print("    <meta name='viewport' content='width=device-width, initial-scale=1'>")
	print("    <link href='./style.css' rel='stylesheet' type='text/css'")
	print("  </head>")
	print("  <body>")
	print("    <h2><center>Download Sensor Data</center></h2>")
	print("    <form action='viewer.cgi' method='get'>")
	print("      <div class='selection'>")
	print("        <label for='sensorname'>Sensor Name: </label>")
	print("        <select id='sensorname' name='s'>")
	for sensor in valid_sensors:
		print(f"          <option value='{sensor}'>{sensor}</option>")
	print("        </select>")
	print("      </div>")
	print("      <div class='selection'>")
	print("        <label for='format'>Data Format: </label>")
	print("        <select id='format' name='f' class='selection'>")
	for format in valid_formats:
		print(f"          <option value='{format}'>{format}</option>")
	print("        </select>")
	print("      </div>")
	print("      <input type='hidden' id='varnames' name='v' value='time,pressure,temperature'/>")
	print("      <center><input type='submit' value='retrieve'/></center>")
	print("    </form>")
	print("  </body>")
	print("</html>")
