#!/usr/bin/python3
"""
This file is part of FF_SmsServer (https://github.com/FlyingDomotic/FF_SmsServer)

It reads received SMS through MQTT, to isolate messages starting with this node name.

When found, rest of message is executed as OS local command.

Result, output and errors are then sent back by mail.

If answer is shorter than 70 characters, it will be also sent back by SMS.

Else result code will be sent back to sender.

Traces are kept in a log file, rotated each week.

Author: Flying Domotic
License: GNU GPL V3
"""

fileVersion = "1.2.0"

import paho.mqtt.client as mqtt
import pathlib
import os
import socket
import random
import smtplib
import ssl
import email.utils
from email.mime.text import MIMEText
import logging
import logging.handlers as handlers
import json
import socket
import shlex
import subprocess
from datetime import datetime

def onConnect(client, userdata, flags, reasonCode, properties=None):
    # Check for connection state
    if reasonCode != 'Success' and str(reasonCode) != '0':
        logger.error(F"Failed to connect - Reason code={reasonCode}")
        return
    mqttClient.subscribe(MQTT_RECEIVE_TOPIC, 0)

def onMessage(client, userdata, msg):
    if msg.retain==0:
        payload = msg.payload.decode("UTF-8")
        logger.info('Received >'+payload+'< from '+msg.topic)
        try:
            jsonData = json.loads(payload)
        except:
            #logger.error("Can't decode payload")
            logger.exception("Can't decode payload")
            return
        number = getValue(jsonData, 'number').strip()
        date = getValue(jsonData, 'date').strip()
        message = getValue(jsonData, 'message').strip()
        if message == '' or date == '' or number == '':
            logger.error("Can't find 'number' or 'date' or 'message'")
            return
        if message[:len(hostName)].lower() == hostName.lower():
            receiver = ""
            if number in jsonData["mailReceivers"]:
                receiver = jsonData["mailReceivers"][number]
            command = message[len(hostName):].strip()
            logger.info("Command="+command)
            try:
                args = shlex.split(command)
            except ValueError as err:
                logger.error("Command split failed with error "+str(err))
                response = "Error: "+str(err)
                logger.info("Response: "+response)
                sendMail(command, response, receiver)
            else:
                logger.info("Args="+str(args))
                try:
                    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    log = result.stdout.decode('UTF-8')
                    logger.info("Log="+log)
                    response = "Result: {:d}".format(result.returncode)
                    logger.info("Response: "+response)
                    # Replace response code by full answer if short
                    if len(log) < 70:
                        response = log
                    if receiver != None:
                        sendMail(command, log, receiver)
                    else:
                        response += ", mail not in "+jsonFile
                except OSError as err:
                    logger.error("Command execution failed with error "+str(err))
                    response = "Error: {:s}".format(err.strerror)
                    logger.info("Response: "+response)
                    sendMail(command, response, receiver)
            # compose SMS answer message
            jsonAnswer = {}
            jsonAnswer['number'] = str(number)
            jsonAnswer['message'] = response
            answerMessage = json.dumps(jsonAnswer)
            logger.info("Answer: >"+answerMessage+"<")
            mqttClient.publish(MQTT_SEND_TOPIC, answerMessage)
        else:
            logger.info("Ignoring "+message)

# Send an email to me
def sendMail(subject, message, to=''):
    with smtplib.SMTP(mailServer) as server:
        msg = MIMEText(message, _charset='UTF-8')
        msg['Subject'] = hostName +": "+subject
        msg['Date'] = email.utils.formatdate(localtime=1)
        msg['From'] = mailSender
        if to:
            msg['To'] = to
        else:
            msg['To'] = mailSender
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

# Get this file name (w/o path & extension)
cdeFile = pathlib.Path(__file__).stem

# Log settings
log_format = "%(asctime)s:%(levelname)s:%(message)s"
logger = logging.getLogger(cdeFile)
logger.setLevel(logging.INFO)
logHandler = handlers.TimedRotatingFileHandler(os.path.join(currentPath, cdeFile +'_'+hostName+'.log'), when='W0', interval=1)
logHandler.suffix = "%Y%m%d"
logHandler.setLevel(logging.INFO)
formatter = logging.Formatter(log_format)
logHandler.setFormatter(formatter)
logger.addHandler(logHandler)
logger.info('----- Starting on '+hostName+', version '+fileVersion+' -----')

# Read JSON configuration file
jsonFile = "smsServerParameters.json"
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

# MQTT Settings
MQTT_BROKER = jsonData["mqttServer"]
MQTT_PORT = jsonData["mqttPort"]
MQTT_RECEIVE_TOPIC = jsonData["mqttReceiveTopic"]
MQTT_SEND_TOPIC = jsonData["mqttSendTopic"]
MQTT_LWT_TOPIC = jsonData["mqttLwtTopic"]+"/"+hostName
MQTT_ID = jsonData["mqttUser"]
MQTT_KEY = jsonData["mqttPassword"]

# Mail settings
mailSender = jsonData["mailSender"]
mailServer = jsonData["mailServer"]

# Use this python file name and random number as client name
random.seed()
mqttClientName = pathlib.Path(__file__).stem+'_{:x}'.format(random.randrange(65535))

# Initialize MQTT client
# Try to find CallbackAPIVersion (exists starting on version 2)
try:
    from paho.mqtt.enums import CallbackAPIVersion
    mqttClient = mqtt.Client(client_id=mqttClientName, callback_api_version=CallbackAPIVersion.VERSION2)
except AttributeError:
    mqttClient = mqtt.Client(client_id=mqttClientName)
except ModuleNotFoundError:
    mqttClient = mqtt.Client(client_id=mqttClientName)
mqttClient.on_message = onMessage
mqttClient.on_connect = onConnect
mqttClient.username_pw_set(MQTT_ID, MQTT_KEY)
# Set Last Will Testament (QOS=0, retain=True)
mqttClient.will_set(MQTT_LWT_TOPIC, '{"state":"down"}', 0, True)
# Connect to MQTT
mqttClient.connect(MQTT_BROKER, MQTT_PORT)
mqttClient.publish(MQTT_LWT_TOPIC, '{"state":"up", "version":"'+str(fileVersion)+'", "startDate":"'+str(datetime.now())+'"}', 0, True)
# Never give up!
mqttClient.loop_forever()
