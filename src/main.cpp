#include <c_online.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  SPIFFS.begin();
  Wire.begin();

  keepLog = SPIFFS.exists("/log.txt");

  note("iDom Blinds " + String(version));
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
    startTime = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
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
    if (setMode) {
      for (int i = 0; i < 3; i++) {
        pinMode(bipolar_step_pin[i], OUTPUT);
      }
      pinMode(bipolar_direction_pin, OUTPUT);
      pinMode(bipolar_enable_pin, OUTPUT);
    }
    step = 30;
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
    note("The " + String(backup ? "backup" : "settings") + " file cannot be read");
    return false;
  }

  DynamicJsonDocument jsonObject(1024);
  deserializeJson(jsonObject, file.readString());
  file.close();

  if (jsonObject.isNull() || jsonObject.size() < 5) {
    note(String(backup ? "Backup" : "Settings") + " file error");
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
  if (jsonObject.containsKey("dst")) {
    dst = jsonObject["dst"].as<bool>();
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
  serializeJson(jsonObject, logs);
  note("Reading the " + String(backup ? "backup" : "settings") + " file:\n " + logs);

  saveSettings();

  return true;
}

void saveSettings() {
  DynamicJsonDocument jsonObject(1024);

  jsonObject["ssid"] = ssid;
  jsonObject["password"] = password;

  jsonObject["smart"] = smartString;
  jsonObject["uprisings"] = uprisings;
  jsonObject["offset"] = offset;
  jsonObject["dst"] = dst;
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
    serializeJson(jsonObject, logs);
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

  DynamicJsonDocument jsonObject(1024);
  deserializeJson(jsonObject, file.readString());
  file.close();

  if (!jsonObject.isNull() && jsonObject.size() > 0) {
    String logs = "";

    if (jsonObject.containsKey("1")) {
      actual1 = jsonObject["1"].as<int>();
      if (actual1 != destination1) {
        logs = "\n1 to " + String(destination1 - actual1) + " steps to " + toPercentages(destination1, steps1) + "%";
      }
    }
    if (jsonObject.containsKey("2")) {
      actual2 = jsonObject["2"].as<int>();
      if (actual2 != destination2) {
        logs += "\n2 to " + String(destination2 - actual2) + " steps to " + toPercentages(destination2, steps2) + "%";
      }
    }
    if (jsonObject.containsKey("3")) {
      actual3 = jsonObject["3"].as<int>();
      if (actual3 != destination3) {
        logs += "\n3 to " + String(destination3 - actual3) + " steps to " + toPercentages(destination3, steps3) + "%";
      }
    }

    if (actual1 != destination1 || actual2 != destination2 || actual3 != destination3) {
      note("Resume: " + logs);
    } else {
      if (SPIFFS.exists("/resume.txt")) {
        SPIFFS.remove("/resume.txt");
      }
    }
  }
}

void saveTheState() {
  DynamicJsonDocument jsonObject(1024);

  jsonObject["1"] = actual1;
  jsonObject["2"] = actual2;
  jsonObject["3"] = actual3;

  writeObjectToFile("resume", jsonObject);
}


void sayHelloToTheServer() {
  // This function is only available with a ready-made iDom device.
}

void introductionToServer() {
  // This function is only available with a ready-made iDom device.
}

void startServices() {
  server.on("/hello", HTTP_POST, handshake);
  server.on("/set", HTTP_PUT, receivedOfflineData);
  server.on("/state", HTTP_GET, requestForState);
  server.on("/basicdata", HTTP_GET, requestForBasicData);
  server.on("/reversed", HTTP_POST, reverseDirection);
  server.on("/measurement/start", HTTP_POST, makeMeasurement);
  server.on("/measurement/cancel", HTTP_POST, cancelMeasurement);
  server.on("/measurement/end", HTTP_POST, endMeasurement);
  server.on("/log", HTTP_GET, requestForLogs);
  server.on("/log", HTTP_DELETE, clearTheLog);
  server.on("/admin/reset", HTTP_POST, setMin);
  server.on("/admin/setmax", HTTP_POST, setMax);
  server.on("/admin/sensor", HTTP_POST, initiateTheLightSensor);
  server.on("/admin/log", HTTP_POST, activationTheLog);
  server.on("/admin/log", HTTP_DELETE, deactivationTheLog);
  server.on("/admin/wifisettings", HTTP_DELETE, deleteWiFiSettings);
  server.begin();

  note("Launch of services. " + String(hostName) + (MDNS.begin(hostName) ? " started." : " unsuccessful!"));

  MDNS.addService("idom", "tcp", 8080);

  if (!offline) {
    prime = true;
  }
  getOfflineData(true);
}

String getBlindsDetail() {
  return String(steps1) + "." + steps2 + "." + steps3 + "," + RTC.isrunning() + "," + String(startTime) + "," + reversed + "," + uprisings + "," + version + "," + boundary;
}

String getLightStatus() {
  return String(light) + String(twilight ? "t" : "") + (twilightCounter > 0 ? ("," + String(twilightCounter)) : "");
}

void handshake() {
  readData(server.arg("plain"), true);

  String reply = "\"id\":\"" + WiFi.macAddress()
  + "\",\"version\":" + version
  + ",\"value\":\"" + toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3)
  + "\",\"light\":\"" + getLightStatus()
  + "\",\"sunset\":" + (light > -1 || sunset > 0 ? sunset : 0)
  + ",\"sunrise\":" + (light > -1 || sunrise > 0 ? sunrise : 0)
  + ",\"smart\":\"" + smartString
  + "\",\"steps\":\"" + steps1 + "." + steps2 + "." + steps3
  + "\",\"boundary\":" + boundary
  + ",\"rtc\":" + RTC.isrunning()
  + ",\"dst\":" + dst
  + ",\"offset\":" + offset
  + ",\"time\":" + String(RTC.now().unixtime() - offset - (dst ? 3600 : 0))
  + ",\"active\":" + String(startTime != 0 ? RTC.now().unixtime() - offset - (dst ? 3600 : 0) - startTime : 0)
  + ",\"uprisings\":" + uprisings
  + ",\"offline\":" + offline
  + ",\"prime\":" + prime
  + ",\"reversed\":" + reversed;

  Serial.print("\nHandshake");
  server.send(200, "text/plain", "{" + reply + "}");
}

