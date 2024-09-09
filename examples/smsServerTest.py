#!/usr/bin/python3
"""
This file is part of FF_SmsServer (https://github.com/FlyingDomotic/FF_SmsServer)

It sends a test SMS to SMS server and checks it can read it back within a minute.

If not, it sends a error mail and restart SMS server if smsServerRestartUrl is defined in JSON file

Author: Flying Domotic
License: GNU GPL V3
"""

fileVersion = "1.0.0"

import pathlib
import os
import socket
import random
import smtplib
import email.utils
from email.mime.text import MIMEText
import logging
import json
import socket
import requests
from datetime import datetime
import paho.mqtt.client as mqtt

def onConnect(client, userdata, flags, reasonCode, properties=None):
    # Check for connection state
    if reasonCode != 'Success' and str(reasonCode) != '0':
        logger.error(F"Failed to connect - Reason code={reasonCode}")
        return
    # Subscribe to receive topic
    client.subscribe(userdata['mqttReceiveTopic'], 0)

def onSubscribe(client, userdata, mid, reasonCode, properties=None):
    # Compose SMS answer message
    jsonMessage = {}
    jsonMessage['number'] = userdata['number']
    jsonMessage['message'] = userdata['prefix'] + ' ' + userdata['message']
    sendMessage = json.dumps(jsonMessage)
    topic = userdata['mqttSendTopic']
    logger.info(F"Publish {sendMessage} to {topic}")
    # Send message to SMS server through MQTT
    client.publish(topic, sendMessage)

def onMessage(client, userdata, msg):
    payload = msg.payload.decode("UTF-8", errors='backslashreplace')
    logger.info('Received >'+payload+'< from '+msg.topic)
    try:
        jsonData = json.loads(payload)
    except:
        logger.exception(F"Can't decode payload >{msg.payload}< from topic {msg.topic}")
        return

    # Extract message
    message = getValue(jsonData, 'message').strip()
    if message == '' :
        logger.error(F"Can't find 'message' in {str(jsonData)}")
        return
    
    # Is message for us ?
    prefix = userdata['prefix']
    if message[:len(prefix)] == prefix:
        # Extract command
        command = message[len(prefix):].strip()
        # Is message the one sent?
        if command == userdata['message']:
            logger.info(F"Correct message received - SMS server working properly")
            global correctMessageReceived
            correctMessageReceived = True
        else:
            logger.error(F"Waiting for '{userdata['message']}', received '{command}' - Ignored!")
    else:
        logger.info(F"Ignoring {message}, not starting by {prefix}")

# Send an email to me
def sendMail(subject, message, mailServer, sender, to=''):
    with smtplib.SMTP(mailServer) as server:
        msg = MIMEText(message, _charset='UTF-8')
        msg['Subject'] = hostName +": "+subject
        msg['Date'] = email.utils.formatdate(localtime=1)
        msg['From'] = sender
        if to:
            msg['To'] = to
        else:
            msg['To'] = sender
        server.sendmail(msg['From'], msg['To'], msg.as_string())

# Returns a dictionary value giving a key or default value if not existing
def getValue(dict, key, default=''):
    if key in dict:
        if dict[key] == None:
            return default #or None
        else:
            return dict[key]
    else:
        return default

#   *****************
#   *** Main code ***
#   *****************

# Set current working directory to this python file folder
currentPath = pathlib.Path(__file__).parent.resolve()
os.chdir(currentPath)

# Get this host name
hostName = socket.gethostname()
prefix = F"Test{hostName}"

# Get this file name (w/o path & extension)
cdeFile = pathlib.Path(__file__).stem

# Log settings
logFile = str(cdeFile) + prefix + ".log"
# Delete log file if already existing
if os.path.exists(logFile):
    os.remove(logFile)
logger = logging.getLogger(cdeFile)
logging.basicConfig(filename=logFile, level=logging.INFO)
logger.setLevel(logging.INFO)
logger.info('----- Starting on '+hostName+', version '+fileVersion+' -----')

# Define global flag, set by callback routine
global correctMessageReceived
correctMessageReceived = False

# Use this python file name and random number as client name
random.seed()
mqttClientName = pathlib.Path(__file__).stem+'_{:x}'.format(random.randrange(65535))

# Read JSON configuration file
jsonFile = str(cdeFile)+".json"
try:
    with open(jsonFile, "r") as jsonStream:
        jsonBuffer = jsonStream.read()
    # Convert jsonBuffer to dictionary
    try:
        jsonData = json.loads(jsonBuffer)
    except Exception as e:
        logger.error(F"Error {str(e)} decoding {jsonBuffer}")
        exit(2)
except Exception as e:
    logger.error(F"Error {str(e)} opening {jsonFile}")
    exit(2)

# Check for JSON data
jsonError = False
dataList = {"mqttServer", "mqttPort", "mqttUser", "mqttPassword", "mqttReceiveTopic", "mqttSendTopic", 
    "mailSender", "mailServer", "smsServerNumber", "smsServerRestartUrl"}
for dataElement in dataList:
    if dataElement not in jsonData:
        logging.error(F"Mandatory parameter {dataElement} not present in {jsonFile}")
        jsonError = True

if jsonError:
    exit(2)

data = {}
data['mqttSendTopic'] = jsonData['mqttSendTopic']
data['mqttReceiveTopic'] = jsonData['mqttReceiveTopic']
data['number'] = jsonData['smsServerNumber']
data['message'] = F"Test from {hostName} {datetime.now().isoformat()}"
data['prefix'] = prefix

# Try to find CallbackAPIVersion (exists starting on version 2)
try:
    from paho.mqtt.enums import CallbackAPIVersion
    mqttClient = mqtt.Client(client_id=mqttClientName, userdata=data, callback_api_version=CallbackAPIVersion.VERSION2)
except AttributeError:
    mqttClient = mqtt.Client(client_id=mqttClientName, userdata=data)

# Set callbacks
mqttClient.on_message = onMessage
mqttClient.on_connect = onConnect
mqttClient.on_subscribe = onSubscribe

# Set user and password
if jsonData['mqttUser'] != "":
    mqttClient.username_pw_set(jsonData['mqttUser'], jsonData['mqttPassword'])

# Connect to MQTT
mqttClient.connect(host=jsonData['mqttServer'], port=jsonData['mqttPort'])

# Wait for answer for a minute
sleepCount = 60
while not correctMessageReceived and sleepCount > 0:
    mqttClient.loop(timeout=1.0)
    sleepCount -= 1

# Try to disconnect client, ignore errors
try:
    mqttClient.disconnect()
except:
    pass

# Was correct message received?
if correctMessageReceived:
    print("SMS server seems to work...")
else:    
    # if SMS restart command defined, restart SMS server
    if jsonData['smsServerRestartUrl'] != "":
        httpAnswer = requests.get(jsonData['smsServerRestartUrl'])
        if httpAnswer.status_code == 200:
            logging.info(F"Restarting ESP returned {httpAnswer.text}")
        else:
            logging.error(F"Restarting ESP returned error {httpAnswer.status_code} {httpAnswer.text}")
    # Stop logging
    logging.shutdown()
    # Open logging file
    with open(logFile, "rt") as logStream:
        errors = logStream.readlines()
    # Print all errors
    for line in errors:
        print(line, end="")
    # Send a mail with errors
    sendMail("SMS server not answering !!!", errors, jsonData['mailServer'], jsonData['mailSender'])
