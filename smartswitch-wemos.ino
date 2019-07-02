/*
  Version 1.0 - May 29th 2019
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <WebSocketsClient.h> //  https://github.com/kakopappa/sinric/wiki/How-to-add-dependency-libraries
#include <ArduinoJson.h> // https://github.com/kakopappa/sinric/wiki/How-to-add-dependency-libraries (use the correct version)
#include <StreamString.h>
#include "FS.h"

// default values
const char *DEFAULT_APSSID = "smartswitch-WEMOS";
const char *DEFAULT_APPASS = "swt12345678";
const char *DEFAULT_SSID = "mywifinetwork";
const char *DEFAULT_PASS = "mywifipass";
const char *DEFAULT_APIURL = "iot.sinric.com";
const char *DEFAULT_APIPORT = "80";
const char *DEFAULT_DEVICEID = "mydeviceID";
const char *DEFAULT_APIKEY = "myAPIKey";
const char *config_file = "/smartswitch.conf"; // file that holds WIFI and IOT configuration

char ssid[128] = "", passphrase[128] = ""; // WIFI connection info
char ap_ssid[128] = "", ap_passphrase[128] = ""; // WIFI AP connection info
char apiurl[128] = "", apiport[5] = "", apikey[128] = "", deviceId[128] = ""; // IOT settings


const int buttonPin = D7; // pin number for the function button
const int relayPin = D3;  // pin for the relay shield
bool configMode = false; // if in config mode (rocker switch on), act as a WiFi AP, else connect to the net

ESP8266WebServer server(80); // HTTP server will listen at port 80
WebSocketsClient webSocket;

// Web forms sent to the client
String header =   "<h2>smart switch</h2>"
                  "<span>a simple smart switch for your WEMOS D1, by docca</span>";

String setup_success =
  "<center>" + header +
  "<p>Configuration saved successfully!</p>"
  "</center>";

String setup_failed =
  "<center>" + header +
  "<h1>wifi setup</h1>"
  "<p>SSID and passphrase both need to be longer than 8 characters! Configuration has NOT been changed.</p>"
  "</center>";


#define HEARTBEAT_INTERVAL 300000 // 5 Minutes 

uint64_t heartbeatTimestamp = 0;
bool isConnected = false;
const long interval = 2000;

// function prototypes
String getSetupForm(void);
String getFooter(void);
void setupWifi(void);
void readConfig(void);
void saveConfig(void);

// deviceId is the ID assgined to your smart-home-device in sinric.com dashboard. Copy it from dashboard and paste it here

void turnOn(String devId) {
  if (devId == deviceId) // Device ID of first device
  {
    Serial.print("Turn on device id: ");
    Serial.println(deviceId);
    digitalWrite(relayPin, HIGH); // turn on relay with voltage HIGH

  }
  else {
    Serial.print("Turn on for unknown device id: ");
    Serial.println(devId);
  }
}

void turnOff(String devId) {
  if (devId == deviceId) // Device ID of first device
  {
    Serial.print("Turn off Device ID: ");
    Serial.println(devId);
    digitalWrite(relayPin, LOW);  // turn off relay with voltage LOW

  }
  else {
    Serial.print("Turn off for unknown device id: ");
    Serial.println(devId);
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      isConnected = false;
      Serial.printf("[WSc] Webservice disconnected from IOT provider!\r\n");
      break;
    case WStype_CONNECTED: {
        isConnected = true;
        Serial.printf("\n[WSc] Service connected at url: %s\r\n", payload);
        Serial.printf("\nWaiting for commands from IOT provider...\r\n");
      }
      break;
    case WStype_TEXT: {
        Serial.printf("[WSc] get text: %s\n", payload);

        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject((char*)payload);
        String deviceId = json ["deviceId"];
        String action = json ["action"];

        if (action == "action.devices.commands.OnOff") { // Switch or Light
          String value = json ["value"]["on"];
          if (value == "true") {
            turnOn(deviceId);
          } else {
            turnOff(deviceId);
          }
        }
        else if (action == "test") {
          Serial.println("[WSc] received test command from IOT provider");
          digitalWrite(relayPin, HIGH); // turn on relay with voltage HIGH
          delay(interval);              // pause
          digitalWrite(relayPin, LOW);  // turn off relay with voltage LOW
          delay(interval); // pause
        }
      }
      break;
    case WStype_BIN:
      Serial.printf("[WSc] get binary length: %u\n", length);
      break;
  }
}

void setup() {
  // set up the pins
  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  delay(10);
  Serial.println("\n");
  Serial.println("=== Smart Switch by docca starting up ===");

  Serial.println();

  // mount the filesystem
  bool result = SPIFFS.begin();
  delay(200);
  Serial.println("SPIFFS opened: " + result);
  readConfig();
  
  digitalRead(buttonPin)==LOW ? configMode=true : configMode=false; 
  delay(20);
  setupWifi();

  if (configMode) {
    Serial.println("*** CONFIG MODE ***");

    // Set up the endpoints for HTTP server,  Endpoints can be written as inline functions:    
    server.on("/", [&server]() {
      Serial.println("Showing setup form");
      String form_setup = getSetupForm();
      server.send(200, "text/html", form_setup);
    });

    server.on("/dosetup", [&server]() {
        Serial.println("Processing setup form");
        // compare new data with old data
      
        String id = server.arg("ssid");
        String pass = server.arg("passphrase");
        String apid = server.arg("ap_ssid");
        String appass = server.arg("ap_passphrase");
      
        String url = server.arg("apiurl");
        String port = server.arg("apiport");
        String key = server.arg("apikey");
        String devid = server.arg("deviceid");
      
        if ((id.length() >= 8) && (pass.length() >= 8) && (url.length() >= 8) && (port.length() >= 2) && (key.length() >= 8) && (devid.length() >= 8) && (apid.length() >= 8) && (appass.length() >= 8))
        {
          server.send(200, "text/html", setup_success + getFooter());
          strcpy(ssid, id.c_str());
          strcpy(passphrase, pass.c_str());
          strcpy(ap_ssid, apid.c_str());
          strcpy(ap_passphrase, appass.c_str());
      
          strcpy(apiurl, url.c_str());
          strcpy(apiport, port.c_str());
          strcpy(apikey, key.c_str());
          strcpy(deviceId, devid.c_str());
          Serial.printf("New SSID = %s, new passphrase = %s\n", ssid, passphrase);
          Serial.printf("New AP SSID = %s, new AP passphrase = %s\n", ap_ssid, ap_passphrase);
          Serial.printf("New API URL = %s, new API Port = %s, new API Key = %s, new Device ID = %s\n", url.c_str(), port.c_str(), key.c_str(), devid.c_str());
          server.send(200, "text/html", setup_success);
          saveConfig();
          Serial.println("config saved");
        }
        else
        {
          server.send(200, "text/html", setup_failed + getFooter());
          Serial.println("setup error, try again");
        }
    });
    server.begin();
    Serial.println("Webserver ready!");
  } else {
    Serial.println("config mode is OFF");

    // server address, port and URL
    webSocket.begin(apiurl, String(apiport).toInt(), "/");
    // event handler
    webSocket.onEvent(webSocketEvent);
    webSocket.setAuthorization("apikey", apikey);

    // try again every 5000ms if connection has failed
    webSocket.setReconnectInterval(5000);   // If you see 'class WebSocketsClient' has no member named 'setReconnectInterval' error update arduinoWebSockets
    Serial.printf("API Server: %s port %s\nAPI Key: %s\nDevice ID: %s\n", apiurl, apiport, apikey, deviceId);
    Serial.println("Websocket client ready!");

  }
}

void loop() {
  if (!configMode) {
    webSocket.loop();
    if (isConnected) {
      uint64_t now = millis();

      // Send heartbeat in order to avoid disconnections during ISP resetting IPs over night. Thanks @MacSass
      if ((now - heartbeatTimestamp) > HEARTBEAT_INTERVAL) {
        heartbeatTimestamp = now;
        webSocket.sendTXT("H");
      }
    }
  } else {
    server.handleClient();
  }
}


void readConfig()
{
  memset(&ssid, 0, sizeof(ssid));
  memset(&passphrase, 0, sizeof(passphrase));
  memset(&ap_ssid, 0, sizeof(ap_ssid));
  memset(&ap_passphrase, 0, sizeof(ap_passphrase));
  memset(&apiurl, 0, sizeof(apiurl));
  memset(&apiport, 0, sizeof(apiport));
  memset(&apikey, 0, sizeof(apikey));
  memset(&deviceId, 0, sizeof(deviceId));

  Serial.println("reading config file");
  File f = SPIFFS.open(config_file, "r");

  if (!f)
  {
    f.close();
    Serial.println("No configuration found, using default values.");
    memset(&ssid, 0, sizeof(ssid));
    memset(&passphrase, 0, sizeof(passphrase));
    memset(&ap_ssid, 0, sizeof(ap_ssid));
    memset(&ap_passphrase, 0, sizeof(ap_passphrase));

    strcpy(ssid, DEFAULT_SSID);
    strcpy(passphrase, DEFAULT_PASS);
    strcpy(ap_ssid, DEFAULT_APSSID);
    strcpy(ap_passphrase, DEFAULT_APPASS);

    strcpy(apiurl, DEFAULT_APIURL);
    strcpy(apiport, DEFAULT_APIPORT);
    strcpy(apikey, DEFAULT_APIKEY);
    strcpy(deviceId, DEFAULT_DEVICEID);

    // create a new file
    saveConfig();
    return;
  }

  // file read okay
  while (f.available()) {
    //Lets read line by line from the file
    String line = f.readStringUntil('\n');

    int pos = line.indexOf('=');
    if (line.substring(0, pos) == "ssid")
    {
      strcpy(ssid, line.substring(pos + 1).c_str());
      Serial.printf("got ssid = %s\r\n", ssid);
    }
    if (line.substring(0, pos) == "password")
    {
      strcpy(passphrase, line.substring(pos + 1).c_str());
      Serial.printf("got password = %s\r\n", passphrase);
    }
    if (line.substring(0, pos) == "ap_ssid")
    {
      strcpy(ap_ssid, line.substring(pos + 1).c_str());
      Serial.printf("got ap_ssid = %s\r\n", ap_ssid);
    }
    if (line.substring(0, pos) == "ap_password")
    {
      strcpy(ap_passphrase, line.substring(pos + 1).c_str());
      Serial.printf("got ap_password = %s\r\n", ap_passphrase);
    }
    if (line.substring(0, pos) == "apiurl")
    {
      strcpy(apiurl, line.substring(pos + 1).c_str());
      Serial.printf("got apiurl = %s\r\n", apiurl);
    }
    if (line.substring(0, pos) == "apiport")
    {
      strcpy(apiport, line.substring(pos + 1).c_str());
      Serial.printf("got apiport = %s\r\n", apiport);
    }
    if (line.substring(0, pos) == "apikey")
    {
      strcpy(apikey, line.substring(pos + 1).c_str());
      Serial.printf("got apikey = %s\r\n", apikey);
    }
    if (line.substring(0, pos) == "deviceid")
    {
      strcpy(deviceId, line.substring(pos + 1).c_str());
      Serial.printf("got deviceId = %s\r\n", deviceId);
    }
  }
  f.close();

}

void saveConfig()
{
  Serial.println("Saving configuration...");

  // open the file in write mode
  File f = SPIFFS.open(config_file, "w");
  if (!f) {
    Serial.printf("file creation failed: %s\r\n", config_file);
  }

  // now write lines in key/value style with  end-of-line characters
  f.printf("ssid=%s\n", ssid);
  f.printf("password=%s\n", passphrase);
  f.printf("ap_ssid=%s\n", ap_ssid);
  f.printf("ap_password=%s\n", ap_passphrase);
  f.printf("apiurl=%s\n", apiurl);
  f.printf("apiport=%s\n", apiport);
  f.printf("apikey=%s\n", apikey);
  f.printf("deviceid=%s\n", deviceId);

  f.close();
  Serial.println("Saved configuration...");

}

void setupWifi()
{

  // load config from file
  readConfig();
  delay(500);

  wifi_set_phy_mode(PHY_MODE_11G);
  if (configMode) {
    Serial.print("Setting soft-AP ... ");
    Serial.printf("using %s and %s\r\n", ap_ssid, ap_passphrase);
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(ap_ssid, ap_passphrase))
    {
      Serial.printf("Success. AP SSID: %s, AP PASS: %s\r\n", ap_ssid, ap_passphrase);
      Serial.print("Web interface available at ");
      Serial.println(WiFi.softAPIP());
      digitalWrite(LED_BUILTIN, HIGH);
      delay(500);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);
      WiFi.printDiag(Serial);
    }
    else
    {
      Serial.printf("WiFi Failure.\n");
    }
  } else {
    ESP8266WiFiMulti WiFiMulti;
    WiFi.mode(WIFI_STA);
    WiFiMulti.addAP(ssid, passphrase);
    Serial.println();
    Serial.print("Connecting to Wifi: ");
    Serial.println(ssid);
    WiFi.printDiag(Serial);
    // Waiting for Wifi connect
    while (WiFiMulti.run() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }

    if (WiFiMulti.run() == WL_CONNECTED) {
      Serial.println("");
      Serial.print("WiFi connected. ");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}


String getFooter(void)
{
  String footer =
    "<p><center><a href='http://github.com/doccaz/smartswitch'>github</a>&nbsp;|&nbsp;<a href='/'>setup page</a></center></p>";
  return footer;
}

String getSetupForm(void)
{
  String formcontents =
    "<center>" + header +
    "<p><form action='dosetup'>"
    "<p>SSID <input type='text' name='ssid' size=32 autofocus value='" + ssid  + "'></p>"
    "<p>Passphrase <input type='text' name='passphrase'  size=32 value='" + passphrase + "'></p>"

    "<p>AP SSID <input type='text' name='ap_ssid' size=32 autofocus value='" + ap_ssid  + "'></p>"
    "<p>AP Passphrase <input type='text' name='ap_passphrase'  size=32 value='" + ap_passphrase + "'></p>"

    "<p>API URL<input type='text' name='apiurl' size=128 value='" + apiurl  + "'></p>"
    "<p>API Port<input type='text' name='apiport' size=5 value='" + apiport  + "'></p>"
    "<p>API Key<input type='text' name='apikey' size=128 value='" + apikey  + "'></p>"
    "<p>Device ID<input type='text' name='deviceid' size=128 value='" + deviceId  + "'></p>"

    "<p><input type='submit' value='Submit'></p></center>"
    "</form>" + getFooter();

  return formcontents;
}
