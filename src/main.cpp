#include "core.h"

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  SPIFFS.begin();
  Wire.begin();

  note("iDom Blinds " + String(bipolar ? "st" : "nx"));
  Serial.print("\nRoller blind ID: " + WiFi.macAddress());
  offline = !SPIFFS.exists("/online.txt");
  Serial.printf("\nThe roller blind is set to %s mode", offline ? "OFFLINE" : "ONLINE");

  sprintf(hostName, "blinds_%s", String(WiFi.macAddress()).c_str());
  WiFi.hostname(hostName);

  if (!readSettings(0)) {
    readSettings(1);
  }
  resume();

  RTC.begin();
  if (RTC.isrunning()) {
    startTime = RTC.now().unixtime() - offset;
  }
  Serial.printf("\nRTC initialization %s", startTime != 0 ? "completed" : "failed!");

  int newLight = analogRead(light_sensor_pin);
  if (newLight > boundary) {
    light = newLight;
  }

  setStepperPins(true);

  if (ssid != "" && password != "") {
    connectingToWifi();
  } else {
    initiatingWPS();
  }
}

void setStepperPins(bool setMode) {
  if (bipolar) {
    if (setMode) {
      for (int i = 0; i < 3; i++) {
        pinMode(bipolar_step_pin[i], OUTPUT);
      }
      pinMode(bipolar_direction_pin, OUTPUT);
      pinMode(bipolar_enable_pin, OUTPUT);
    }
    step = 30;
  } else {
    for (int i = 0; i < 4; i++) {
      if (setMode) {
        pinMode(unipolar_pin[i], OUTPUT);
      }
      digitalWrite(unipolar_pin[i], LOW);
    }
  }
}

String toPercentages(int value, int steps) {
  return String(value > 0 && steps > 0 ? value * 100 / steps : 0);
}

int toSteps(int value, int steps) {
  return value > 0 && steps > 0 ? value * steps / 100 : 0;
}


bool readSettings(bool backup) {
  File file = SPIFFS.open(backup ? "/backup.txt" : "/settings.txt", "r");
  if (!file) {
    note("The " + String(backup ? "backup" : "settings") + " file cannot be read.");
    return false;
  }

  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(file.readString());
  file.close();

  if (!jsonObject.success()) {
    note(String(backup ? "Backup" : "Settings") + " file error.");
    return false;
  }

  if (jsonObject.containsKey("ssid")) {
    ssid = jsonObject["ssid"].as<String>();
  }
  if (jsonObject.containsKey("password")) {
    password = jsonObject["password"].as<String>();
  }

  if (jsonObject.containsKey("smart")) {
    smartString = jsonObject["smart"].as<String>();
    setSmart();
  }
  if (jsonObject.containsKey("uprisings")) {
    uprisings = jsonObject["uprisings"].as<int>() + 1;
  }
  if (jsonObject.containsKey("offset")) {
    offset = jsonObject["offset"].as<int>();
  }
  if (jsonObject.containsKey("sunset")) {
    sunset = jsonObject["sunset"].as<int>();
  }
  if (jsonObject.containsKey("sunrise")) {
    sunrise = jsonObject["sunrise"].as<int>();
  }
  if (jsonObject.containsKey("boundary")) {
    boundary = jsonObject["boundary"].as<int>();
  }
  if (jsonObject.containsKey("reversed")) {
    reversed = jsonObject["reversed"].as<bool>();
  }

  if (jsonObject.containsKey("steps1")) {
    steps1 = jsonObject["steps1"].as<int>();
  }
  if (jsonObject.containsKey("steps2")) {
    steps2 = jsonObject["steps2"].as<int>();
  }
  if (jsonObject.containsKey("steps3")) {
    steps3 = jsonObject["steps3"].as<int>();
  }

  if (jsonObject.containsKey("destination1")) {
    destination1 = jsonObject["destination1"].as<int>();
    actual1 = destination1;
  }
  if (jsonObject.containsKey("destination2")) {
    destination2 = jsonObject["destination2"].as<int>();
    actual2 = destination2;
  }
  if (jsonObject.containsKey("destination3")) {
    destination3 = jsonObject["destination3"].as<int>();
    actual3 = destination3;
  }

  String logs;
  jsonObject.printTo(logs);
  note("The " + String(backup ? "backup" : "settings") + " file has been read:\n " + logs);

  saveSettings();

  return true;
}