void requestForState() {
  String reply = "\"state1\":\"" + toPercentages(destination1, steps1)
  + (!measurement && destination1 != actual1 ? "^" + toPercentages(actual1, steps1) : "")

  + "\",\"state2\":\"" + toPercentages(destination2, steps2)
  + (!measurement && destination2 != actual2 ? "^" + toPercentages(actual2, steps2) : "")

  + "\",\"state3\":\"" + toPercentages(destination3, steps3)
  + (!measurement && destination3 != actual3 ? "^" + toPercentages(actual3, steps3) : "")
  + "\""

  + (light > -1 ? ",\"light\":\"" + getLightStatus() + "\"" : "");

  server.send(200, "text/plain", "{" + reply + "}");
}

void requestForBasicData() {
  String reply = RTC.isrunning() ? ("\"time\":" + String(RTC.now().unixtime() - offset - (dst ? 3600 : 0))
  + ",\"offset\":" + offset
  + ",\"dst\":" + String(dst)) : "";

  reply += light > -1 ? (String(reply.length() > 0 ? "," : "") + "\"light\":\"" + String(light) + String(twilight ? "t" : "") + "\"") : "";

  reply += !offline && prime ? (String(reply.length() > 0 ? "," : "") + "\"prime\":" + String(prime)) : "";

  server.send(200, "text/plain", "{" + reply + "}");
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

  DynamicJsonDocument jsonObject(1024);
  deserializeJson(jsonObject, server.arg("plain"));

  if (jsonObject.isNull()) {
    server.send(200, "text/plain", "Body not received");
    return;
  }

  if (jsonObject.containsKey("wings")) {
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

void initiateTheLightSensor() {
  light = 100;

  server.send(200, "text/plain", "Done");
}


void loop() {
  if (WiFi.status() == WL_CONNECTED) {
  } else {
    sendingError = true;
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
    if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
      saveTheState();
    } else {
        if (step > 0) {
          step--;
        }
        if (step == 0) {
          step--;
          digitalWrite(bipolar_enable_pin, HIGH);
          Serial.print("\nMotor controller deactivated");
        }
    }
    if (!automaticSettings() && loopTime % 2 == 0) {
      getOnlineData(destination1 == actual1 && destination2 == actual2 && destination3 == actual3);
    };
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
  if (loopTime % 60 != 0) {
    return false;
  }

  int newLight = analogRead(light_sensor_pin);
  bool change = false;

  if (light == -1) {
    if (newLight > boundary) {
      light = newLight;
      change = true;
    } else {
      return false;
    }
  } else {
    if (abs(light - newLight) > (newLight < 30 ? 5 : 20)) {
      light = newLight;
      change = true;
    }
  }

  bool result = false;
  bool sent = false;

  if (blockTwilightCounter) {
    if (light < boundary - (boundary < 100 ? 0 : 50) || light > boundary + 50) {
      blockTwilightCounter = false;
    }
  } else {
    if (twilight != (light < (twilight ? boundary - (boundary < 100 ? 0 : 50) : boundary + 50))) {
      change = true;
      if (++twilightCounter > 9 && (twilight ? light > boundary : light < boundary)) {
        twilight = light < boundary;
        result = true;
        blockTwilightCounter = true;
        twilightCounter = 0;
        putMultiOfflineData("{\"light\":\"" + String(light) + String(twilight ? "t" : "") + "\"}");
      }
    } else {
      twilightCounter = 0;
    }
  }

  if (RTC.isrunning()) {
    if (twilight) {
      if (light < 70 && (sunset <= sunrise || (RTC.now().unixtime() - offset - (dst ? 3600 : 0) - sunset > 72000))) {
        sunset = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
        putOnlineData("detail", "set=" + String(sunset) + "&light=" + getLightStatus());
        saveSettings();
        sent = true;
      }
    }
    if (light > 100 && sunrise <= sunset) {
      if (++daybreakCounter > 9) {
        sunrise = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
        putOnlineData("detail", "rise=" + String(sunrise) + "&light=" + getLightStatus());
        saveSettings();
        sent = true;
        daybreakCounter = 0;
      }
    } else {
      daybreakCounter = 0;
    }
  }

  if (!sent && change) {
    putOnlineData("detail", "light=" + getLightStatus(), false);
  }

  return result;
}

void readData(String payload, bool perWiFi) {
  DynamicJsonDocument jsonObject(1024);
  deserializeJson(jsonObject, payload);

  if (jsonObject.isNull()) {
    if (payload.length() > 0) {
      Serial.print("\n Parsing failed!");
    }
    return;
  }

  if (jsonObject.containsKey("apk")) {
    perWiFi = jsonObject["apk"].as<bool>();
  }

  if (jsonObject.containsKey("calibrate")) {
    if (jsonObject.containsKey("wings")) {
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
      if (RTC.isrunning() && !jsonObject.containsKey("time")) {
        RTC.adjust(DateTime((RTC.now().unixtime() - offset) + newOffset));
        note("Time zone change");
      }

      offset = newOffset;
      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("dst")) {
    bool newDST = jsonObject["dst"].as<bool>();
    if (dst != newDST) {
      if (RTC.isrunning() && !jsonObject.containsKey("time")) {
        if (newDST) {
          newTime = RTC.now().unixtime() + 3600;
        } else {
          newTime = RTC.now().unixtime() - 3600;
        }
        RTC.adjust(DateTime(newTime));
        note(newDST ? "Summer time" : "Winter time");
      }

      dst = newDST;
      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("time")) {
    newTime = jsonObject["time"].as<uint32_t>() + offset + (dst ? 3600 : 0);
    if (newTime > 1546304461) {
      if (RTC.isrunning()) {
        if (abs(newTime - RTC.now().unixtime()) > 60) {
          RTC.adjust(DateTime(newTime));
        }
      } else {
        RTC.adjust(DateTime(newTime));
        note("Adjust time");
        startTime = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
        if (RTC.isrunning()) {
          sayHelloToTheServer();
        }
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
    int newDestination1 = steps1 > 0 ? toSteps(newValue.substring(0, newValue.indexOf(".")).toInt(), steps1) : destination1;
    int newDestination2 = destination2;
    int newDestination3 = destination3;

      newValue = newValue.substring(newValue.indexOf(".") + 1, newValue.length());
      newDestination2 = steps2 > 0 ? toSteps(newValue.substring(0, newValue.indexOf(".")).toInt(), steps2) : destination2;
      newDestination3 = steps3 > 0 ? toSteps(newValue.substring(newValue.indexOf(".") + 1, newValue.length()).toInt(), steps3) : destination3;

    if (destination1 != newDestination1 || destination2 != newDestination2 || destination3 != newDestination3) {
      destination1 = newDestination1;
      destination2 = newDestination2;
      destination3 = newDestination3;
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

  if (light == -1 && jsonObject.containsKey("light")) {
    String newLight = jsonObject["light"].as<String>();
    if (twilight != strContains(newLight, "t")) {
      twilight = !twilight;
      automaticSettings(true);
    }
  }

  if (jsonObject.containsKey("prime")) {
    prime = false;
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

  int count = 1;
  smartCount = 1;
  for (byte b: smartString) {
    if (b == ',') {
      count++;
    }
    if (b == 'b') {
      smartCount++;
    }
  }

  if (smartArray != 0) {
    delete [] smartArray;
  }
  smartArray = new Smart[smartCount];
  smartCount = 0;

  for (int i = 0; i < count; i++) {
    smart = get1Smart(i);
    if (smart.length() > 0 && strContains(smart, "b")) {
      enabled = !strContains(smart, "/");
      smart = enabled ? smart : smart.substring(1);

      loweringTime = strContains(smart, "_") ? smart.substring(0, smart.indexOf("_")).toInt() : -1;
      liftingTime = strContains(smart, "-") ? smart.substring(smart.indexOf("-") + 1, smart.length()).toInt() : -1;

      smart = strContains(smart, "_") ? smart.substring(smart.indexOf("_") + 1, smart.length()) : smart;
      smart = strContains(smart, "-") ? smart.substring(0, smart.indexOf("-")) : smart;

      wing = strContains(smart, "4") ? "123" : "";
      wing += strContains(smart, "1") ? "1" : "";
      wing += strContains(smart, "2") ? "2" : "";
      wing += strContains(smart, "3") ? "3" : "";
      wing += wing == "" ? "123" : "";

      days = strContains(smart, "w") ? "w" : "";
      days += strContains(smart, "o") ? "o" : "";
      days += strContains(smart, "u") ? "u" : "";
      days += strContains(smart, "e") ? "e" : "";
      days += strContains(smart, "h") ? "h" : "";
      days += strContains(smart, "r") ? "r" : "";
      days += strContains(smart, "a") ? "a" : "";
      days += strContains(smart, "s") ? "s" : "";

      loweringAtNight = strContains(smart, "n");
      liftingAtDay = strContains(smart, "d");

      smartArray[smartCount++] = (Smart) {wing, days, loweringAtNight, liftingAtDay, loweringTime, liftingTime, enabled, 0};
    }
  }
}

bool automaticSettings() {
  return automaticSettings(hasTheLightChanged());
}

bool automaticSettings(bool lightChanged) {
  bool result = false;
  DateTime now = RTC.now();
  String log = "Smart ";

  int i = -1;
  while (++i < smartCount) {
    if (smartArray[i].enabled) {
      if (lightChanged) {
        if (twilight && smartArray[i].loweringAtNight
          && (strContains(smartArray[i].days, "w") || (RTC.isrunning() && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])))) {
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
          log += "lowering at twilight";
        }
        if (!twilight && smartArray[i].liftingAtDay
          && (strContains(smartArray[i].days, "w") || (RTC.isrunning() && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])))) {
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
          log += "lifting at daybreak";
        }
      } else {
        if (RTC.isrunning()) {
          int currentTime = (now.hour() * 60) + now.minute();

          if (currentTime == 120 || currentTime == 180) {
            if (now.month() == 3 && now.day() > 24 && daysOfTheWeek[now.dayOfTheWeek()][0] == 's' && currentTime == 120 && !dst) {
              int newTime = RTC.now().unixtime() + 3600;
              RTC.adjust(DateTime(newTime));
              dst = true;
              saveSettings();
              note("Smart set to summer time");
            }
            if (now.month() == 10 && now.day() > 24 && daysOfTheWeek[now.dayOfTheWeek()][0] == 's' && currentTime == 180 && dst) {
              int newTime = RTC.now().unixtime() - 3600;
              RTC.adjust(DateTime(newTime));
              dst = false;
              saveSettings();
              note("Smart set to winter time");
            }
          }

          if (smartArray[i].access + 60 < now.unixtime()) {
            if (smartArray[i].loweringTime == currentTime
              && (strContains(smartArray[i].days, "w") || strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]))) {
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
              log += "lowering at time";
            }
            if (smartArray[i].liftingTime == currentTime
              && (strContains(smartArray[i].days, "w") || strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]))) {
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
              log += "lifting at time";
            }
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
      note("Smart didn't activate anything.");
    }
  }

  return result;
}


void prepareCalibration(int set) {
  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    return;
  }

    digitalWrite(bipolar_enable_pin, LOW);
    digitalWrite(bipolar_direction_pin, set < 0 ? reversed : !reversed);

  bool settingsChange = false;
  String logs = "";

  if (strContains(String(wings), "1")) {
    if (actual1 == 0) {
      calibration = set / 2;
    } else
    if (actual1 == steps1) {
      steps1 += set / 2;
      destination1 += set / 2;
      settingsChange = true;
      logs += "\n 1 by " + String(set) + " steps. Steps set at " + String(steps1) + ".";
    }
  }
  if (strContains(String(wings), "2")) {
    if (actual2 == 0) {
      calibration = set / 2;
    } else
    if (actual2 == steps2) {
      steps2 += set / 2;
      destination2 += set / 2;
      settingsChange = true;
      logs += "\n 2 by " + String(set) + " steps. Steps set at " + String(steps2) + ".";
    }
  }
  if (strContains(String(wings), "3")) {
    if (actual3 == 0) {
      calibration = set / 2;
    } else
    if (actual3 == steps3) {
      steps3 += set / 2;
      destination3 += set / 2;
      settingsChange = true;
      logs += "\n 3 by " + String(set) + " steps. Steps set at " + String(steps3) + ".";
    }
  }

  if (settingsChange) {
    note("Calibration: " + logs);
    saveSettings();
  } else {
    note("Zero calibration. " + String(wings) + " by " + String(set) + " steps.");
  }
}

void prepareRotation() {
    digitalWrite(bipolar_enable_pin, LOW);
  String logs = "";
  if (actual1 != destination1) {
    logs = "\n 1 by " + String(destination1 - actual1) + " steps to " + toPercentages(destination1, steps1) + "%";
  }
  if (actual2 != destination2) {
    logs += "\n 2 by " + String(destination2 - actual2) + " steps to " + toPercentages(destination2, steps2) + "%";
  }
  if (actual3 != destination3) {
    logs += "\n 3 by " + String(destination3 - actual3) + " steps to " + toPercentages(destination3, steps3) + "%";
  }
  note("Movement: " + logs);

  saveSettings();
  saveTheState();
}

void rotation() {
    bipolarRotation();
    delay(4);
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
