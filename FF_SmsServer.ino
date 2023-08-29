/*!

	FF_SmsServer	SMS Web server based on ESP8266 and A6 chip

	This code implements an SMS gateway, based on ESP8266 and A6/GA6 GSM modem chip (other may work too).

		Messages to send are read on Serial and/or MQTT and/or HTTP.

		Received SMS from restricted phone numbers are pushed to Serial and/or MQTT. With associated examples,
			it's possible to execute bash commands on remote Unix nodes,
			as well as reading/changing state of Domoticz devices.

		It may have an external relay/NPN to power cycle and reset GSM chip, or a direct connection to modem's reset pin.

		It may also have external watchdog circuit (https://github.com/FlyingDomotic/FF_Watchdog being a good idea).

		It uses FF_WebServer (https://github.com/FlyingDomotic/FF_WebServer) to implement a fully asynchronous Web server with:
			- MQTT connection
			- Arduino and Web OTA
			- local file system to host user and server files
			- file and/or browser based settings
			- full file editor/upload/download
			- optional telnet or serial or MQTT debug commands
			- optional serial and/or syslog trace
			- optional external hardware watchdog
			- optional Domoticz connectivity

		Used ESP8266 PIN:
			- D5 connected on isolation relay or A6 reset pin (optional)
			- D4 connected to hardware watchdog trigger pin (optional)
			- D7 connected on A6 U_RX PIN
			- D8 connected on A6 U_TX PIN

	Written and maintained by Flying Domotic (https://github.com/FlyingDomotic/FF_WebServer)

*/

#define VERSION "1.0.0"										// Version of this code
#include <FF_WebServer.h>									// WebServer class https://github.com/FlyingDomotic/FF_WebServer

//	User internal data
//		Declare here user's data needed by user's code
#include <FF_A6lib.h>										// A6 library https://github.com/FlyingDomotic/FF_A6lib

//	Optional Nano connected to ESP through I2C bus on D1/D2
#ifdef ATTACHED_NANO_SLAVE_ID
	#define ATTACHED_NANO_RESET_PIN D6						// Reset PIN of attached Nano
	#define ATTACHED_NANO_RESET_ACTIVE LOW					// Level to set to reset Nano
	#ifndef ATTACHED_NANO_RESET_TIME
		#define ATTACHED_NANO_RESET_TIME 2000				// Time to keep nano reset active
	#endif
#endif

// ----- SMS stuff -----
#define ISOLATION_RELAY_PIN D5								// ESP8266 pin where 5V isolation relay is connected to
#define RELAY_ON HIGH										// To set 5V relay on (A6 isolated/off)
#define RELAY_OFF LOW										// To set 5V relay off (A6 powered/on)
#ifndef ISOLATION_TIME
	#define ISOLATION_TIME 5000								// Time to keep A6 powered off/reset
#endif
#define MAX_RESTART 10										// Reset CPU after this count of modem restart

#define A6_MODEM_SPEED 9600									// A6 GSM modem requested speed
#define A6_MODEM_RX_PIN D8									// Modem RX pin
#define A6_MODEM_TX_PIN D7									// Modem TX pin

#define LOAD_CHAR(dest, src) strncpy_P(dest, PSTR(src), sizeof(dest))

unsigned long isolationTime = ISOLATION_TIME;				// Init isolation time
unsigned long isolationStartTime = 0;						// Init isolation start time
int restartCount = 0;										// Count of restart

String lastReceivedNumber = "";
String lastReceivedDate = "";
String lastReceivedMessage = "";
String lastSentNumber = "";
String lastSentDate = "";
String lastSentMessage = "";
String gsmState = "";
String messageBuffer = "";
String numberBuffer = "";
String listeningNodes = "";

// SMS hardware stuff
char lastSmsNumber[MAX_SMS_NUMBER_LEN];						// Last valid SMS caller
bool runFlag = true;										// A6 modem run flag (false = don't use A6)

#include <FF_A6lib.h>
#include <FF_Interval.h>
FF_Interval smsLoop(200, 200);						        // Execute SMS loop every 200 ms, first run after 200 ms

// These should be static in order for sscanf to work properly
static FF_A6lib A6Modem;									// A6 GSM Modem

// Routine/function definition
void sendBufferedSMS(void);
void sendSMS(const char* smsNumber, const char* smsMessage);
String cleanMessage(String message);
String getResetCause(void);

String resetCause = "";										// Used to save reset cause

//	User configuration data (should be in line with userconfigui.json)
//		Declare here user's configuration data needed by user's code

String mqttSendTopic = "";
String mqttGetTopic = "";
String mqttLwtTopic = "";
String allowedNumbers = "";

