# FF_SmsServer
ESP8266/A6-GA6 SMS server with HTTP/MQTT/Serial interfaces

## What's for?
This code implements an SMS gateway, based on ESP8266 and A6/GA6 GSM modem chip (other may work too).

Messages to send are read on Serial and/or MQTT and/or HTTP.

Received SMS from restricted phone numbers are pushed to Serial and/or MQTT. With associated examples,
	- it's possible to execute bash commands on remote Unix nodes,
	- as well as reading/changing state of Domoticz devices.

It may have an external relay/NPN to power cycle and reset GSM chip, or a direct connection to modem's reset pin.

It may also have external watchdog circuit (https://github.com/FlyingDomotic/FF_Watchdog being a good idea).

It uses FF_WebServer (https://github.com/FlyingDomotic/FF_WebServer) to implement a fully asynchronous Web server,
	- with MQTT connection, Arduino and Web OTA,
	- telnet serial debug, 
	- serial and syslog trace, 
	- external hardware watchdog,
	- and local file system to host user and server files.

Used ESP8266 PIN:
	- D1 connected on isolation relay or A6 reset pin (optional)
	- D4 connected to hardware watchdog trigger pin (optional)
	- D5 connected on A6 U_RX PIN
	- D6 connected on A6 U_TX PIN

## Prerequisites

Can be used directly with Arduino IDE or PlatformIO.

The following libraries are used:
	- https://github.com/FlyingDomotic/FF_Trace
	- https://github.com/FlyingDomotic/FF_WebServer
	- https://github.com/FlyingDomotic/FF_Interval
	- https://github.com/FlyingDomotic/FF_A6lib
	- https://github.com/me-no-dev/ESPAsyncTCP
	- https://github.com/gmag11/NtpClient
	- https://github.com/me-no-dev/ESPAsyncWebServer
	- https://github.com/bblanchon/ArduinoJson
	- https://github.com/arcao/Syslog
	- https://github.com/marvinroger/async-mqtt-client
	- https://github.com/mgaman/PDUlib
	- https://github.com/joaolopesf/RemoteDebug@2.1.2

## Installation

Clone repository somewhere on your disk.
```
cd <where_you_want_to_install_it>
git clone https://github.com/FlyingDomotic/FF_SmsServer.git FF_SmsServer
```

Note that <where_you_want_to_install_it> could ingeniously be your compiler libraries folder ;-)

## Update

Go to code folder and pull new version:
```
cd <where_you_installed_FF_SmsServer>
git pull
```

Note: if you did any changes to files and `git pull` command doesn't work for you anymore, you could stash all local changes using:
```
git stash
```
or
```
git checkout <modified file>
```

## Parameters at compile time

The following parameters are used at compile time:
	ISOLATION_RELAY_PIN: (default=D1) ESP8266 pin where 5V isolation relay is connected to (don't define it if not used)
	RELAY_ON: (default=HIGH) Level to set 5V relay on/reset PIN active (useless if ISOLATION_RELAY_PIN not defined)
	RELAY_OFF: (default=LOW) Level to set 5V relay off/reset PIN inactive (useless if ISOLATION_RELAY_PIN not defined)
	ISOLATION_TIME: (default 2000) Time (ms) to keep A6 powered off/reset (useless if ISOLATION_RELAY_PIN not defined)
	MAX_RESTART: (default 10) Reset CPU after this count of modem restart

In addition, the following parameters can be defined for FF_WebServer:
	REMOTE_DEBUG: (default=defined) Enable telnet remote debug (optional)
	SERIAL_DEBUG: (default=defined) Enable serial debug (optional, only if REMOTE_DEBUG is disabled)
	HARDWARE_WATCHDOG_PIN: (default=D4) Enable watchdog external circuit on D4 (optional)
	HARDWARE_WATCHDOG_ON_DELAY: (default=5000) Define watchdog level on delay (in ms)
	HARDWARE_WATCHDOG_OFF_DELAY: (default=1) Define watchdog level off delay (in ms)
	FF_TRACE_KEEP_ALIVE: (default=5 * 60 * 1000) Trace keep alive timer (optional)
	FF_TRACE_USE_SYSLOG: (default=defined) Send trace messages on Syslog (optional)
	FF_TRACE_USE_SERIAL: (default=defined) Send trace messages on Serial (optional)
	INCLUDE_DOMOTICZ:(default=defined) Include Domoticz support (optional)
	CONNECTION_LED: (default=-1) Connection LED pin, -1 to disable
	AP_ENABLE_BUTTON: (default=-1) Button pin to enable AP during startup for configuration, -1 to disable
	AP_ENABLE_TIMEOUT: (default=240) If the device can not connect to WiFi it will switch to AP mode after this time (Seconds, max 255),  -1 to disable
	DEBUG_FF_WEBSERVER: (default=defined) Enable internal FF_WebServer debug
	FF_DISABLE_DEFAULT_TRACE: (default=not defined) Disable default trace callback
	FF_TRACE_USE_SYSLOG: (default=defined) SYSLOG to be used for trace

## Parameters defined at run time

The following parameters can be defined, either in json files before loading LittleFS file system, or through internal http server.

### In config.json:
	ssid: Wifi SSID to connect to
	pass: Wifi key
	ip: this node's fix IP address (useless if DHCP is true)
	netmask: this node's netmask (useless if DHCP is true)
	gateway: this node's IP gateway (useless if DHCP is true)
	dns: this node's DHCP server (useless if DHCP is true)
	dhcp: use DHCP if true, fix IP address else
	ntp: NTP server to use
	NTPperiod: interval between two NTP updates (in minutes)
	timeZone: NTP time zone (use internal HTTP server to set)
	daylight: true if DST is used
	deviceName: this node name

### In userconfig.json:
	MQTTClientID: MQTT client ID, used during connection to MQTT
	MQTTUser: User to connect to MQTT server²
	MQTTPass: Password to connect to MQTT server
	MQTTTopic: base MQTT topic, used to set LWT topic of SMS server
	MQTTHost: MQTT server to connect to
	MQTTPort: MQTT port to connect to
	MQTTInterval: Domoticz update interval (useless)
	SyslogServer: syslog server to use (empty if not to be used)
	SyslogPort: syslog port to use (empty if not to be used)
	mqttSendTopic: MQTT topic to write received SMS to
	mqttGetTopic: MQTT topic to read SMS messages to send
	mqttLwtTopic: root MQTT last will topic to to read application status
	allowedNumbers: allowed senders phone numbers, comma separated
	mqttLogTopic: MQTT topic to send log messages

## Available URLs

WebServer answers to the following URLs. If authentication is turned on, some of them needs user to be authenticated before being served.

### FF_SmsServer URLs
- /rest/params=&number=01234567&message=my+test+message: sends a SMS (number is phone number, message is ... message)
- /rest/send=&number=01234567&message=my+test+message: same as above
- /rest/listening: returns list of connected applications

### FF_WebServer URLs
- /list?dir=/ -> list file system content
- /edit -> load editor (GET) , create file (PUT), delete file (DELETE)
- /admin/generalvalues -> return deviceName and userVersion in json format
- /admin/values -> return values to be loaded in index.html and indexuser.html
- /admin/connectionstate -> return connection state
- /admin/infovalues -> return network info data
- /admin/ntpvalues -> return ntp data
- /scan -> return wifi network scan data
- /admin/restart -> restart ESP
- /admin/wwwauth -> ask for authentication
- /admin -> return admin.html contents
- /update/updatepossible
- /setmd5 -> set MD5 OTA file value
- /update -> update system with OTA file
- /rconfig (GET) -> get configuration data
- /pconfig (POST) -> set configuration data
- /rest -> activate a rest request to get some (user's) values (*)
- /rest/values -> -> return values to be loaded in index.html and index_user.html (*)
- /json -> activate a rest request to get some (user's) values (*)
- /post  -> activate a post request to set some (user's) values (*)

## MQTT topics

The following MQTT topics are used:

### MQTTTopic

Base MQTT topic, used to write LWT topic of SMS server
For example: smsServer
LWT format: when SMS server active: {"state":"up","version":"[SMS server version],[FF_WebServer version]"} , when down: {"state":"down"}

### mqttSendTopic

MQTT topic to write received SMS to
For example: smsServer/received
Format: {"number":"[phone number of sender]", "message":"[Message sent to SMS server]"}

### mqttGetTopic

MQTT topic to read SMS messages to send
For example: smsServer/toSend
Format: {"number":"[phone number to send message to]", "message":"[Message to send]"}

### mqttLwtTopic

Root MQTT last will topic to to read application status
For example: smsServer/LWT (will be followed by node/application name, i.e. smsServer/LWT/myNode)
Format: {"state":"[State of node/application]"}, probably "up" or "down", displayed on main Web server page, and returned to answer of /rest/listening request

### mqttCommandTopic

MQTT topic to read debug commands to be executed
For example: smsServer/command
Format: [Command to be exxecuted] List of available commands can be returned sending "?" or "h" command

## Associated files

Here's list of associated files

##examples/SmsHandler.py

Reads received SMS through MQTT, to isolate messages starting with this node name. 
When found, rest of message is executed as OS local command.
Result, output and errors are then sent back by mail.
If answer is shorter than 70 characters, it will be also sent back by SMS.
Else result code will be sent back to sender.
Traces are kept in a log file, rotated each week.

MQTT and mail settings are to be set into file before running it, probably as a service.