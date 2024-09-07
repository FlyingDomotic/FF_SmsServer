#!/usr/bin/python3
"""
This file is part of FF_SmsServer (https://github.com/FlyingDomotic/FF_SmsServer)

It reads received SMS through MQTT and send them back.

This is useless, but may be a good starting point for your own code.

Traces are kept in a log file, rotated each week.

Author: Flying Domotic
License: GNU GPL V3
"""

fileVersion = "1.1.0"

import paho.mqtt.client as mqtt
import pathlib
import os
import socket
import random
import logging
import logging.handlers as handlers
import json
from datetime import datetime

def onConnect(client, userdata, flags, reasonCode, properties=None):
    # Check for connection state
    if reasonCode != 'Success' and str(reasonCode) != '0':
        logger.error(F"Failed to connect - Reason code={reasonCode}")
        return
    client.subscribe(MQTT_RECEIVE_TOPIC, 0)

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
        logger.info("Received >"+str(message)+"< from "+str(number)+" on "+str(date))
        # Compose SMS answer message
        message = "Received: "+message
        jsonAnswer = {}
        jsonAnswer['number'] = str(number)
        jsonAnswer['message'] = message
        answerMessage = json.dumps(jsonAnswer)
        logger.info("Answer: >"+answerMessage+"<")
        client.publish(MQTT_SEND_TOPIC, answerMessage)

def onSubscribe(client, userdata, mid, reasonCode, properties=None):
  pass

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

### Here are settings to be adapted to your context ###

# MQTT Settings
MQTT_BROKER = "*myMqttHost*"
MQTT_RECEIVE_TOPIC = "smsServer/received"
MQTT_SEND_TOPIC = "smsServer/toSend"
MQTT_LWT_TOPIC = "smsServer/LWT/"+hostName
MQTT_ID = "*myMqttUser*"
MQTT_KEY = "*myMqttKey*"

### End of settings ###

# Log settings
log_format = "%(asctime)s:%(levelname)s:%(message)s"
logger = logging.getLogger(cdeFile)
logger.setLevel(logging.INFO)
logHandler = handlers.TimedRotatingFileHandler(str(currentPath) + cdeFile +'_'+hostName+'.log', when='W0', interval=1)
logHandler.suffix = "%Y%m%d"
logHandler.setLevel(logging.INFO)
formatter = logging.Formatter(log_format)
logHandler.setFormatter(formatter)
logger.addHandler(logHandler)
logger.info('----- Starting on '+hostName+', version '+fileVersion+' -----')

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
mqttClient.on_subscribe = onSubscribe
mqttClient.username_pw_set(MQTT_ID, MQTT_KEY)
# Set Last Will Testament (QOS=0, retain=True)
mqttClient.will_set(MQTT_LWT_TOPIC, '{"state":"down"}', 0, True)
# Connect to MQTT
mqttClient.connect(MQTT_BROKER)
mqttClient.publish(MQTT_LWT_TOPIC, '{"state":"up", "version":"'+str(fileVersion)+'", "startDate":"'+str(datetime.now())+'"}', 0, True)
# Never give up!
mqttClient.loop_forever()