// Local variables
bool localDebugFlag = false;
bool localTraceFlag = false;

static void enterRoutine(const char* routineName);

// Declare here used callbacks
static CONFIG_CHANGED_CALLBACK(onConfigChangedCallback);
static DEBUG_COMMAND_CALLBACK(onDebugCommandCallback);
static SERIAL_COMMAND_CALLBACK(onSerialCommandCallback);
static REST_COMMAND_CALLBACK(onRestCommandCallback);
static MQTT_CONNECT_CALLBACK(onMqttConnectCallback);
static MQTT_MESSAGE_CALLBACK(onMqttMessageCallback);
static HELP_MESSAGE_CALLBACK(onHelpMessageCallback);

static void readSmsCallback(int pendingSmsIndex, const char* smsNumber, const char* smsDate, const char* smsMessage);

// Here is callback code

/*!

	This routine is called when permanent configuration data has been changed.
		User should call FF_WebServer.load_user_config to get values defined in userconfigui.json.
		Values in config.json may also be get here.

	\param	none
	\return	none

*/

CONFIG_CHANGED_CALLBACK(onConfigChangedCallback) {
	if (localTraceFlag) enterRoutine(__func__);
	trace_info_P("Loading user config", NULL);
	FF_WebServer.load_user_config("mqttSendTopic", mqttSendTopic);
    trace_info_P("mqttSendTopic=%s", mqttSendTopic.c_str());
	FF_WebServer.load_user_config("mqttGetTopic", mqttGetTopic);
    trace_info_P("mqttGetTopic=%s", mqttGetTopic.c_str());
	FF_WebServer.load_user_config("mqttLwtTopic", mqttLwtTopic);
    trace_info_P("mqttLwtTopic=%s", mqttLwtTopic.c_str());
	FF_WebServer.load_user_config("allowedNumbers", allowedNumbers);
    trace_info_P("allowedNumbers=%s", allowedNumbers.c_str());
}

/*!

	This routine is called when a user's debug command is received.

	User should analyze here debug command and execute them properly.

	\note	Note that standard commands are already taken in account by server and never passed here.

	\param[in]	command: last debug command entered by user
	\return	none

*/

DEBUG_COMMAND_CALLBACK(onDebugCommandCallback) {
	if (localTraceFlag) enterRoutine(__func__);
	// "user" command is a standard one used to print user variables
	if (debugCommand == "user") {
		// -- Add here your own user variables to print
		trace_info_P("mqttSendTopic=%s", mqttSendTopic.c_str());
		trace_info_P("mqttGetTopic=%s", mqttGetTopic.c_str());
		trace_info_P("mqttLwtTopic=%s", mqttLwtTopic.c_str());
		trace_info_P("allowedNumbers=%s", allowedNumbers.c_str());
		trace_info_P("runFlag=%d", runFlag);
		trace_info_P("gsmState=%s", gsmState.c_str());
		A6Modem.debugState();
		trace_info_P("lastReceivedNumber=%s", lastReceivedNumber.c_str());
		trace_info_P("lastReceivedDate=%s", lastReceivedDate.c_str());
		trace_info_P("lastReceivedMessage=%s", lastReceivedMessage.c_str());
		trace_info_P("lastSentNumber=%s", lastSentNumber.c_str());
		trace_info_P("lastSentDate=%s", lastSentDate.c_str());
		trace_info_P("lastSentMessage=%s", lastSentMessage.c_str());
		trace_info_P("listeningNodes=%s", listeningNodes.substring(2, listeningNodes.length()-2).c_str());
		trace_info_P("getFreeHeap()=%d", ESP.getFreeHeap());
		trace_info_P("getMaxFreeBlockSize()=%d", ESP.getMaxFreeBlockSize());
		trace_info_P("localTraceFlag=%d", localTraceFlag);
		trace_info_P("localDebugFlag=%d", localDebugFlag);
		// -----------
		return true;
	// Put here your own debug commands
	// -----------
	} else if (debugCommand == "a6debug") {
		A6Modem.debugFlag = !A6Modem.debugFlag;
		trace_info_P("a6-debug is now %d", A6Modem.debugFlag);
	} else if (debugCommand == "a6trace") {
		A6Modem.traceFlag = !A6Modem.traceFlag;
		trace_info_P("a6-trace is now %d", A6Modem.traceFlag);
	} else if (debugCommand == "run") {
		A6Modem.setRestart(false);
		runFlag = ! runFlag;
		trace_info_P("runFlag is now %d", runFlag);
	} else if (debugCommand == "restart") {
		A6Modem.setRestart(true);
		trace_info_P("Restarting modem", NULL);
	} else if (debugCommand[0] == 'A' && debugCommand[1] == 'T') {
		A6Modem.sendAT(debugCommand.c_str()); 
	} else if (debugCommand[0] == 'a' && debugCommand[1] == 't') {
		A6Modem.sendAT(debugCommand.c_str()); 
	} else if (debugCommand[0] == '>') {
		A6Modem.sendAT(debugCommand.substring(1).c_str()); 
	} else if (debugCommand == "eof") {
		A6Modem.sendEOF(); 
	} else if (debugCommand == "enter") {
		localTraceFlag = !localTraceFlag;
		trace_info_P("localTraceFlag is now %d", localTraceFlag);
	} else if (debugCommand == "call") {
		localDebugFlag = !localDebugFlag;
		trace_info_P("localDebugFlag is now %d", localDebugFlag);
	}
	return false;
}
/*!

	This routine is called when help message is to be printed

	\param	None
	\return	help message to be displayed

*/
HELP_MESSAGE_CALLBACK(onHelpMessageCallback) {
	if (localTraceFlag) enterRoutine(__func__);
	return PSTR("a6debug - toggle A6 modem debug flag\n\r"
				"a6trace - toggle A6 modem trace flag\n\r"
				"a6enter - toggle A6 modem entering routine trace flag\n\r"
				"enter - toggle routine entering trace flag\n\r"
				"call - toggle routine call trace flag\n\r"
				"run - Toggle A6 modem run flag\n\r"
				"restart - Restart A6 modem\n\r"
				"AT or at - Send AT command\n\r"
				"> - Send command without AT prefix\n\r"
				"eof - Send EOF\n\r"
	);
}

