#include "core.h"

void setup() {
  Serial.begin(74880);
  while (!Serial) {}

  SPIFFS.begin();
  Wire.begin();

  note("iDom Blinds nx");
  Serial.print("\n Roller blind ID: " + WiFi.macAddress());
  Serial.printf("\n The roller blind is set to %s mode", offline ? "OFFLINE" : "ONLINE");

  sprintf(hostName, "blinds_%s", String(WiFi.macAddress()).c_str());
  WiFi.hostname(hostName);

  readSettings();
  resume();

  RTC.begin();
  if (RTC.isrunning()) {
    start = RTC.now().unixtime() - offset;
  }
  Serial.print("\n RTC initialization " + start != 0 ? "completed" : "failed!");

  int newLight = analogRead(light_sensor_pin);
  if (newLight > boundary) {
    light = newLight;
    twilight = newLight < boundary;
    twilightLoopTime = RTC.isrunning() ? RTC.now().unixtime() : millis() / 1000;
  }

  setStepperPins(true);

  if (ssid != "" && password != "") {
    connectingToWifi();
  } else {
    initiatingWPS();
  }
}

void setStepperPins(bool setMode) {
  for (int i = 0; i < 4; i++) {
    if (setMode) {
      pinMode(stepper_pin[i], OUTPUT);
    }
    digitalWrite(stepper_pin[i], LOW);
  }
}

String toPercentages(int value) {
  return String(value > 0 ? value * 100 / steps : 0);
}

int toSteps(int value) {
  return value > 0 ? value * steps / 100 : 0;
}


