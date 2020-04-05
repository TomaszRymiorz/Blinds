#include <Wire.h>
#include <SPI.h>
#include <FS.h>
#include <RTClib.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include "main.h"

RTC_DS1307 RTC;
ESP8266WebServer server(80);
HTTPClient HTTP;

// core version = 12;
bool offline = true;
bool keepLog = false;

const char daysOfTheWeek[7][2] = {"s", "o", "u", "e", "h", "r", "a"};
char hostName[30] = {0};

String ssid = "";
String password = "";

uint32_t startTime = 0;
uint32_t loopTime = 0;
int uprisings = 1;
int offset = 0;
bool dst = false;

String smartString = "0";
Smart *smartArray;
int smartCount = 0;

bool strContains(String text, String value);
bool hasTimeChanged();
void note(String text);
bool writeObjectToFile(String name, DynamicJsonDocument object);
String get1Smart(int index);
void connectingToWifi();
void initiatingWPS();
void activationTheLog();
void deactivationTheLog();
void requestForLogs();
void clearTheLog();
void deleteWiFiSettings();
void deleteDeviceMemory();
void receivedOfflineData();
void putOfflineData(String url, String values);
void putMultiOfflineData(String values);
void getOfflineData(bool log);


bool strContains(String text, String value) {
  return text.indexOf(value) > -1;
}

bool hasTimeChanged() {
  uint32_t currentTime = RTC.isrunning() ? RTC.now().unixtime() : millis() / 1000;
  if (abs(currentTime - loopTime) >= 1) {
    loopTime = currentTime;
    return true;
  }
  return false;
}

void note(String text) {
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

  if (keepLog) {
    File file = SPIFFS.open("/log.txt", "a");
    if (file) {
      file.println(logs);
      file.close();
    }
  }

  Serial.print("\n" + logs);
}

