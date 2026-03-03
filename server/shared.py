# This file contains functions and procedures shared by the CGI scripts

# procedure for sending a status code + short plaintext message
def sendStatus(status, msg):
	print("Content-Type: text/plain")
	print(f"Status: {status}\n")
	print(msg)