void readSettings() {
  File file = SPIFFS.open("/settings.txt", "r");
  if (!file) {
    note("The settings file cannot be read");
    return;
  }

  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(file.readString());
  file.close();

  if (!jsonObject.success()) {
    note("Settings file error:\n" + file.readString());
    return;
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
  if (jsonObject.containsKey("steps")) {
    steps = jsonObject["steps"].as<int>();
  }
  if (jsonObject.containsKey("boundary")) {
    boundary = jsonObject["boundary"].as<int>();
  }
  if (jsonObject.containsKey("destination")) {
    destination = jsonObject["destination"].as<int>();
    actual = destination;
  }
  if (jsonObject.containsKey("reversed")) {
    reversed = jsonObject["reversed"].as<bool>();
  }

  String logs;
  jsonObject.printTo(logs);
  note("The settings file has been read:\n " + logs);

  saveSettings();
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
  jsonObject["steps"] = steps;
  jsonObject["boundary"] = boundary;
  jsonObject["destination"] = destination;
  jsonObject["reversed"] = reversed;

  if (writeObjectToFile("settings", jsonObject)) {
    String logs;
    jsonObject.printTo(logs);
    note("Saving settings:\n " + logs);
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

  if (jsonObject.success()) {
    if (jsonObject.containsKey("actual")) {
      actual = jsonObject["actual"].as<float>();

      if (actual != destination) {
        note("The roller blind will be moved by " + String(destination - actual) + " steps to the state of " + toPercentages(destination) + "%");
      } else {
        if (SPIFFS.exists("/resume.txt")) {
          SPIFFS.remove("/resume.txt");
        }
      }
    }
  }

  delete buffer;
}

void saveTheState() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& jsonObject = jsonBuffer.createObject();

  jsonObject["actual"] = actual;

  writeObjectToFile("resume", jsonObject);
}


void sayHelloToTheServer() {
  // if (!offline) {
  //   String request = "ip=" + WiFi.localIP().toString()
  //   + "&deets=" + steps + "," + RTC.isrunning() + "," + start + "," + reversed + "," + uprisings + "," + version + "," + boundary
  //   + "&tw=" + String(twilight) + "," + light;
  //
  //   if (sendingError) {
  //     request += "&val=" + toPercentages(destination)
  //     + "&set=" + (sunset > 0 ? sunset : 0)
  //     + "&rise=" + (sunrise > 0 ? sunrise : 0);
  //
  //     putDataOnline("detail", request);
  //   } else {
  //     putDataOnline("rooms", request);
  //   }
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
  server.on("/reset", HTTP_POST, resetCoverage);
  server.on("/deletewifisettings", HTTP_DELETE, deleteWiFiSettings);
  server.on("/measurement", HTTP_POST, makeMeasurement);
  server.begin();

  note("Launch of services. MDNS responder " + String(hostName) + MDNS.begin(hostName) ? " started " : " unsuccessful! ");

  MDNS.addService("idom", "tcp", 8080);

  if (light == -1 || !RTC.isrunning()) {
    getBasicData();
  }
}

void handshake() {
  readData(server.arg("plain"), true);

  String reply = "{\"id\":\"" + WiFi.macAddress()
  + "\",\"version\":" + version
  + ",\"value\":" + toPercentages(destination)
  + ",\"twilight\":\"" + twilight + "," + light
  + "\",\"sunset\":" + (sunset > 0 ? sunset : 0)
  + ",\"sunrise\":" + (sunrise > 0 ? sunrise : 0)
  + ",\"smart\":\"" + smartString
  + "\",\"steps\":" + steps
  + ",\"boundary\":" + boundary
  + ",\"rtc\":" + RTC.isrunning()
  + ",\"active\":" + (RTC.isrunning() ? RTC.now().unixtime() - offset - start : 0)
  + ",\"uprisings\":" + uprisings
  + ",\"offline\":" + offline
  + ",\"reversed\":" + reversed + "}";

  note("Shake hands");
  server.send(200, "text/plain", reply);
}

void requestForState() {
  String reply = "{\"state\":\"" + toPercentages(destination) + ","
  + light
  + (twilight ? "t" : "")
  + (!measurement && destination != actual ? "^" + toPercentages(actual) : "")
  + (measurement ?  "=" + String(actual) : "")
  + "\"}";

  server.send(200, "text/plain", reply);
}

void requestForBasicData() {
  String reply = "{\"time\":" + String(RTC.now().unixtime() - offset);
  if (light > -1) {
    reply += ",";
    reply += "\"twilight\":" + String(twilight);
  }

  server.send(200, "text/plain", reply + "}");
}

void reverseDirection() {
  reversed = !reversed;
  saveSettings();
  note("Engine set in the " + String(reversed ? "opposite" : "right") +  " direction");
  server.send(200, "text/plain", "Done");
  sayHelloToTheServer();
}

void resetCoverage() {
  destination = 0;
  actual = 0;
  saveSettings();
  server.send(200, "text/plain", "Done");
  putDataOnline("detail", "val=0");
}

void makeMeasurement() {
  measurement = !measurement;
  server.send(200, "text/plain", "{\"steps\":" + String(actual) + "}");

  if (measurement) {
    actual = 0;
  } else {
    note("\nMeasurement result: " + String(actual) + " steps");
    steps = actual;
    destination = actual;
    setStepperPins(false);
    saveSettings();
    sayHelloToTheServer();
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (ssid != "" && password != "") {
      note("Reconnection with Wi-Fi");

      if (!connectingToWifi()) {
        initiatingWPS();
      }
    } else {
      initiatingWPS();
    }

    if (measurement) {
      return;
    }
  }

  server.handleClient();
  MDNS.update();

  if (measurement) {
    actual++;
    rotation();
    delay(1);
    return;
  }

  if (calibration != 0) {
    if (calibration < 0) {
      calibration++;
    } else {
      calibration--;
    }
    rotation();
    delay(1);
    if (calibration == 0) {
      setStepperPins(false);
    }
    return;
  }

  if (timeHasChanged()) {
    if (loopTime % 2 == 0) {
      getOnlineData();
    }
    if (destination != actual) {
      saveTheState();
    }
    checkSmart(lightHasChanged());
  }

  if (destination != actual) {
    if (destination > actual) {
      actual++;
    } else {
      actual--;
    }
    rotation();
    delay(1);

    if (destination == actual) {
      Serial.printf("\nRoller blind reached the target position of %s%s", toPercentages(destination).c_str(), "%");
      setStepperPins(false);
      if (SPIFFS.exists("/resume.txt")) {
        SPIFFS.remove("/resume.txt");
      }
    }
  }
}


bool lightHasChanged() {
  if (destination != actual || light == -1) {
    return false;
  }

  DateTime now = RTC.now();
  bool result = false;

  if (abs((RTC.isrunning() ? now.unixtime() : millis() / 1000) - twilightLoopTime) >= 300) {
    twilightLoopTime = RTC.isrunning() ? now.unixtime() : millis() / 1000;
    int newLight = analogRead(light_sensor_pin);

    if (abs(light - newLight) >= 20) {
      light = newLight;
      bool sent = false;

      if (twilight != (light < boundary)) {
        twilight = light < boundary;

        if (RTC.isrunning() && ((RTC.now().unixtime() - offset) - start) > 60
        && (now.unixtime() - offset - sunset) > 3600 && (now.unixtime() - offset - sunrise) > 3600) {
          if (twilight) {
            if (sunset <= sunrise) {
              sunset = now.unixtime() - offset;
              putDataOnline("detail", "set=" + String(sunset) + "&tw=" + twilight + "," + light);
              saveSettings();
              sent = true;
            }
          } else {
            if (sunrise <= sunset) {
              sunrise = now.unixtime() - offset;
              putDataOnline("detail", "rise=" + String(sunrise) + "&tw=" + twilight + "," + light);
              saveSettings();
              sent = true;
            }
          }
        }

        result = true;
        putOfflineData("{\"twilight\":" + String(twilight) + "}");
      }

      if (!sent) {
        putDataOnline("detail", "tw=" + String(twilight) + "," + light);
      }
    }
  }

  return result;
}

void readData(String payload, bool perWiFi) {
  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(payload);
  DateTime now = RTC.now();
  uint32_t newTime = 0;
  bool settingsChange = false;
  String result = "";

  if (!jsonObject.success()) {
    if (payload.length() > 0) {
      Serial.print("\n Parsing failed!");
    }
    return;
  }

  if (jsonObject.containsKey("calibrate")) {
    setCoverage(jsonObject["calibrate"].as<int>(), true);
    return;
  }

  if (jsonObject.containsKey("offset")) {
    int newOffset = jsonObject["offset"].as<int>();
    if (offset != newOffset) {
      if (RTC.isrunning()) {
        newTime = now.unixtime() - offset;
      }
      offset = newOffset;

      if (!jsonObject.containsKey("time") && RTC.isrunning()) {
        newTime = newTime + offset;
        if (abs(newTime - now.unixtime()) > 10) {
          RTC.adjust(DateTime(newTime));
          note("Adjust time");
        }
      }

      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("time")) {
    newTime = jsonObject["time"].as<uint32_t>() + offset;
    if (RTC.isrunning()) {
      if (abs(newTime - now.unixtime()) > 10) {
        RTC.adjust(DateTime(newTime));
        note("Adjust time");
      }
    } else {
      RTC.adjust(DateTime(newTime));
      start = RTC.now().unixtime() - offset;
      note("Adjust time");
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
      result = "smart=" + newSmartString;
      setSmart();
      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("val")) {
    int newDestination = toSteps(jsonObject["val"].as<int>());
    if (destination != newDestination) {
      result += result.length() > 0 ? "&" : "";
      result += "val=" + toPercentages(newDestination);
      setCoverage(newDestination, false);
    }
  }

  if (jsonObject.containsKey("steps")) {
    int newSteps = jsonObject["steps"].as<int>();
    if (steps != newSteps) {
      steps = newSteps;
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
    note("Received basic data: " + payload);
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
    putDataOnline("detail", result);
  }
}

void setSmart() {
  if (smartString.length() < 2) {
    smartCount = 0;
    return;
  }

  String smart;
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
      loweringTime = strContains(smart, "_") ? smart.substring(0, smart.indexOf("_")).toInt() : -1;
      liftingTime = strContains(smart, "-") ? smart.substring(smart.indexOf("-") + 1, smart.length()).toInt() : -1;

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

      enabled = !strContains(smart, "/");

      smartArray[i] = (Smart) {days, loweringAtNight, liftingAtDay, loweringTime, liftingTime, enabled, 0};
    }
  }
}

void checkSmart(bool lightHasChanged) {
  if (!RTC.isrunning() || destination != actual) {
    return;
  }

  bool result = false;
  int newDestination = 0;
  DateTime now = RTC.now();
  int currentTime = (now.hour() * 60) + now.minute();

  int i = -1;
  while (++i < smartCount && !result) {
    if (smartArray[i].enabled) {
      if (lightHasChanged) {
        if (smartArray[i].loweringAtNight && twilight && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) && destination != steps) {
          newDestination = steps;
          result = true;
          note("The smart function has activated lowering the roller blind for the night");
        }
        if (smartArray[i].liftingAtDay && !twilight && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() - 1]) && destination != 0) {
          newDestination = 0;
          result = true;
          note("The smart function has activated lifting the roller blind for the day");
        }
      } else {
        if (smartArray[i].access + 60 < now.unixtime() && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])) {
          if (smartArray[i].loweringTime == currentTime) {
            smartArray[i].access = now.unixtime();
            if (destination != steps) {
              newDestination = steps;
              result = true;
              note("The smart function has activated lowering the roller blind on time");
            }
          }
          if (smartArray[i].liftingTime == currentTime) {
            smartArray[i].access = now.unixtime();
            if (destination != 0) {
              newDestination = 0;
              result = true;
              note("The smart function has activated lifting the roller blind on time");
            }
          }
        }
      }
    }
  }

  if (result) {
    putDataOnline("detail", "val=" + toPercentages(newDestination));
    setCoverage(newDestination, false);
  }
}