void saveSettings() {
  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.createObject();

  jsonObject["ssid"] = ssid;
  jsonObject["password"] = password;

  jsonObject["smart"] = smartString;
  jsonObject["uprisings"] = uprisings;
  jsonObject["offset"] = offset;
  jsonObject["sunset"] = sunset;
  jsonObject["sunrise"] = sunrise;
  jsonObject["boundary"] = boundary;
  jsonObject["reversed"] = reversed;

  jsonObject["steps1"] = steps1;
  jsonObject["steps2"] = steps2;
  jsonObject["steps3"] = steps3;
  jsonObject["destination1"] = destination1;
  jsonObject["destination2"] = destination2;
  jsonObject["destination3"] = destination3;

  if (writeObjectToFile("settings", jsonObject)) {
    String logs;
    jsonObject.printTo(logs);
    note("Saving settings:\n " + logs);

    writeObjectToFile("backup", jsonObject);
  } else {
    note("Saving the settings failed!");
  }
}

void resume() {
  File file = SPIFFS.open("/resume.txt", "r");
  if (!file) {
    return;
  }

  size_t size = file.size();
  char *buffer = new char[size];

  file.readBytes(buffer, size);
  file.close();

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& jsonObject = jsonBuffer.parseObject(buffer);
  delete buffer;

  if (jsonObject.success()) {
    String logs = "";

    if (jsonObject.containsKey("1")) {
      actual1 = jsonObject["1"].as<int>();
      if (actual1 != destination1) {
        logs = "\n1 by " + String(destination1 - actual1) + " steps to the state of " + toPercentages(destination1, steps1) + "%";
      }
    }
    if (jsonObject.containsKey("2")) {
      actual2 = jsonObject["2"].as<int>();
      if (actual2 != destination2) {
        logs += "\n2 by " + String(destination2 - actual2) + " steps to the state of " + toPercentages(destination2, steps2) + "%";
      }
    }
    if (jsonObject.containsKey("3")) {
      actual3 = jsonObject["3"].as<int>();
      if (actual3 != destination3) {
        logs += "\n3 by " + String(destination3 - actual3) + " steps to the state of " + toPercentages(destination3, steps3) + "%";
      }
    }

    if (actual1 != destination1 || actual2 != destination2 || actual3 != destination3) {
      note("The roller blind will be moved: " + logs);
    } else {
      if (SPIFFS.exists("/resume.txt")) {
        SPIFFS.remove("/resume.txt");
      }
    }
  }
}

void saveTheState() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& jsonObject = jsonBuffer.createObject();

  jsonObject["1"] = actual1;
  jsonObject["2"] = actual2;
  jsonObject["3"] = actual3;

  writeObjectToFile("resume", jsonObject);
}


void sayHelloToTheServer() {
  // if (offline) {
  //   return;
  // }
  //
  // String request = "ip=" + WiFi.localIP().toString()
  // + "&deets=" + steps1 + "." + steps2 + "." + steps3 + "," + RTC.isrunning() + "," + startTime + "," + reversed + "," + uprisings + "," + version + "," + boundary
  // + "&tw=" + String(twilight) + "," + light;
  //
  // if (sendingError) {
  //   request += "&val=" + toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3)
  //   + "&set=" + (sunset > 0 ? sunset : 0)
  //   + "&rise=" + (sunrise > 0 ? sunrise : 0);
  //
  //   putOnlineData("detail", request);
  // } else {
  //   putOnlineData("rooms", request);
  // }
}

