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

fileVersion = "1.0.0"

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

def on_connect(client, userdata, flags, rc):
  mqttClient.subscribe(MQTT_RECEIVE_TOPIC, 0)

def on_message(mosq, obj, msg):
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
        command = message[len(hostName):].strip()
        logger.info("Command="+command)
        try:
            args = shlex.split(command)
        except ValueError as err:
            logger.error("Command split failed with error "+str(err))
            response = "Error: "+str(err)
            data = bytes(response, 'UTF-8')
            logger.info("Response: "+response)
            sendMail(command, response)
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
                data = bytes(response, 'UTF-8')
                sendMail(command, log)
            except OSError as err:
                logger.error("Command execution failed with error "+str(err))
                response = "Error: {:s}".format(err.strerror)
                logger.info("Response: "+response)
                sendMail(command, response)
        # compose SMS answer message
        jsonAnswer = {}
        jsonAnswer['number'] = str(number)
        jsonAnswer['message'] = response
        answerMessage = json.dumps(jsonAnswer)
        logger.info("Answer: >"+answerMessage+"<")
        mqttClient.publish(MQTT_SEND_TOPIC, answerMessage)
    else:
        logger.info("Ignoring "+message)

def on_subscribe(mosq, obj, mid, granted_qos):
  pass

# Send an email to me
def sendMail(subject, message, to=''):
    with smtplib.SMTP_SSL(mailServer, mailPort, context=ssl.create_default_context()) as server:
        server.login(mailUser, mailKey)
        msg = MIMEText(message, _charset='UTF-8')
        msg['Subject'] = hostName +": "+subject
        msg['Date'] = email.utils.formatdate(localtime=1)
        msg['From'] = mailSender
        if to:
            msg['To'] = to
        else:
            msg['To'] = mailSender
        server.sendmail(msg['From'], {msg['To'], mailSender}, msg.as_string())

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

# Mail settings
mailSender = "*myMailSender*"
mailUser = "*myMailUser*"
mailKey = "*myMailKey*"
mailServer = "*myMailServer*"
mailPort = *myMailPort*

### End of settings ###
# Log settings
log_format = "%(asctime)s:%(levelname)s:%(message)s"
logger = logging.getLogger(appName)
logger.setLevel(logging.INFO)
logHandler = handlers.TimedRotatingFileHandler(str(currentpath) + cdeFile +'_'+hostName+'.log', when='W0', interval=1)
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
mqttClient = mqtt.Client(mqttClientName)
mqttClient.on_message = on_message
mqttClient.on_connect = on_connect
mqttClient.on_subscribe = on_subscribe
mqttClient.username_pw_set(MQTT_ID, MQTT_KEY)
# Set Last Will Testament (QOS=0, retain=True)
mqttClient.will_set(MQTT_LWT_TOPIC, '{"state":"down"}', 0, True)
# Connect to MQTT
mqttClient.connect(MQTT_BROKER)
mqttClient.publish(MQTT_LWT_TOPIC, '{"state":"up", "version":"'+str(fileVersion)+'", "startDate":"'+str(datetime.now())+'"}', 0, True)
# Never give up!
mqttClient.loop_forever()