void setCoverage(int set, bool calibrate) {
  if (calibrate) {
    if (destination == actual) {
      if (actual == steps) {
        steps += set;
        destination += set;
      } else {
        calibration = set;
        note("Calibration. Roller blind movement by " + String(set) + " steps");
      }
    }
  } else {
    destination = set;
    note("Roller blinds movement by " + String(destination - actual) + " steps. Change of state to " + toPercentages(destination) + "%");
    saveSettings();
    saveTheState();
  }
}

void rotation() {
  for (int x = 0; x < 1; x++) {
    switch (step) {
      case 0:
        digitalWrite(stepper_pin[0], LOW);
        digitalWrite(stepper_pin[1], LOW);
        digitalWrite(stepper_pin[2], LOW);
        digitalWrite(stepper_pin[3], HIGH);
        break;
      case 1:
        digitalWrite(stepper_pin[0], LOW);
        digitalWrite(stepper_pin[1], LOW);
        digitalWrite(stepper_pin[2], HIGH);
        digitalWrite(stepper_pin[3], HIGH);
        break;
      case 2:
        digitalWrite(stepper_pin[0], LOW);
        digitalWrite(stepper_pin[1], LOW);
        digitalWrite(stepper_pin[2], HIGH);
        digitalWrite(stepper_pin[3], LOW);
        break;
      case 3:
        digitalWrite(stepper_pin[0], LOW);
        digitalWrite(stepper_pin[1], HIGH);
        digitalWrite(stepper_pin[2], HIGH);
        digitalWrite(stepper_pin[3], LOW);
        break;
      case 4:
        digitalWrite(stepper_pin[0], LOW);
        digitalWrite(stepper_pin[1], HIGH);
        digitalWrite(stepper_pin[2], LOW);
        digitalWrite(stepper_pin[3], LOW);
        break;
      case 5:
        digitalWrite(stepper_pin[0], HIGH);
        digitalWrite(stepper_pin[1], HIGH);
        digitalWrite(stepper_pin[2], LOW);
        digitalWrite(stepper_pin[3], LOW);
        break;
      case 6:
        digitalWrite(stepper_pin[0], HIGH);
        digitalWrite(stepper_pin[1], LOW);
        digitalWrite(stepper_pin[2], LOW);
        digitalWrite(stepper_pin[3], LOW);
        break;
      case 7:
        digitalWrite(stepper_pin[0], HIGH);
        digitalWrite(stepper_pin[1], LOW);
        digitalWrite(stepper_pin[2], LOW);
        digitalWrite(stepper_pin[3], HIGH);
        break;
      default:
        digitalWrite(stepper_pin[0], LOW);
        digitalWrite(stepper_pin[1], LOW);
        digitalWrite(stepper_pin[2], LOW);
        digitalWrite(stepper_pin[3], LOW);
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
        if (destination < actual) {
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