void startServices() {
  server.on("/hello", HTTP_POST, handshake);
  server.on("/set", HTTP_PUT, receivedTheData);
  server.on("/state", HTTP_GET, requestForState);
  server.on("/basicdata", HTTP_GET, requestForBasicData);
  server.on("/log", HTTP_GET, requestForLogs);
  server.on("/log", HTTP_DELETE, clearLogs);
  server.on("/reversed", HTTP_POST, reverseDirection);
  server.on("/measurement/start", HTTP_POST, makeMeasurement);
  server.on("/measurement/cancel", HTTP_POST, cancelMeasurement);
  server.on("/measurement/end", HTTP_POST, endMeasurement);
  server.on("/admin/reset", HTTP_POST, setMin);
  server.on("/admin/setmax", HTTP_POST, setMax);
  server.on("/admin/onlineswitch", HTTP_POST, onlineSwitch);
  server.on("/admin/delete/wifisettings", HTTP_DELETE, deleteWiFiSettings);
  server.begin();

  note("Launch of services. MDNS responder " + String(hostName) + (MDNS.begin(hostName) ? " started." : " unsuccessful!"));

  MDNS.addService("idom", "tcp", 8080);

  if (light == -1 || !RTC.isrunning()) {
    getOfflineData();
  }
}

void handshake() {
  readData(server.arg("plain"), true);

  String reply = "{\"id\":\"" + WiFi.macAddress()
  + "\",\"version\":" + version
  + ",\"value1\":" + toPercentages(destination1, steps1)
  + ",\"value2\":" + toPercentages(destination2, steps2)
  + ",\"value3\":" + toPercentages(destination3, steps3)
  + ",\"twilight\":\"" + twilight + "," + light
  + "\",\"sunset\":" + (sunset > 0 ? sunset : 0)
  + ",\"sunrise\":" + (sunrise > 0 ? sunrise : 0)
  + ",\"smart\":\"" + smartString
  + "\",\"steps1\":" + steps1
  + ",\"steps2\":" + steps2
  + ",\"steps3\":" + steps3
  + ",\"boundary\":" + boundary
  + ",\"rtc\":" + RTC.isrunning()
  + ",\"active\":" + (startTime != 0 ? RTC.now().unixtime() - offset - startTime : 0)
  + ",\"uprisings\":" + uprisings
  + ",\"offline\":" + offline
  + ",\"reversed\":" + reversed + "}";

  Serial.print("\nHandshake");
  server.send(200, "text/plain", reply);
}

void requestForState() {
  String reply = "{\"state1\":\"" + toPercentages(destination1, steps1)
  + (!measurement && destination1 != actual1 ? "^" + toPercentages(actual1, steps1) : "")

  + "\",\"state2\":\"" + toPercentages(destination2, steps2)
  + (!measurement && destination2 != actual2 ? "^" + toPercentages(actual2, steps2) : "")

  + "\",\"state3\":\"" + toPercentages(destination3, steps3)
  + (!measurement && destination3 != actual3 ? "^" + toPercentages(actual3, steps3) : "")

  + (light > -1 ? "\",\"light\":\"" + String(light) + String(twilight ? "t" : "") + (twilightCounter > 0 ? ("," + String(twilightCounter)) : "") : "")

  + "\"}";

  server.send(200, "text/plain", reply);
}

void requestForBasicData() {
  String reply = "{\"time\":" + String(RTC.now().unixtime() - offset)
  + (light > -1 ? ",\"twilight\":" + String(twilight) : "")
  + "}";

  server.send(200, "text/plain", reply);
}

void reverseDirection() {
  reversed = !reversed;
  note("Engine set in the " + String(reversed ? "opposite" : "right") +  " direction");
  saveSettings();
  server.send(200, "text/plain", "Done");
  sayHelloToTheServer();
}

void setMin() {
  destination1 = 0;
  destination2 = 0;
  destination3 = 0;
  actual1 = 0;
  actual2 = 0;
  actual3 = 0;
  saveSettings();
  server.send(200, "text/plain", "Done");
  putOnlineData("detail", "val=0.0.0");
}

void setMax() {
  destination1 = steps1;
  destination2 = steps2;
  destination3 = steps3;
  actual1 = steps1;
  actual2 = steps2;
  actual3 = steps3;
  saveSettings();
  server.send(200, "text/plain", "Done");
  putOnlineData("detail", "val=100.100.100");
}