/*!

	This routine is called when a user Serial command is given

	User should analyze here these Serial commands and execute them properly.

	\note	Note that standard Serial commands are already taken in account by server and never passed here.

	\param[in]	command: last Serial command entered by user
	\return	none

*/

SERIAL_COMMAND_CALLBACK(onSerialCommandCallback) {
	if (localTraceFlag) enterRoutine(__func__);
	trace_info_P("Serial command >%s< received", command.c_str());
	// Only analyze message starting with "{"
	if (command[0] == '{') {
		// Analyze JSON message (will fail if payload is empty)
		DynamicJsonDocument jsonDoc(MAX_SMS_MESSAGE_LEN);
		auto error = deserializeJson(jsonDoc, command);
		if (error) {
			trace_error_P("Failed to parse >%s<. Error: %s", command.c_str(), error.c_str());
			#ifndef NO_SERIAL_COMMAND_CALLBACK
				#ifdef SEND_SMS_FROM_SERIAL
					Serial.printf(PSTR("{\"status\":\failed to parse\"}\n"));
				#endif
			#endif
			return false;
		}
		// Analyze message
		String message = jsonDoc["message"].as<const char *>();
		String number = jsonDoc["number"].as<const char *>();
		#ifndef NO_SERIAL_COMMAND_CALLBACK
			#ifdef SEND_SMS_FROM_SERIAL
				Serial.printf(PSTR("{\"status\":\"missing keyword\"}\n"));
			#endif
		#endif
		if (message == "" && number == "") {			// Check for message and number
			trace_error_P("Message and number missing from MQTT payload %s", command.c_str());
			return false;
		}
		if (message == "" ) {							// Check for message
			trace_error_P("Message missing from MQTT payload %s", command.c_str());
			return false;
		}
		if (number == "") {								// Check for number
			trace_error_P("Number missing from MQTT payload %s", command.c_str());
			return false;
		}
		sendSMS(number.c_str(), message.c_str());		// Ok, store the SMS in queue
		#ifndef NO_SERIAL_COMMAND_CALLBACK
			#ifdef SEND_SMS_FROM_SERIAL
				Serial.printf(PSTR("{\"status\":\"ok\"}\n"));
			#endif
		#endif
		return true;
	}
	return false;
}

