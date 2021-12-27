#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "main.h"

#ifdef physical_clock
  RTC_DS1307 rtc;
#else
  RTC_Millis rtc;
#endif

ESP8266WebServer server(80);
WiFiClient wifiClient;
HTTPClient httpClient;

const int core_version = 20;
bool offline = true;
bool keep_log = false;

const char days_of_the_week[7][2] = {"s", "o", "u", "e", "h", "r", "a"};
char host_name[30] = {0};
String devices = "";

String ssid = "";
String password = "";

uint32_t start_time = 0;
uint32_t loop_time = 0;
int uprisings = 1;
int offset = 0;
bool dst = false;

String smart_string = "0";
Smart *smart_array;
int smart_count = 0;

String geo_location = "0";
int last_sun_check = -1;
int next_sunset = -1;
int next_sunrise = -1;
bool also_sensors = false;

int dusk_delay = 0;
int dawn_delay = 0;
int light_delay = 0;

bool strContains(String text, String value);
bool strContains(int text, String value);
bool isStringDigit(String text);
bool RTCisrunning();
bool hasTimeChanged();
void note(String text);
bool writeObjectToFile(String name, DynamicJsonDocument object);
String get1(String text, int index);
String getSmartString();
void connectingToWifi();
void initiatingWPS();
void activationTheLog();
void deactivationTheLog();
void requestForLogs();
void clearTheLog();
void getSunriseSunset(int day);
int findMDNSDevices();
void receivedOfflineData();
void putOfflineData(String url, String data);
void putMultiOfflineData(String data);
void getOfflineData();
void setupOTA();


bool strContains(String text, String value) {
  return text.indexOf(value) > -1;
}

bool strContains(int text, String value) {
  return String(text).indexOf(value) > -1;
}

bool isStringDigit(String text) {
  for (byte i = 0; i < text.length(); i++) {
    if (!isDigit(text.charAt(i))) {
      return false;
    }
  }
  return text.length() > 0;
}

bool RTCisrunning() {
  #ifdef RTC_DS1307
    return rtc.isrunning();
  #else
    return rtc.now().unixtime() > 1546304461;
  #endif
}

bool hasTimeChanged() {
  int current_time = RTCisrunning() ? rtc.now().unixtime() : millis() / 1000;
  if (abs(current_time - (int)loop_time) >= 1) {
    loop_time = current_time;
    return true;
  }
  return false;
}

void note(String text) {

  String logs = strContains(text, "iDom") ? "\n[" : "[";
  if (RTCisrunning()) {
    DateTime now = rtc.now();
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

  Serial.print("\n" + logs);

  if (keep_log) {
    File file = LittleFS.open("/log.txt", "a");
    if (file) {
      file.println(logs);
      file.close();
    }
  }
}

bool writeObjectToFile(String name, DynamicJsonDocument object) {
  name = "/" + name + ".txt";
  bool result = false;

  File file = LittleFS.open(name, "w");
  if (file && object.size() > 0) {
    result = serializeJson(object, file) > 2;
    file.close();
  }

  return result;
}

String get1(String text, int index) {
  int found = 0;
  int str_index[] = {0, -1};
  int max_index = text.length() - 1;

  for (int i = 0; i <= max_index && found <= index; i++) {
    if (text.charAt(i) == ',' || i == max_index) {
      found++;
      str_index[0] = str_index[1] + 1;
      str_index[1] = (i == max_index) ? i + 1 : i;
    }
  }
  return found > index ? text.substring(str_index[0], str_index[1]) : "";
}

String getSmartString() {
  String result = smart_string;
  result.replace("&", "%26");
  return result;
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
    logs += " : " + WiFi.localIP().toString();
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
  if (keep_log) {
    server.send(200, "text/html", "Done");
    return;
  }

  File file = LittleFS.open("/log.txt", "a");
  if (file) {
    file.println();
    file.close();
  }
  keep_log = true;

  server.send(200, "text/plain", "The log has been activated");
}

void deactivationTheLog() {
  if (!keep_log) {
    server.send(200, "text/html", "Done");
    return;
  }

  if (LittleFS.exists("/log.txt")) {
    LittleFS.remove("/log.txt");
  }
  keep_log = false;

  server.send(200, "text/plain", "The log has been deactivated");
}

void requestForLogs() {
  File file = LittleFS.open("/log.txt", "r");
  if (!file) {
    server.send(404, "text/plain", "No log file");
    return;
  }

  server.setContentLength(file.size() + String("Log file\nHTTP/1.1 200 OK").length());
  server.send(200, "text/html", "Log file\n");
  while (file.available()) {
    server.sendContent(String(char(file.read())));
  }
  file.close();

  server.send(200, "text/html", "Done");
}

void clearTheLog() {
  File file = LittleFS.open("/log.txt", "w");
  if (!file) {
    server.send(404, "text/plain", "Failed!");
    return;
  }

  file.println();
  file.close();

  server.send(200, "text/plain", "The log file was cleared");
}


void getSunriseSunset(int day) {
  if (WiFi.status() != WL_CONNECTED || geo_location.length() < 2) {
    return;
  }

  String location = "lat=" + geo_location;
  location.replace("x", "&lng=");

  httpClient.begin(wifiClient, "http://api.sunrise-sunset.org/json?" + location);
  httpClient.addHeader("Content-Type", "text/plain");

  if (httpClient.GET() == HTTP_CODE_OK) {
    DynamicJsonDocument json_object(1024);
    deserializeJson(json_object, httpClient.getString());
    JsonObject object = json_object["results"];

    if (object.containsKey("sunrise") && object.containsKey("sunset")) {
      String data = object["sunrise"].as<String>();
      next_sunrise = ((data.substring(0, data.indexOf(":")).toInt() + (strContains(data, "PM") ? 12 : 0) + (offset > 0 ? offset / 3600 : 0) + (dst ? 1 : 0)) * 60) + data.substring(data.indexOf(":") + 1, data.indexOf(":") + 3).toInt();

      data = object["sunset"].as<String>();
      next_sunset = ((data.substring(0, data.indexOf(":")).toInt() + (strContains(data, "PM") ? 12 : 0) + (offset > 0 ? offset / 3600 : 0) + (dst ? 1 : 0)) * 60) + data.substring(data.indexOf(":") + 1, data.indexOf(":") + 3).toInt();

      last_sun_check = day;
      note("Sunrise: " + String(next_sunrise) + " / Sunset: " + String(next_sunset));
    }
  }

  httpClient.end();
}

int findMDNSDevices() {
  int n = MDNS.queryService("idom", "tcp");
  String ip;

  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      ip = String(MDNS.IP(i)[0]) + '.' + String(MDNS.IP(i)[1]) + '.' + String(MDNS.IP(i)[2]) + '.' + String(MDNS.IP(i)[3]);
      if (!strContains(devices, ip)) {
        devices += (devices.length() > 0 ? "," : "" ) + ip;
      }
    }
  }

  if (devices.length() > 0) {
    int count = 1;
    for (byte b: devices) {
      if (b == ',') {
        count++;
      }
    }
    return count;
  } else {
    return 0;
  }
}