void makeMeasurement() {
  if (measurement) {
    return;
  }

  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(server.arg("plain"));

  if (!jsonObject.success()) {
    server.send(200, "text/plain", "Body not received");
    return;
  }

  if (bipolar && jsonObject.containsKey("wings")) {
    wings = jsonObject["wings"].as<int>();
  } else {
    wings = 123;
  }

  if ((strContains(String(wings), "1") && !(destination1 == 0 || actual1 == destination1))
  || (strContains(String(wings), "2") && !(destination2 == 0 || actual2 == destination2))
  || (strContains(String(wings), "3") && !(destination3 == 0 || actual3 == destination3))) {
    server.send(200, "text/plain", "Cannot execute a command");
    return;
  }

  measurement = true;
  server.send(200, "text/plain", "Done");

  digitalWrite(bipolar_enable_pin, LOW);
  digitalWrite(bipolar_direction_pin, !reversed);
}

void cancelMeasurement() {
  if (!measurement) {
    return;
  }

  measurement = false;
  server.send(200, "text/plain", "Done");
}

void endMeasurement() {
  if (!measurement) {
    return;
  }

  measurement = false;
  server.send(200, "text/plain", "Done");

  if (strContains(String(wings), "1")) {
    steps1 = actual1;
    destination1 = actual1;
  }
  if (strContains(String(wings), "2")) {
    steps2 = actual2;
    destination2 = actual2;
  }
  if (strContains(String(wings), "3")) {
    steps3 = actual3;
    destination3 = actual3;
  }
  setStepperPins(false);
  note("Measurement completed");
  saveSettings();
  sayHelloToTheServer();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
  } else {
    if (measurement) {
      measurement = false;
    }
  }

  server.handleClient();
  MDNS.update();

  if (measurement) {
    rotation();
    return;
  }

  if (calibration != 0) {
    rotation();
    if (calibration < 0) {
      calibration++;
    } else {
      calibration--;
    }
    if (calibration == 0) {
      setStepperPins(false);
    }
    return;
  }

  if (hasTimeChanged()) {
    if (loopTime % 2 == 0) {
      getOnlineData();
    }
    if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
      saveTheState();
    } else {
      if (bipolar) {
        if (step > 0) {
          step--;
        }
        if (step == 0) {
          step--;
          digitalWrite(bipolar_enable_pin, HIGH);
          Serial.print("\nMotor controller deactivated");
        }
      }
    }
    checkSmart(hasTheLightChanged());
  }

  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    rotation();
    if (destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
      Serial.print("\nRoller blind reached the target position");
      setStepperPins(false);
      if (SPIFFS.exists("/resume.txt")) {
        SPIFFS.remove("/resume.txt");
      }
    }
  }
}


bool hasTheLightChanged() {
  if (light == -1 || loopTime % 60 != 0) {
    return false;
  }

  int newLight = analogRead(light_sensor_pin);
  if (abs(light - newLight) > 20) {
    light = newLight;
  }

  bool result = false;
  bool sent = false;

  if (twilight != (light < (twilight ? boundary + 20 : boundary - 20))) {
    if (++twilightCounter > 9) {
      twilight = light < boundary;
      result = true;
      putOfflineData("{\"twilight\":" + String(twilight) + "}");

      if (RTC.isrunning()) {
        if (twilight) {
          if (sunset <= sunrise) {
            sunset = RTC.now().unixtime() - offset;
            putOnlineData("detail", "set=" + String(sunset) + "&tw=" + String(twilight) + "," + light);
            saveSettings();
            sent = true;
          }
        } else {
          if (sunrise <= sunset) {
            sunrise = RTC.now().unixtime() - offset;
            putOnlineData("detail", "rise=" + String(sunrise) + "&tw=" + String(twilight) + "," + light);
            saveSettings();
            sent = true;
          }
        }
      }
    }
  } else {
    twilightCounter = 0;
  }

  if (!sent) {
    putOnlineData("detail", "tw=" + String(twilight) + "," + light);
  }

  return result;
}