/*!

	This routine analyze and execute REST commands sent through /rest GET command
	It should answer valid requests using a request->send(<error code>, <content type>, <content>) and returning true.

	If no valid command can be found, should return false, to let server returning an error message.

	\note	Note that minimal implementation should support at least /rest/values, which is requested by index.html
		to get list of values to display on root main page. This should at least contain "header" topic,
		displayed at top of page. Standard header contains device name, versions of user code, FF_WebServer template
		followed by device uptime. You may send something different.
		It should then contain user's values to be displayed by index_user.html file.

	\param[in]	request: AsyncWebServerRequest structure describing user's request
	\return	true for valid answered by request->send command, false else

*/
REST_COMMAND_CALLBACK(onRestCommandCallback) {
	if (localTraceFlag) enterRoutine(__func__);
    char tempBuffer[600];
    tempBuffer[0] = 0;
	if (request->url() == "/rest/values") {
		snprintf_P(tempBuffer, sizeof(tempBuffer),
			PSTR(
				// -- Put header composition
				"header|%s V%s/%s, up since %s|div\n"
				// -- Put here user variables to be available in index_user.html
				"lastReceivedNumber|%s|div\n"
				"lastReceivedDate|%s|div\n"
				"lastReceivedMessage|%s|div\n"
				"lastSentNumber|%s|div\n"
				"lastSentDate|%s|div\n"
				"lastSentMessage|%s|div\n"
				"gsmState|%s|div\n"
                "listeningNodes|%s|div\n"
				"resetCause|%s|div\n"
				"heapLargest|%d|div\n"
				"heapTotal|%d|div\n"
				// -----------------
				)
			// -- Put here values of header line
			,FF_WebServer.getDeviceName().c_str()
			,VERSION, FF_WebServer.getWebServerVersion()
			,NTP.getUptimeString().c_str()
			// -- Put here values in index_user.html
			,lastReceivedNumber.c_str()
			,lastReceivedDate.c_str()
			,cleanMessage(lastReceivedMessage).c_str()
			,lastSentNumber.c_str()
			,lastSentDate.c_str()
			,cleanMessage(lastSentMessage).c_str()
			,gsmState.c_str()
            ,(listeningNodes.length()>2)? listeningNodes.substring(2, listeningNodes.length()-2).c_str():"[None]"
			,resetCause.c_str()
			,ESP.getMaxFreeBlockSize()
			,ESP.getFreeHeap()
			// -----------------
			);
		request->send(200, "text/plain", tempBuffer);
		return true;
	} else if (request->url().startsWith("/rest/params&") || request->url().startsWith("/rest/send&")) {
		// /rest/params or /rest/send -> send an SMS
		//		&number=0123456789 : phone number to send message to
		//		&message=abcdefg hijklm nopqrs : message to send
		char paramList[200];								// Given parameters, excluding initial &
		char *params[3][2];									// Will contain 3 couples of param/value
		memset(paramList, 0, sizeof(paramList));			// Clear param list
		strncpy(paramList, 									// Copy to param list
			request->url().substring(request->url().indexOf('&')+1).c_str() // From URL
			, sizeof(paramList));							// Max size
		trace_debug_P("Params: %s (%d)", paramList, request->url().indexOf('&'));
		int nbParams = FF_WebServer.parseUrlParams(paramList, params, 3, true);	// Extract up to 3 parameters
		String number = "";
		String message = "";
		for (int i = 0; i < nbParams; i++) {				// Check given params
			if (!strcmp(params[i][0], "number")) {			// Extract number
				number = String(params[i][1]);
				number.replace(" ","+");
			} else if (!strcmp(params[i][0], "message")) {	// Extract message
				message = String(params[i][1]);
				message.replace("\n", "<br>");
			} else {										// Parameter is unknown
				snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("Unknown topic '%s'"), params[i][0]);
				trace_debug_P("%s", tempBuffer);
				request->send(400, "text/plain", tempBuffer);
				return true;
			}
		}
		if (number != "" && message != "") {				// Number and message should be given
			sendSMS(number.c_str(), message.c_str());		// Ok, sends the SMS
			snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("ok\n"));
			request->send(200, "text/plain", tempBuffer);
            return true;
		} else {											// Load error message
			 snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("Missing number and message"));
			if (number != "") {
				snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("Missing message"));
			}
			if (message != "") {
				snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("Missing number"));
			}
			trace_debug_P("%s", tempBuffer);
			request->send(400, "text/plain", tempBuffer);
            return true;
		}
	} else if (request->url() == "/rest/listening") {
        snprintf_P(tempBuffer, sizeof(tempBuffer),
            PSTR("%s")
            ,(listeningNodes.length()>2)? listeningNodes.substring(2, listeningNodes.length()-2).c_str():"[None]"
            );
        request->send(200, "text/plain", tempBuffer);
        return true;
	}
	return false;
}