void receivedOfflineData() {
  if (server.hasArg("plain")) {
    server.send(200, "text/plain", "Data has received");
    readData(server.arg("plain"), true);
    return;
  }

  server.send(200, "text/plain", "Body not received");
}

void putOfflineData(String url, String data) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  String logs;

  httpClient.begin(wifiClient, "http://" + url + "/set");
  httpClient.addHeader("Content-Type", "text/plain");
  int http_code = httpClient.PUT(data);

  if (http_code == HTTP_CODE_OK) {
    logs = url + ": " + data;
  } else {
    logs = url + " - error "  + http_code;
  }

  httpClient.end();

  note("Data transfer to:\n " + logs);
}

void putMultiOfflineData(String data) {
  if (WiFi.status() != WL_CONNECTED || data.length() < 2) {
    return;
  }

  int count = findMDNSDevices();
  if (count == 0) {
    return;
  }

  String ip;
  int http_code;
  String logs = "";

  for (int i = 0; i < count; i++) {
    ip = get1(devices, i);

    if (wifiClient.available() == 0) {
      wifiClient.stop();
    }

    httpClient.begin(wifiClient, "http://" + ip + "/set");
    httpClient.addHeader("Content-Type", "text/plain");
    http_code = httpClient.PUT(data);

    if (http_code == HTTP_CODE_OK) {
      logs += "\n " + ip;
    } else {
      logs += "\n " + ip + " - error "  + http_code;
    }

    httpClient.end();
  }

  note(data + " transfer to " + String(count) + ":" + logs);
}

void getOfflineData() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  int count = findMDNSDevices();
  if (count == 0) {
    return;
  }

  String ip;
  int http_code;
  String data;
  String logs = "";

  for (int i = 0; i < count; i++) {
    ip = get1(devices, i);

    if (wifiClient.available() == 0) {
      wifiClient.stop();
    }

    httpClient.begin(wifiClient, "http://" + ip + "/basicdata");
    httpClient.addHeader("Content-Type", "text/plain");
    http_code = httpClient.POST("");

    if (http_code == HTTP_CODE_OK) {
      if (httpClient.getSize() > 15) {
        data = httpClient.getString();
        logs +=  "\n " + ip + ": " + data;
        readData(data, true);
      }
    } else {
      logs += "\n " + ip + ": error " + http_code;
    }

    httpClient.end();
  }

  note("Received data..." + logs);
}

void setupOTA() {
  ArduinoOTA.setHostname(host_name);

  ArduinoOTA.onEnd([]() {
    note("Software update over Wi-Fi");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String log = "OTA ";
    if (error == OTA_AUTH_ERROR) {
      log += "Auth";
    } else if (error == OTA_BEGIN_ERROR) {
      log += "Begin";
    } else if (error == OTA_CONNECT_ERROR) {
      log += "Connect";
    } else if (error == OTA_RECEIVE_ERROR) {
      log += "Receive";
    } else if (error == OTA_END_ERROR) {
      log += "End";
    }
    note(log + String(" failed!"));
  });

  ArduinoOTA.begin();
}