void readData(String payload, bool perWiFi) {
  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(payload);

  if (!jsonObject.success()) {
    if (payload.length() > 0) {
      Serial.print("\n Parsing failed!");
    }
    return;
  }

  if (jsonObject.containsKey("calibrate")) {
    if (bipolar && jsonObject.containsKey("wings")) {
      wings = jsonObject["wings"].as<int>();
    } else {
      wings = 123;
    }

    prepareCalibration(jsonObject["calibrate"].as<int>());
    return;
  }

  bool settingsChange = false;
  String result = "";

  uint32_t newTime = 0;
  if (jsonObject.containsKey("offset")) {
    int newOffset = jsonObject["offset"].as<int>();
    if (offset != newOffset) {
      if (RTC.isrunning()) {
        newTime = RTC.now().unixtime() - offset;
      }
      offset = newOffset;

      if (RTC.isrunning() && !jsonObject.containsKey("time")) {
        newTime = newTime + offset;
        if (abs(newTime - RTC.now().unixtime()) > 10) {
          RTC.adjust(DateTime(newTime));
          note("Adjust time");
        }
      }

      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("time")) {
    newTime = jsonObject["time"].as<uint32_t>() + offset;
    if (newTime > 1546304461) {
      if (RTC.isrunning()) {
        if (abs(newTime - RTC.now().unixtime()) > 10) {
          RTC.adjust(DateTime(newTime));
          note("Adjust time");
        }
      } else {
        RTC.adjust(DateTime(newTime));
        startTime = RTC.now().unixtime() - offset;
        note("Adjust time");
      }
    }
  }

  if (jsonObject.containsKey("up")) {
    uint32_t newUpdateTime = jsonObject["up"].as<uint32_t>();
    if (updateTime < newUpdateTime) {
      updateTime = newUpdateTime;
    }
  }

  if (jsonObject.containsKey("smart")) {
    String newSmartString = jsonObject["smart"].as<String>();
    if (smartString != newSmartString) {
      smartString = newSmartString;
      setSmart();
      result = "smart=" + newSmartString;
      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("val")) {
    String newValue = jsonObject["val"].as<String>();
    destination1 = steps1 > 0 ? toSteps(newValue.substring(0, newValue.indexOf(".")).toInt(), steps1) : destination1;

    if (bipolar) {
      newValue = newValue.substring(newValue.indexOf(".") + 1, newValue.length());
      destination2 = steps2 > 0 ? toSteps(newValue.substring(0, newValue.indexOf(".")).toInt(), steps2) : destination2;
      destination3 = steps3 > 0 ? toSteps(newValue.substring(newValue.indexOf(".") + 1, newValue.length()).toInt(), steps3) : destination3;
    }

    if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
      prepareRotation();
      result += String(result.length() > 0 ? "&" : "") + "val=" + toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3);
    }
  }

  int newSteps;
  if (jsonObject.containsKey("steps1")) {
    newSteps = jsonObject["steps1"].as<int>();
    if (steps1 != newSteps && actual1 == 0) {
      steps1 = newSteps;
      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("steps2")) {
    newSteps = jsonObject["steps2"].as<int>();
    if (steps2 != newSteps && actual2 == 0) {
      steps2 = newSteps;
      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("steps3")) {
    int newSteps = jsonObject["steps3"].as<int>();
    if (steps3 != newSteps && actual3 == 0) {
      steps3 = newSteps;
      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("boundary")) {
    int newBoundary = jsonObject["boundary"].as<int>();
    if (boundary != newBoundary) {
      boundary = newBoundary;
      settingsChange = true;
    }
  }

  if (light == -1 && jsonObject.containsKey("twilight")) {
    bool newTwilight = jsonObject["twilight"].as<bool>();
    if (twilight != newTwilight) {
      twilight = newTwilight;
      checkSmart(true);
    }
  }

  if (settingsChange) {
    note("Received the data:\n " + payload);
    saveSettings();
  }
  if (perWiFi && result.length() > 0) {
    putOnlineData("detail", result);
  }
}

void setSmart() {
  if (smartString.length() < 2) {
    smartCount = 0;
    return;
  }

  String smart;
  String wing;
  String days;
  bool loweringAtNight;
  bool liftingAtDay;
  int loweringTime;
  int liftingTime;
  bool enabled;

  smartCount = 1;
  for (byte b: smartString) {
    if (b == ',') {
      smartCount++;
    }
  }

  if (smartArray != 0) {
    delete [] smartArray;
  }
  smartArray = new Smart[smartCount];

  for (int i = 0; i < smartCount; i++) {
    smart = get1Smart(i);
    if (smart.length() > 0 && strContains(smart, "b")) {
      enabled = !strContains(smart, "/");
      smart = !enabled ? smart.substring(0, smart.indexOf("/")) : smart;

      loweringTime = strContains(smart, "_") ? smart.substring(0, smart.indexOf("_")).toInt() : -1;
      liftingTime = strContains(smart, "-") ? smart.substring(smart.indexOf("-") + 1, smart.length()).toInt() : -1;

      smart = strContains(smart, "_") ? smart.substring(smart.indexOf("_") + 1, smart.length()) : smart;
      smart = strContains(smart, "-") ? smart.substring(0, smart.indexOf("-")) : smart;

      wing = strContains(smart, "4") ? "123" : "";
      wing += strContains(smart, "1") ? "1" : "";
      wing += strContains(smart, "2") ? "2" : "";
      wing += strContains(smart, "3") ? "3" : "";
      wing += wing == "" ? "123" : "";

      days = strContains(smart, "w") ? "ouehras" : "";
      days += strContains(smart, "o") ? "o" : "";
      days += strContains(smart, "u") ? "u" : "";
      days += strContains(smart, "e") ? "e" : "";
      days += strContains(smart, "h") ? "h" : "";
      days += strContains(smart, "r") ? "r" : "";
      days += strContains(smart, "a") ? "a" : "";
      days += strContains(smart, "s") ? "s" : "";

      loweringAtNight = strContains(smart, "n");
      liftingAtDay = strContains(smart, "d");

      smartArray[i] = (Smart) {wing, days, loweringAtNight, liftingAtDay, loweringTime, liftingTime, enabled, 0};
    }
  }
}

void checkSmart(bool lightChanged) {
  if (!RTC.isrunning()) {
    return;
  }

  bool result = false;
  DateTime now = RTC.now();
  int currentTime = (now.hour() * 60) + now.minute();
  String log = "The smart function has";

  int i = -1;
  while (++i < smartCount) {
    if (smartArray[i].enabled) {
      if (lightChanged) {
        if (twilight && smartArray[i].loweringAtNight && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])) {
          if (strContains(smartArray[i].wing, "1")) {
            destination1 = steps1;
          }
          if (strContains(smartArray[i].wing, "2")) {
            destination2 = steps2;
          }
          if (strContains(smartArray[i].wing, "3")) {
            destination3 = steps3;
          }
          result = true;
          log += " activated lowering the roller blind for the night";
        }
        if (!twilight && smartArray[i].liftingAtDay && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() != 0 ? now.dayOfTheWeek() - 1 : now.dayOfTheWeek() + 6])) {
          if (strContains(smartArray[i].wing, "1")) {
            destination1 = 0;
          }
          if (strContains(smartArray[i].wing, "2")) {
            destination2 = 0;
          }
          if (strContains(smartArray[i].wing, "3")) {
            destination3 = 0;
          }
          result = true;
          log += " activated lifting the roller blind for the day";
        }
      } else {
        if (smartArray[i].access + 60 < now.unixtime()) {
          if (smartArray[i].loweringTime == currentTime && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])) {
            smartArray[i].access = now.unixtime();
            if (strContains(smartArray[i].wing, "1")) {
              destination1 = steps1;
            }
            if (strContains(smartArray[i].wing, "2")) {
              destination2 = steps2;
            }
            if (strContains(smartArray[i].wing, "3")) {
              destination3 = steps3;
            }
            result = true;
            log += " activated lowering the roller blind on time";
          }
          if (smartArray[i].liftingTime == currentTime && (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) || strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() != 0 ? now.dayOfTheWeek() - 1 : now.dayOfTheWeek() + 6]))) {
            smartArray[i].access = now.unixtime();
            if (strContains(smartArray[i].wing, "1")) {
              destination1 = 0;
            }
            if (strContains(smartArray[i].wing, "2")) {
              destination2 = 0;
            }
            if (strContains(smartArray[i].wing, "3")) {
              destination3 = 0;
            }
            result = true;
            log += " activated lifting the roller blind on time";
          }
        }
      }
    }
  }

  if (result && (destination1 != actual1 || destination2 != actual2 || destination3 != actual3)) {
    note(log);
    putOnlineData("detail", "val=" + toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3));
    prepareRotation();
  } else {
    if (lightChanged) {
      note(log + "n't activated anything.");
    }
  }
}