/*!

	This routine is called each time MQTT is (re)connected

	\param	none
	\return	none

*/
MQTT_CONNECT_CALLBACK(onMqttConnectCallback) {
	if (localTraceFlag) enterRoutine(__func__);
	// Subscribe to MQTT topics we want to get
	if (mqttGetTopic != "") {
		trace_info_P("Subscribing to %s", mqttGetTopic.c_str());
		FF_WebServer.mqttSubscribeRaw(mqttGetTopic.c_str());
	}
	if (mqttLwtTopic != "") {
		trace_info_P("Subscribing to %s", (mqttLwtTopic+"+").c_str());
		FF_WebServer.mqttSubscribeRaw((mqttLwtTopic+"+").c_str());
	}
}

/*!

	This routine is called each time MQTT receives a subscribed topic

	\note	** Take care of long payload that will arrive in multiple packets **
            Take also care that payload is not null terminated, making a local (null ended) copy could be a good idea

	\param[in]	topic: received message topic
	\param[in]	payload: (part of) payload
	\param[in]	len: length of (this part of) payload
	\param[in]	index: index of (this part of) payload
	\param[in]	total: total length of all payload parts
	\return	none

*/
MQTT_MESSAGE_CALLBACK(onMqttMessageCallback) {
	if (localTraceFlag) enterRoutine(__func__);
    char localPayload[250];
    size_t localSize = sizeof(localPayload);

    if (len < localSize) localSize = len;                   // Maximize len to copy
    memset(localPayload, '\0', sizeof(localPayload));
    strncpy(localPayload, payload, localSize);

	trace_info_P("Received MQTT message %s on topic %s, len=%d, index=%d, total=%d", localPayload, topic, len, index, total);

	// Check for long (multi packets) messages
	if (len != total) {
		trace_error_P("Can't deal with multi packet messages!", NULL);
		return;
	}

	// Empty payload means deleted topic
	if (mqttLwtTopic != "" && !localPayload[0]) {			// Is LWT topic defined and payload empty?
		if (String(topic).substring(0, mqttLwtTopic.length()) == mqttLwtTopic) {	// Doest topic start with mqttLwtTopic?
			String node = String(topic).substring(mqttLwtTopic.length());	// Extract node part from topic
			trace_info_P("Node >%s< is deleted", node.c_str());
			// Try to locate node in listeningNodes
			uint8_t startPos = listeningNodes.indexOf(", "+node+":");
			if (startPos >=0) {
				// Locate end of node name
				uint8_t endPos = listeningNodes.indexOf(", ",startPos+1);
				// Remove node from list as it was deleted
				listeningNodes = listeningNodes.substring(0, startPos+1) + listeningNodes.substring(endPos+1);
			}
			return;
		}
	}

	// Analyze JSON message (will fail if payload is empty)
	DynamicJsonDocument jsonDoc(MAX_SMS_MESSAGE_LEN);
	auto error = deserializeJson(jsonDoc, localPayload);
	if (error) {
		trace_error_P("Failed to parse %s. Error: %s", localPayload, error.c_str());
		return;
	}
	// Analyze topic
	if (mqttGetTopic != "") {								// Is get topic defined?
		if (String(topic) == mqttGetTopic) {				// Is it the right topic?
			String message = jsonDoc["message"].as<const char *>();
			String number = jsonDoc["number"].as<const char *>();
			if (message == "" && number == "") {			// Check for message and number
				trace_error_P("Message and number missing from MQTT payload %s for topic %s", localPayload, topic);
				return;
			}
			if (message == "" ) {							// Check for message
				trace_error_P("Message missing from MQTT payload %s for topic %s", localPayload, topic);
				return;
			}
			if (number == "") {								// Check for number
				trace_error_P("Number missing from MQTT payload %s for topic %s", localPayload, topic);
				return;
			}
			sendSMS(number.c_str(), message.c_str());		// Ok, store the SMS in queue
		}
	}

	if (mqttLwtTopic != "") {								// Is LWT topic defined?
		if (String(topic).substring(0, mqttLwtTopic.length()) == mqttLwtTopic) {	// Doest topic start with mqttLwtTopic?
			String node = String(topic).substring(mqttLwtTopic.length());	// Extract node part from topic
			String state =  jsonDoc["state"].as<const char *>();	// Get state item
			if (state) {
				trace_info_P("Node >%s< is >%s<", node.c_str(), state.c_str());
                // Try to locate node in listeningNodes
                uint8_t startPos = listeningNodes.indexOf(", "+node+":");
                if (startPos >=0) {
                    uint8_t endPos = listeningNodes.indexOf(", ",startPos+1);
                    // Remove node from list
                    listeningNodes = listeningNodes.substring(0, startPos+1) + listeningNodes.substring(endPos+1);
                }
                // Add node at end of list
                listeningNodes += node + ":" + state + ", ";
			    return;
			} else {
				trace_error_P("State missing from MQTT payload %s for topic %s", localPayload, topic);
				return;
			}
		}
	}
	trace_error_P("Unknown topic %s received!", topic);
}

