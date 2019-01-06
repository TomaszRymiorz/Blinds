#include <Wire.h>
#include <SPI.h>
#include <FS.h>
#include <RTClib.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "main.h"

RTC_DS1307 RTC;
ESP8266WebServer server(80);
HTTPClient HTTP;

const int version = 5;
const bool offline = true;
const String baseURL = "";

const char daysOfTheWeek[7][12] = {"s", "o", "u", "e", "h", "r", "a"};

String ssid;
String password;

uint32_t start = 0;
int uprisings = 1;
bool reconnect = false;
uint32_t loopTime = 0;
uint32_t updateTime = 0;

String twin;
int offset = 0;

String smartString = "0";
Smart *smartArray;
int smartCount = 0;

bool twilight = false;
bool sendingError = false;
bool blockOnlineData = false;

bool strContains(String text, String value);
bool timeHasChanged();
void writeLog(String text);
bool writeObjectToFile(String name, JsonObject& jsonObject);
bool connectingToWifi();
bool initiatingWPS();
void receivedTheData();
void requestForLogs();
void clearLogs();
void deleteWiFiSettings();
String get1Smart(int index);
void getOnlineData();
void putDataOnline(String variant, String values);
void postDataToTheTwin(String request);


bool strContains(String text, String value) {
  return text.indexOf(value) != -1;
}

bool timeHasChanged() {
  uint32_t currentTime = RTC.isrunning() ? RTC.now().unixtime() : millis() / 1000;
  if (abs(currentTime - loopTime) >= 1) {
    loopTime = currentTime;
    return true;
  }
  return false;
}


void writeLog(String text) {
  if (text == "") {
    return;
  }

  String logs = strContains(text, "iDom") ? "\n[" : "[";
  if (RTC.isrunning()) {
    DateTime now = RTC.now();
    logs += now.day();
    logs += ".";
    logs += now.month();
    logs += ".";
    logs += String(now.year()).substring(2, 4);
    logs += " ";
    logs += now.hour();
    logs += ":";
    logs += now.minute();
    logs += ":";
    logs += now.second();
  } else {
    logs += millis() / 1000;
  }
  logs += "] " + text;

  File file = SPIFFS.open("/log.txt", "a");
  if (file) {
    file.println(logs);
    file.close();
  }
  Serial.print("\n" + logs);
}

bool writeObjectToFile(String name, JsonObject& jsonObject) {
  name = "/" + name + ".txt";

  File file = SPIFFS.open(name, "w");
  if (file) {
    jsonObject.printTo(file);
    file.close();
    return true;
  }
  return false;
}


bool connectingToWifi() {
  String logs = "Connecting to Wi-Fi";
  Serial.print("\n" + logs);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.begin(ssid.c_str(), password.c_str());
  int timeout = 0;
  while (timeout++ < 20 && WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  bool result = WiFi.status() == WL_CONNECTED;

  if (result) {
    logs += " finished";
    logs += "\n Connected to " + WiFi.SSID();
    logs += "\n IP address: " + WiFi.localIP().toString();
  } else {
    logs += " timed out";
  }

  writeLog(logs);

  if (result) {
    startRestServer();
    sayHelloToTheServer();
    reconnect = true;
  }
  return result;
}

bool initiatingWPS() {
  String logs = "Initiating WPS";
  Serial.print("\n" + logs);

  WiFi.beginWPSConfig();
  int timeout = 0;
  while (timeout++ < 20 && WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  bool result = WiFi.status() == WL_CONNECTED;

  if (result) {
    ssid = WiFi.SSID();
    password = WiFi.psk();
    logs += " finished";
    logs += "\n Connected to " + WiFi.SSID();
    logs += "\n IP address: " + WiFi.localIP().toString();

    saveTheSettings();
    startRestServer();
    sayHelloToTheServer();
    reconnect = true;
  } else {
    logs += " time out";
  }

  writeLog(logs);
  return result;
}


void receivedTheData() {
  if (server.hasArg("plain")) {
    server.send(200, "text/plain", "Data has received");
    readData(server.arg("plain"), true);
    return;
  }

  server.send(200, "text/plain", "Body not received");
}

void requestForLogs() {
  File file = SPIFFS.open("/log.txt", "r");
  if (!file) {
    server.send(404, "text/plain", "There are no log file");
    return;
  }
  Serial.print("\nA log file was requested");

  server.setContentLength(file.size() + String("Log file\n").length());
  server.send (200, "text/html", "Log file\n");
  while (file.available()) {
    server.sendContent(String(char(file.read())));
  }
  file.close();
}

void clearLogs() {
  if (SPIFFS.exists("/log.txt")) {
    SPIFFS.remove("/log.txt");
    server.send(200, "text/plain", "Done");
    Serial.print("\nThe log file was cleared");
  } else {
    server.send(404, "text/plain", "Failed!");
  }
}

void deleteWiFiSettings() {
  ssid = "";
  password = "";
  writeLog("Wi-Fi settings have been removed");
  saveTheSettings();
  server.send(200, "text/plain", "Done");
}


String get1Smart(int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = smartString.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (smartString.charAt(i) == ',' || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? smartString.substring(strIndex[0], strIndex[1]) : "";
}


void getOnlineData() {
  if (WiFi.status() != WL_CONNECTED || offline || blockOnlineData) {
    return;
  }

  if (sendingError) {
    sayHelloToTheServer();
    // return;
  }

  blockOnlineData = true;

  HTTP.begin(baseURL + "/detail/" + device + "/?id=" + WiFi.macAddress() + "&up=" + updateTime);
  int httpCode = HTTP.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      readData(HTTP.getString(), false);
      blockOnlineData = false;
    }
  } else {
    Serial.print(".");
    blockOnlineData = false;
  }

  HTTP.end();
}

void putDataOnline(String variant, String values) {
  if (offline) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    sendingError = true;
    return;
  }

  HTTP.begin(baseURL + "/" + variant + "/" + device + "/?id=" + WiFi.macAddress() + "&" + values);
  int httpCode = HTTP.PUT("");

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      writeLog("Data sent to the server:\n {" + values + "}");
      readData(HTTP.getString(), false);
      sendingError = false;
    }
  } else {
    if (!sendingError) {
      writeLog("Failure to send data to the server:\n {" + values + "}");
    }
    sendingError = true;
  }

  HTTP.end();
}

void postDataToTheTwin(String values) {
  if (WiFi.status() != WL_CONNECTED || !offline || twin.length() < 2) {
    return;
  }

  writeLog("Sending data to a twin:\n {" + values + "}");

  HTTP.begin("http://" + twin + "/set");
  HTTP.PUT(values);
  HTTP.end();
}