void prepareCalibration(int set) {
  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    return;
  }

  if (bipolar) {
    digitalWrite(bipolar_enable_pin, LOW);
    digitalWrite(bipolar_direction_pin, set < 0 ? reversed : !reversed);
  }

  bool settingsChange = false;
  String logs = "";

  if (strContains(String(wings), "1")) {
    if (actual1 == 0) {
      calibration = bipolar ? set / 2 : set;
    } else
    if (actual1 == steps1) {
      steps1 += bipolar ? set / 2 : set;
      destination1 += bipolar ? set / 2 : set;
      settingsChange = true;
      logs += "\n1 movement by " + String(set) + " steps. Steps set at " + String(steps1) + ".";
    }
  }
  if (strContains(String(wings), "2")) {
    if (actual2 == 0) {
      calibration = bipolar ? set / 2 : set;
    } else
    if (actual2 == steps2) {
      steps2 += bipolar ? set / 2 : set;
      destination2 += bipolar ? set / 2 : set;
      settingsChange = true;
      logs += "\n2 movement by " + String(set) + " steps. Steps set at " + String(steps2) + ".";
    }
  }
  if (strContains(String(wings), "3")) {
    if (actual3 == 0) {
      calibration = bipolar ? set / 2 : set;
    } else
    if (actual3 == steps3) {
      steps3 += bipolar ? set / 2 : set;
      destination3 += bipolar ? set / 2 : set;
      settingsChange = true;
      logs += "\n3 movement by " + String(set) + " steps. Steps set at " + String(steps3) + ".";
    }
  }

  if (settingsChange) {
    note("Height calibration: " + logs);
    saveSettings();
  } else {
    note("Calibration of position 0. Wings " + String(wings) + " movement by " + String(set) + " steps.");
  }
}