/*!

	This routine cleans \n and \r from a message

	\param[in]	message: message to be cleaned
	\return	Cleaned message

*/
String cleanMessage(String message){
	if (localTraceFlag) enterRoutine(__func__);
	String cleanedMessage = message;
	cleanedMessage.replace("\n", "\\n");
	cleanedMessage.replace("\r", "\\r");
	return cleanedMessage;
}

/*!

	This routine returns ESP reset reason

	Reset reason will be given in HEX and clear text format
	Exception will be given if existing
	For reset that include registers, EPC1-3, EXCVADDR and DEPC will also be given

	\param[in]	None
	\return	String containing ESP reset cause

*/
String getResetCause(void) {
	if (localTraceFlag) enterRoutine(__func__);
	struct rst_info *rtc_info = system_get_rst_info();
	// Get reset reason
	String reason = PSTR("Reset reason: ") + String(rtc_info->reason, HEX) + PSTR(" - ") + ESP.getResetReason();
	// In case of software restart, send additional info
	if (rtc_info->reason == REASON_WDT_RST || rtc_info->reason == REASON_EXCEPTION_RST || rtc_info->reason == REASON_SOFT_WDT_RST) {
		// If crashed, print exception
		if (rtc_info->reason == REASON_EXCEPTION_RST) {
			reason += PSTR(", exception (") + String(rtc_info->exccause)+PSTR("):");
		}
		reason += PSTR(" epc1=0x") + String(rtc_info->epc1, HEX) 
				+ PSTR(", epc2=0x") + String(rtc_info->epc2, HEX) 
				+ PSTR(", epc3=0x") + String(rtc_info->epc3, HEX) 
				+ PSTR(", excvaddr=0x") + String(rtc_info->excvaddr, HEX) 
				+ PSTR(", depc=0x") + String(rtc_info->depc, HEX);
	}
	return reason;
}

#ifdef ATTACHED_NANO_SLAVE_ID
	#include "Wire.h"
	unsigned long nanoResetTime = ATTACHED_NANO_RESET_TIME;	// Init nano reset time
	unsigned long nanoResetStartTime = 0;					// Init nano reset start time
	FF_Interval attachedNanoInterval(5, 5);					// Execute nano request every 5 ms

	void sendToSlave(uint8_t _command) {
		Wire.beginTransmission(ATTACHED_NANO_SLAVE_ID);
		Wire.write(_command);
		byte status = Wire.endTransmission();
		trace_info_P("Sending %d to slave, result %d", _command, status);
	}

	bool getFromSlave() {
		int nbChar = Wire.requestFrom(ATTACHED_NANO_SLAVE_ID, 2);
		int nbChar2 = Wire.available();
		if (nbChar != nbChar2) {
			trace_info_P("?? Request returned %d while available %d", nbChar, nbChar2);
		}
		if (nbChar == 2) {
			uint8_t c1 = Wire.read();
			uint8_t c2 = Wire.read();
			if (c1) {
				if (c1 == 254) {
					trace_info_P("Slave ready, version %x", c2);
					return false;
				}
				trace_info_P( "Received c1=%d, c2=%d", c1, c2);
				return true;
			}
		} else {
			if (nbChar) {
				trace_info_P("?? getFromSlave returned %d characters", nbChar);
				while (Wire.available()) {
					char c = Wire.read();
					trace_info_P("?? Discarding %d", c);
				}
			}
		}
		return false;
	}
#endif


