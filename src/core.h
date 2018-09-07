#include <Wire.h>
#include <SPI.h>
#include <SdFat.h>
#include <RTClib.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "main.h"

SdFat SD;
RTC_DS1307 RTC;
ESP8266WebServer server(80);
HTTPClient HTTP;

const char daysOfTheWeek[7][12] = {"s", "o", "u", "e", "h", "r", "a"};

const String baseURL = "http://";
const String database = "idom";

bool reconnect = false;
uint32_t loopTime = 0;
uint32_t start = 0;
int uprisings = 1;

bool offline = true;
String twin = "0";
int offset = 0;

String ssid;
String password;

Smart *smartArray;
String smartString = "0";
int smartCount = 0;

bool twilight = false;

bool strContains(String text, String value);
bool timeHasChanged();
void serialPrint(String text);
void readOffset();
void readSmart();
String readFromSD(String file);
bool readWiFiConfiguration();
void writeOffset();
void writeSmart();
void writeWiFiConfiguration();
void writeOnSD(String file, String value1, String value2, String text);
bool connectingToWifi();
bool initiatingWPS();
String get1Smart(String smartString, int index);
void getOnlineData();
void putDataOnServer(String values);
void postToTwin(String request);


bool strContains(String text, String value) {
  return text.indexOf(value) != -1;
}

bool timeHasChanged() {
  if (!RTC.isrunning()) {
    delay(1000);
    return true;
  }

  DateTime now = RTC.now();
  bool changed = loopTime != now.unixtime();
  loopTime = now.unixtime();
  return changed;
}

void serialPrint(String text) {
  Serial.print("\n");

  if (RTC.isrunning()) {
    DateTime now = RTC.now();
    Serial.print('[');
    Serial.print(now.day(), DEC);
    Serial.print('.');
    Serial.print(now.month(), DEC);
    Serial.print('.');
    Serial.print(now.year(), DEC);
    Serial.print(" ");
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.print("] ");
  }

  Serial.print(text);
}


void readOffset() {
  String s = readFromSD("offset");
  if (s != "-1") {
    offset = s.toInt();
    Serial.printf("\n Offset: %i", offset);
  }
}

void readSmart() {
  String s = readFromSD("smart");
  if (s != "-1") {
    smartString = s;
    Serial.printf("\n Smart: %s", smartString.c_str());
    setSmart();
  }
}

String readFromSD(String file) {
  SdFile rdfile((file + ".txt").c_str(), O_READ);

  if (rdfile.isOpen()) {
    int lineNo = 0;
    char line[25];
    int n;
    String s;

    while ((n = rdfile.fgets(line, sizeof(line))) > 0) {
      if (line[n - 1] == '\n') {
        switch (lineNo++) {
          case 0:
            s += line;
            s.trim();
            break;
        }
      }
    }
    rdfile.close();
    return s;
  } else {
    return "-1";
  }
}

bool readWiFiConfiguration() {
  Serial.print("\nOpening the Wi-Fi settings file");
  SdFile rdfile("wifi.txt", O_READ);

  if (rdfile.isOpen()) {
    int lineNo = 0;
    char line[25];
    int n;
    String s;
    String p;

    while ((n = rdfile.fgets(line, sizeof(line))) > 0) {
      if (line[n - 1] == '\n') {
        switch (lineNo++) {
          case 0:
            s = line;
            s.trim();
            ssid = s;
            Serial.print("\n SSID: " + s);
            break;

          case 1:
            p = line;
            p.trim();
            password = p;
            Serial.print("\n PASSWORD: " + p);
            break;
        }
      }
    }

    rdfile.close();
    reconnect = true;
    return true;
  } else {
    Serial.print(" failed");
    return false;
  }
}


void writeOffset() {
  writeOnSD("offset", String(offset), "", "// Przesunięcie strefy czasowej z uwzględnieniem czasu letniego wyrażone w liczbie sekund. UTC+01:00 = 3600");
}

void writeSmart() {
  writeOnSD("smart", smartString, "", "// Automatyczne ustawienia urządzenia.");
}

void writeWiFiConfiguration() {
  ssid = WiFi.SSID();
  ssid.trim();
  password = WiFi.psk();
  password.trim();

  writeOnSD("wifi", ssid, password, "// Dane dostępowe do lokalnej sieci Wi-Fi. Pierwsza linia zawiera SSID sieci Wi-Fi, druga hasło dostępu do sieci.");
}

void writeOnSD(String file, String value1, String value2, String comment) {
  Serial.printf("\nSaving the %s (%s) on the SD card", file.c_str(), value1.c_str());

  SPI.begin();
  if (SD.exists((file + ".txt").c_str())) {
    SD.remove((file + ".txt").c_str());
  }

  File wrfile = SD.open((file + ".txt").c_str(), FILE_WRITE);
  if (wrfile) {
    wrfile.println(value1);
    if (value2.length() > 0 ) {
      wrfile.println(value2);
    }
    wrfile.println(comment);
    wrfile.close();
  } else {
    Serial.print(" failed");
  }
  SPI.end();
}


bool connectingToWifi() {
  serialPrint("Connecting to Wi-Fi");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());
  int timeout = 0;
  while (timeout++ < 20 && WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" finished");
    Serial.printf("\n Connected to %s", WiFi.SSID().c_str());
    Serial.printf("\n IP address: %s", WiFi.localIP().toString().c_str());

    startRestServer();
    reconnect = true;
    return true;
  } else {
    Serial.print(" timed out");
    return false;
  }
}

bool initiatingWPS() {
  serialPrint("Initiating WPS");

  WiFi.beginWPSConfig();
  int timeout = 0;
  while (timeout++ < 20 && WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" finished");
    Serial.printf("\n Connected to %s", WiFi.SSID().c_str());
    Serial.printf("\n IP address: %s\n", WiFi.localIP().toString().c_str());

    writeWiFiConfiguration();
    startRestServer();
    reconnect = true;
    return true;
  } else {
    Serial.print(" time out");
    return false;
  }
}


String get1Smart(String smartString, int index) {
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
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  int status = 0;

  while (status == 0) {
    HTTP.begin(baseURL + "/detail/" + device + "/?db=" + database + "&id=" + WiFi.macAddress() + "&web=" + WiFi.SSID());
    int httpCode = HTTP.GET();

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        status = readData(HTTP.getString());
      }
    } else {
      serialPrint("HTTP GET... failed, error: " + HTTP.errorToString(httpCode));
    }

    HTTP.end();
  }
}

void putDataOnServer(String values) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTP.begin(baseURL + "/detail/" + device + "/?db=" + database + "&id=" + WiFi.macAddress() + "&web=" + WiFi.SSID() + "&" + values);
  HTTP.addHeader("Accept", "application/json, text/plain, */*");
  HTTP.addHeader("Content-Type", "application/json;charset=utf-8");
  HTTP.PUT("");
  // HTTP.sendRequest("PUT", "");
  HTTP.end();
}

void postToTwin(String values) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (twin.length() < 2) {
    return;
  }

  Serial.print("\nSending data to a twin");

  HTTP.begin("http://" + twin + "/set");
  HTTP.addHeader("Accept", "application/json, text/plain, */*");
  HTTP.addHeader("Content-Type", "application/json;charset=utf-8");
  HTTP.PUT(values);
  HTTP.end();
}