void prepareRotation() {
  if (bipolar) {
    digitalWrite(bipolar_enable_pin, LOW);
  }
  String logs = "";
  if (actual1 != destination1) {
    logs = "\n1 by " + String(destination1 - actual1) + " steps. Change of state to " + toPercentages(destination1, steps1) + "%";
  }
  if (actual2 != destination2) {
    logs += "\n2 by " + String(destination2 - actual2) + " steps. Change of state to " + toPercentages(destination2, steps2) + "%";
  }
  if (actual3 != destination3) {
    logs += "\n3 by " + String(destination3 - actual3) + " steps. Change of state to " + toPercentages(destination3, steps3) + "%";
  }
  note("Roller blinds movement: " + logs);

  saveSettings();
  saveTheState();
}

void rotation() {
  if (bipolar) {
    bipolarRotation();
    delay(4);
  } else {
    unipolarRotation();
    delay(1);
  }
}

void bipolarRotation() {
  if (measurement) {
    if (strContains(String(wings), "1")) {
      digitalWrite(bipolar_step_pin[0], HIGH);
      actual1++;
    }
  } else {
    if (destination1 != actual1) {
      digitalWrite(bipolar_direction_pin, destination1 < actual1 ? reversed : !reversed);
      digitalWrite(bipolar_step_pin[0], HIGH);
      if (destination1 > actual1) {
        actual1++;
      } else {
        actual1--;
      }
    }
    digitalWrite(bipolar_step_pin[0], calibration !=0 && strContains(String(wings), "1") ? HIGH : LOW);
  }
  digitalWrite(bipolar_step_pin[0], LOW);

  if (measurement) {
    if (strContains(String(wings), "2")) {
      digitalWrite(bipolar_step_pin[1], HIGH);
      actual2++;
    }
  } else {
    if (destination2 != actual2
      && (((destination1 == actual1 || destination1 > actual1) && destination2 > actual2)
      || ((destination1 == actual1 || destination1 < actual1) && destination2 < actual2))) {
      digitalWrite(bipolar_direction_pin, destination2 < actual2 ? reversed : !reversed);
      digitalWrite(bipolar_step_pin[1], HIGH);
      if (destination2 > actual2) {
        actual2++;
      } else {
        actual2--;
      }
    }
    digitalWrite(bipolar_step_pin[1], calibration !=0 && strContains(String(wings), "2") ? HIGH : LOW);
  }
  digitalWrite(bipolar_step_pin[1], LOW);

  if (measurement) {
    if (strContains(String(wings), "3")) {
      digitalWrite(bipolar_step_pin[2], HIGH);
      actual3++;
    }
  } else {
    if (destination3 != actual3
      && (((destination1 == actual1 || destination1 > actual1) && (destination2 == actual2 || destination2 > actual2) && destination3 > actual3)
      || ((destination1 == actual1 || destination1 < actual1) && (destination2 == actual2 || destination2 < actual2) && destination3 < actual3))) {
      digitalWrite(bipolar_direction_pin, destination3 < actual3 ? reversed : !reversed);
      digitalWrite(bipolar_step_pin[2], HIGH);
      if (destination3 > actual3) {
        actual3++;
      } else {
        actual3--;
      }
    }
    digitalWrite(bipolar_step_pin[2], calibration !=0 && strContains(String(wings), "3") ? HIGH : LOW);
  }
  digitalWrite(bipolar_step_pin[2], LOW);
}