//	This is the setup routine.
void setup() {
	// Initialize Strings with PSTR (flash) values
	lastReceivedNumber = PSTR("[none]");
	lastReceivedDate = PSTR("[never]");
	lastReceivedMessage = PSTR("[no message]");
	lastSentNumber = PSTR("[none]");
	lastSentDate = PSTR("[never]");
	lastSentMessage = PSTR("[no message]");
	gsmState = PSTR("Not started");
	listeningNodes = PSTR(", ");

	// Open serial connection
	Serial.begin(74880);
	// Start Little File System
	LittleFS.begin();
    // Enable debug
	resetCause = getResetCause();
    FF_TRACE.setLevel(FF_TRACE_LEVEL_DEBUG);
    //FF_WebServer.debugFlag = true;
    //FF_WebServer.traceFlag = true;
	//localDebugFlag = true;
	//localTraceFlag = true;
    // Set user's callbacks
    FF_WebServer.setConfigChangedCallback(&onConfigChangedCallback);
    FF_WebServer.setDebugCommandCallback(&onDebugCommandCallback);
    FF_WebServer.setSerialCommandCallback(&onSerialCommandCallback);
    FF_WebServer.setRestCommandCallback(&onRestCommandCallback);
    FF_WebServer.setMqttConnectCallback(&onMqttConnectCallback);
    FF_WebServer.setMqttMessageCallback(&onMqttMessageCallback);
	FF_WebServer.setHelpMessageCallback(&onHelpMessageCallback);

	// SMS server specific setup
	#ifdef ISOLATION_RELAY_PIN
		digitalWrite(ISOLATION_RELAY_PIN, RELAY_ON);		// Switch 5V relay on, isolate A6 power or set reset PIN
		pinMode(ISOLATION_RELAY_PIN, OUTPUT);
		isolationStartTime = millis();
	#endif
	#ifdef ATTACHED_NANO_SLAVE_ID
		digitalWrite(ATTACHED_NANO_RESET_PIN, ATTACHED_NANO_RESET_ACTIVE);	// Reset Nano
		pinMode(ATTACHED_NANO_RESET_PIN, OUTPUT);
		nanoResetStartTime = millis();
	#endif

    A6Modem.registerSmsCb(readSmsCallback);                 // SMS received callback
    A6Modem.debugFlag = true;
    A6Modem.traceFlag = false;
	//A6Modem.traceEnterFlag = true;
	#ifdef ATTACHED_NANO_SLAVE_ID
		// Start Wire as master
		Wire.begin();
		Wire.setClockStretchLimit(40000);    // Slave wait for answer time in µs (default to 230)
		delay(10);
	#endif

	// Start FF_WebServer
	FF_WebServer.begin(&LittleFS, VERSION);
}

//	This is the main loop.
//	Do what ever you want and call FF_WebServer.handle()
void loop() {
	// Manage Web Server
	FF_WebServer.handle();
	// User part of loop
	#ifdef ATTACHED_NANO_SLAVE_ID
		if (nanoResetTime){										// Are we resetting Nano?
			// Test for nano reset time-out
			if ((millis() - nanoResetStartTime) >= nanoResetTime) {
				nanoResetTime = 0;								// Clear nano reset
				pinMode(ATTACHED_NANO_RESET_PIN, INPUT);		// Deactivate output
				trace_info_P("End of Nano reset", NULL);
			}
		} else {
			// Read data from slave every 5 ms
			if (attachedNanoInterval.shouldRun()) {
				getFromSlave();
			}
		}
	#endif
	#ifdef ISOLATION_RELAY_PIN
		if (isolationTime) {									// Are we isolating power/resetting modem?
			// Test for isolation time-out
			if ((millis() - isolationStartTime) >= isolationTime) {
				isolationTime = 0;								// Clear isolation
				digitalWrite(ISOLATION_RELAY_PIN, RELAY_OFF);	// Switch 5V relay off
				trace_info_P("Isolation relay switched off", NULL);
				A6Modem.begin(A6_MODEM_SPEED, A6_MODEM_RX_PIN, A6_MODEM_TX_PIN);	// Start A6 communication channel
				smsLoop.begin();
				trace_info_P("GSM started!", NULL);
			}
		} else {
	#endif
			// Execute SMS loop every 200 ms
			if (smsLoop.shouldRun()) {
				A6Modem.doLoop();
				if (runFlag) {
					if (A6Modem.needRestart()) {
						restartCount++;
						if (restartCount >= MAX_RESTART) {
							gsmState = PSTR("Restarting ESP"); 
							trace_info_P("%s after %d restart", gsmState.c_str(), restartCount);
							delay(500);
							#ifdef FF_TRACE_USE_SERIAL
								Serial.flush();
							#endif
							ESP.restart();
						}
						gsmState = PSTR("Restarting GSM"); 
						trace_debug_P("%s", gsmState.c_str());
						A6Modem.begin(A6_MODEM_SPEED, A6_MODEM_RX_PIN, A6_MODEM_TX_PIN);	// Start A6 communication channel
					} else {
						if (A6Modem.isIdle()) {
							if (messageBuffer != "") {
								sendBufferedSMS(); 
							} else {
								if (runFlag) {
									gsmState = PSTR("Waiting for SMS");
								} else {
									gsmState = PSTR("Stopped");
								}
							}
						}
					}
				}
			}
	#ifdef ISOLATION_RELAY_PIN
		}
	#endif
}

//
//	Specific code for SMS server
//