bool writeObjectToFile(String name, DynamicJsonDocument object) {
  name = "/" + name + ".txt";

  File file = SPIFFS.open(name, "w");
  if (file && object.size() > 0) {
    bool result = serializeJson(object, file) > 2;
    file.close();
    return result;
  }
  return false;
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


void connectingToWifi() {
  String logs = "Connecting to Wi-Fi";
  Serial.print("\n" + logs);


  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.begin(ssid.c_str(), password.c_str());

  int timeout = 0;
  while (timeout++ < 20 && WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    delay(250);
  }

  bool result = WiFi.status() == WL_CONNECTED;

  if (result) {
    logs = "Connected to " + WiFi.SSID();
    logs += " : " + WiFi.localIP().toString();
  } else {
    logs += " timed out";
  }
  note(logs);

  if (result) {
    WiFi.setAutoReconnect(true);

    startServices();
    sayHelloToTheServer();
  } else {
    delay(1000);
    initiatingWPS();
  }
}

void initiatingWPS() {
  String logs = "Initiating WPS";
  Serial.print("\n" + logs);


  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  WiFi.begin();
  WiFi.beginWPSConfig();

  int timeout = 0;
  while (timeout++ < 20 && WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    delay(250);
  }

  bool result = WiFi.status() == WL_CONNECTED;

  if (result) {
    ssid = WiFi.SSID();
    password = WiFi.psk();

    logs += " finished. ";
    logs += "Connected to " + WiFi.SSID();
  } else {
    logs += " timed out";
  }
  note(logs);

  if (result) {
    saveSettings();
    startServices();
    sayHelloToTheServer();
  } else {
    if (hasTimeChanged()) {
      automaticSettings();
    }
    if (ssid != "" && password != "") {
      connectingToWifi();
    } else {
      initiatingWPS();
    }
  }
}


void activationTheLog() {
  if (keepLog) {
    return;
  }

  File file = SPIFFS.open("/log.txt", "a");
  if (file) {
    file.println();
    file.close();
  }
  keepLog = true;

  String logs = "The log has been activated";
  server.send(200, "text/plain", logs);
  Serial.print("\n" + logs);
}

void deactivationTheLog() {
  if (!keepLog) {
    return;
  }

  if (SPIFFS.exists("/log.txt")) {
    SPIFFS.remove("/log.txt");
  }
  keepLog = false;

  String logs = "The log has been deactivated";
  server.send(200, "text/plain", logs);
  Serial.print("\n" + logs);
}

void requestForLogs() {
  File file = SPIFFS.open("/log.txt", "r");
  if (!file) {
    server.send(404, "text/plain", "No log file");
    return;
  }

  Serial.print("\nA log file was requested");

  server.setContentLength(file.size() + String("Log file\nHTTP/1.1 200 OK").length());
  server.send(200, "text/html", "Log file\n");
  while (file.available()) {
    server.sendContent(String(char(file.read())));
  }
  file.close();
  server.send(200, "text/html", "Done");
}

void clearTheLog() {
  if (SPIFFS.exists("/log.txt")) {
    File file = SPIFFS.open("/log.txt", "w");
    if (file) {
      file.println();
      file.close();
    }

    String logs = "The log file was cleared";
    server.send(200, "text/plain", logs);
    Serial.print("\n" + logs);
    return;
  }

  server.send(404, "text/plain", "Failed!");
}


void deleteWiFiSettings() {
  ssid = "";
  password = "";
  saveSettings();

  String logs = "Wi-Fi settings have been removed";
  server.send(200, "text/plain", logs);
  Serial.print("\n" + logs);
}




void receivedOfflineData() {
  if (server.hasArg("plain")) {
    server.send(200, "text/plain", "Data has received");
    readData(server.arg("plain"), true);
    return;
  }

  server.send(200, "text/plain", "Body not received");
}

void putOfflineData(String url, String values) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  String logs;

  HTTP.begin("http://" + url + "/set");
  HTTP.addHeader("Content-Type", "text/plain");
  int httpCode = HTTP.PUT(values);

  if (httpCode > 0) {
    logs = "Data transfer:\n http://" + url + "/set" + values;
  } else {
    logs = "Error sending data to " + url;
  }

  HTTP.end();

  note(logs);
}

void putMultiOfflineData(String values) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  int n = MDNS.queryService("idom", "tcp");
  if (n > 0) {
    String ip;
    String logs;
    String devices;

    for (int i = 0; i < n; ++i) {
      ip = String(MDNS.IP(i)[0]) + '.' + String(MDNS.IP(i)[1]) + '.' + String(MDNS.IP(i)[2]) + '.' + String(MDNS.IP(i)[3]);

      if (!strContains(devices, ip)) {
        devices += ip + ",";

        HTTP.begin("http://" + ip + "/set");
        HTTP.addHeader("Content-Type", "text/plain");
        int httpCode = HTTP.PUT(values);

        if (httpCode > 0) {
          logs += "\n http://" + ip + "/set" + values;
        } else {
          logs += "\n Error sending data to " + ip;
        }

        HTTP.end();
      }
    }

    note("Data transfer between devices (" + String(n) + "): " + logs + "");
  }
}

void getOfflineData(bool log) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  int n = MDNS.queryService("idom", "tcp");
  if (n > 0) {
    String ip;
    String logs = "Received data...";

    for (int i = 0; i < n; ++i) {
      ip = String(MDNS.IP(i)[0]) + '.' + String(MDNS.IP(i)[1]) + '.' + String(MDNS.IP(i)[2]) + '.' + String(MDNS.IP(i)[3]);

      HTTP.begin("http://" + ip + "/basicdata");
      HTTP.addHeader("Content-Type", "text/plain");
      int httpCode = HTTP.GET();

      if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
        String data = HTTP.getString();
        logs +=  "\n " + ip + ": " + data;
        readData(data, true);
      } else {
        logs += "\n " + ip + " - failed! " + httpCode;
      }

      HTTP.end();
    }

    if (log) {
      note(logs);
    }
  }
}