void unipolarRotation() {
  if (measurement) {
    actual1++;
  } else {
    if (destination1 > actual1) {
      actual1++;
    } else {
      actual1--;
    }
  }

  for (int x = 0; x < 1; x++) {
    switch (step) {
      case 0:
        digitalWrite(unipolar_pin[0], LOW);
        digitalWrite(unipolar_pin[1], LOW);
        digitalWrite(unipolar_pin[2], LOW);
        digitalWrite(unipolar_pin[3], HIGH);
        break;
      case 1:
        digitalWrite(unipolar_pin[0], LOW);
        digitalWrite(unipolar_pin[1], LOW);
        digitalWrite(unipolar_pin[2], HIGH);
        digitalWrite(unipolar_pin[3], HIGH);
        break;
      case 2:
        digitalWrite(unipolar_pin[0], LOW);
        digitalWrite(unipolar_pin[1], LOW);
        digitalWrite(unipolar_pin[2], HIGH);
        digitalWrite(unipolar_pin[3], LOW);
        break;
      case 3:
        digitalWrite(unipolar_pin[0], LOW);
        digitalWrite(unipolar_pin[1], HIGH);
        digitalWrite(unipolar_pin[2], HIGH);
        digitalWrite(unipolar_pin[3], LOW);
        break;
      case 4:
        digitalWrite(unipolar_pin[0], LOW);
        digitalWrite(unipolar_pin[1], HIGH);
        digitalWrite(unipolar_pin[2], LOW);
        digitalWrite(unipolar_pin[3], LOW);
        break;
      case 5:
        digitalWrite(unipolar_pin[0], HIGH);
        digitalWrite(unipolar_pin[1], HIGH);
        digitalWrite(unipolar_pin[2], LOW);
        digitalWrite(unipolar_pin[3], LOW);
        break;
      case 6:
        digitalWrite(unipolar_pin[0], HIGH);
        digitalWrite(unipolar_pin[1], LOW);
        digitalWrite(unipolar_pin[2], LOW);
        digitalWrite(unipolar_pin[3], LOW);
        break;
      case 7:
        digitalWrite(unipolar_pin[0], HIGH);
        digitalWrite(unipolar_pin[1], LOW);
        digitalWrite(unipolar_pin[2], LOW);
        digitalWrite(unipolar_pin[3], HIGH);
        break;
      default:
        digitalWrite(unipolar_pin[0], LOW);
        digitalWrite(unipolar_pin[1], LOW);
        digitalWrite(unipolar_pin[2], LOW);
        digitalWrite(unipolar_pin[3], LOW);
        break;
    }

    if (measurement) {
      if (reversed) {
        step++;
      } else {
        step--;
      }
    } else {
      if (calibration != 0) {
        if (calibration < 0) {
          if (reversed) {
            step--;
          } else {
            step++;
          }
        } else {
          if (reversed) {
            step++;
          } else {
            step--;
          }
        }
      } else {
        if (destination1 < actual1) {
          if (reversed) {
            step--;
          } else {
            step++;
          }
        } else {
          if (reversed) {
            step++;
          } else {
            step--;
          }
        }
      }
    }
    if (step > 7) {
      step = 0;
    }
    if (step < 0) {
      step = 7;
    }
  }
}