/*!

	This routine is called each time a SMS is received

	\param[in]	pendingSmsIndex: not yet used, set to zero
	\param[in]	smsNumber: sender's phone number
	\param[in]	smsDate:message date, as provided by the network
	\param[in]	smsMessage: content of sent SMS
	
	\return	None

*/
// SMS read callback, extract commands
void readSmsCallback(int pendingSmsIndex, const char* smsNumber, const char* smsDate, const char* smsMessage) {
	if (localTraceFlag) enterRoutine(__func__);
	// Empty SMS?
	if (!smsMessage[0]) {
		return;
	}
	gsmState = PSTR("SMS read");							// Update state
	trace_debug_P("Read SMS from %s, date %s, >%s<", smsNumber, smsDate, smsMessage);
	lastReceivedNumber = String(smsNumber);					// Save received number
	lastReceivedDate = String(smsDate);						// ... date
	lastReceivedMessage = String(smsMessage);				// ... and message
	// Check for a known sender phone number 
	int endPosition = 0;
	int startPosition = 0;
	while (endPosition >= 0) {
		endPosition = allowedNumbers.indexOf(",", startPosition);
		// Compare sending number with extracted one
		if (!strcmp(smsNumber, allowedNumbers.substring(startPosition, endPosition).c_str())) {
			break;
		}
		startPosition = endPosition + 1;
	}
	if (endPosition > 0) {									// Sender is known
		// Save number
		strncpy(lastSmsNumber, smsNumber, sizeof(lastSmsNumber));
		// Compose jsonMessage
		DynamicJsonDocument jsonDoc(512);
		jsonDoc["number"] = lastReceivedNumber;				// Set received number
		jsonDoc["date"] = lastReceivedDate;					// ... date
		jsonDoc["message"] = lastReceivedMessage;			// ... and message
		String temp;
		serializeJson(jsonDoc, temp);						// Convert json to string
		#ifdef PRINT_RECEIVED_SMS_ON_SERIAL
			Serial.println(temp);							// Print message on Serial
		#endif
		// Publish received SMS on MQTT
		if (mqttSendTopic != "") {
			trace_info_P("Publishing %s to %s", temp.c_str(), mqttSendTopic.c_str());
			FF_WebServer.mqttPublishRaw(mqttSendTopic.c_str(),temp.c_str(), false);
		}
	} else {
		trace_debug_P("Bad sender %s", smsNumber);			// Sender not in allowed list
	}
}

/*!

	This routine extract one SMS from queue and send it

	Messages are in (global) messageBuffer, separated by \255 character.
	Numbers are in (global) numberBuffer, separated by \255 character.

	\param[in]	None
	\return	None

*/
void sendBufferedSMS(void) {
	if (localTraceFlag) enterRoutine(__func__);
	size_t pos = messageBuffer.indexOf('\255');
	if (pos != std::string::npos) {
		// We have other messages, extract first one and remove it 
		lastSentMessage = messageBuffer.substring(0, pos);
		messageBuffer = messageBuffer.substring(pos + 1);
	} else {
		// This is the last message, load it and clear buffer
		lastSentMessage = messageBuffer;
		messageBuffer = "";
	}
	// Do the same with numberBuffer
	pos = numberBuffer.indexOf('\255');
	if (pos != std::string::npos) {
		lastSentNumber = numberBuffer.substring(0, pos);
		numberBuffer = numberBuffer.substring(pos + 1);
	} else {
		lastSentNumber = numberBuffer;
		numberBuffer = "";
	}
	// Save current date and time
	lastSentDate = NTP.getDateStr() + " " + NTP.getTimeStr();
	gsmState="Sending SMS";
	// Trace
	trace_debug_P("Send SMS to %s: %s", lastSentNumber.c_str(), lastSentMessage.c_str());

	// Send SMS
	A6Modem.sendSMS(lastSentNumber.c_str(), lastSentMessage.c_str()); 
}

/*!

	This routine stores SMS message and number in queue

	Messages are in stored (global) messageBuffer, separated by \255 character.
	Numbers are in stored (global) numberBuffer, separated by \255 character.

	\param[in]	smsNumber: sender's phone number
	\param[in]	smsMessage: content of sent SMS
	
	\return	None

*/
// Add message and number at end of buffers, to be processed later on
void sendSMS(const char* smsNumber, const char* smsMessage) {
	if (localTraceFlag) enterRoutine(__func__);
	messageBuffer += String(smsMessage) + String('\255');
	numberBuffer += String(smsNumber) + String('\255');
}

/*!

	\brief	[Private] Debug: trace each entered routine

	By default, do nothing as very verbose. Enable it only when really needed.

	Nothing is done if routine is the same as the previously printed

	\param[in]	routine name to display
	\return	none

*/
void enterRoutine(const char* routineName) {
	trace_debug_P("Entering %s", routineName);
}
