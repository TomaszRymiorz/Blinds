#include "core.h"

void setup() {
  Serial.begin(74880);
  while (!Serial) {}

  SPIFFS.begin();
  Wire.begin();

  writeLog("iDom Blinds nx");
  Serial.print("\n Blinds ID: " + WiFi.macAddress());
  Serial.printf("\n The blinds is set to %s mode", offline ? "OFFLINE" : "ONLINE");
  Serial.print("\n RTC initialization " + String(RTC.begin() ? "completed" : "failed!"));

  readSettings();
  resume();

  if (RTC.isrunning()) {
    start = RTC.now().unixtime() - offset;
  }

  light = analogRead(light_sensor_pin);
  twilight = light < 100;
  twilightLoopTime = RTC.isrunning() ? RTC.now().unixtime() : millis() / 1000;

  setupStepperPins(true);

  if (ssid != "" && password != "") {
    connectingToWifi();
  } else {
    initiatingWPS();
  }
}

void setupStepperPins(bool mode) {
  for (int i = 0; i < 4; i++) {
    if (mode) {
      pinMode(stepper_pin[i], OUTPUT);
    }
    digitalWrite(stepper_pin[i], LOW);
  }
}


void readSettings() {
  File file = SPIFFS.open("/settings.txt", "r");
  if (!file) {
    writeLog("The settings file can not be read");
    return;
  }

  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(file.readString());
  file.close();

  if (!jsonObject.success()) {
    writeLog("Settings file error");
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
    steps = jsonObject["boundary"].as<int>();
  }
  if (jsonObject.containsKey("reversed")) {
    reversed = jsonObject["reversed"].as<bool>();
  }
  if (jsonObject.containsKey("coverage")) {
    coverage = jsonObject["coverage"].as<int>();
  }

  String logs;
  jsonObject.printTo(logs);
  writeLog("The settings file was read:\n " + logs);

  saveTheSettings();
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
    if (jsonObject.containsKey("destination")) {
      destination = jsonObject["destination"].as<float>();

      if (destination > 0) {
        if (jsonObject.containsKey("actual")) {
          actual = jsonObject["actual"].as<int>();
        }

        String logs = "Blinds will be moved by " + String((int)destination - actual)
        + " steps to state " + coverage
        + "%";
        writeLog(logs);
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

  jsonObject["destination"] = destination;
  jsonObject["actual"] = actual;

  writeObjectToFile("resume", jsonObject);
}

void saveTheSettings() {
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
  jsonObject["boundary"] = dayBoundary;
  jsonObject["coverage"] = coverage;
  jsonObject["reversed"] = reversed;

  if (writeObjectToFile("settings", jsonObject)) {
    String logs;
    jsonObject.printTo(logs);
    writeLog("Saving settings:\n " + logs);
  } else {
    writeLog("Saving settings failed!");
  }
}


void sayHelloToTheServer() {
  if (!offline) {
    String request = "ip=" + WiFi.localIP().toString()
    + "&deets=" + steps + "," + RTC.isrunning() + "," + start + "," + reversed + "," + uprisings + "," + version + "," + dayBoundary
    + "&tw=" + String(twilight) + "," + light;

    if (sendingError) {
      request += "&val=" + String(coverage)
      + "&set=" + (sunset > 0 ? sunset : 0)
      + "&rise=" + (sunrise > 0 ? sunrise : 0);

      putDataOnline("detail", request);
    } else {
      putDataOnline("rooms", request);
    }
  }
}

void startRestServer() {
  server.on("/hello", HTTP_POST, handshake);
  server.on("/set", HTTP_PUT, receivedTheData);
  server.on("/state", HTTP_GET, requestForState);
  server.on("/log", HTTP_GET, requestForLogs);
  server.on("/log", HTTP_DELETE, clearLogs);
  server.on("/reversed", HTTP_POST, reverseDirection);
  server.on("/reset", HTTP_POST, resetCoverage);
  server.on("/deletewifisettings", HTTP_DELETE, deleteWiFiSettings);
  server.on("/measurement", HTTP_POST, makeMeasurement);
  server.begin();
  Serial.print("\n Starting the REST server");
}

void handshake() {
  readData(server.arg("plain"), true);

  String reply = "{\"id\":\"" + WiFi.macAddress()
  + "\",\"version\":" + version
  + ",\"value\":" + coverage
  + ",\"twilight\":\"" + twilight + "," + light
  + "\",\"sunset\":" + (sunset > 0 ? sunset : 0)
  + ",\"sunrise\":" + (sunrise > 0 ? sunrise : 0)
  + ",\"smart\":\"" + smartString
  + "\",\"steps\":" + steps
  + ",\"boundary\":" + dayBoundary
  + ",\"rtc\":" + RTC.isrunning()
  + ",\"active\":" + (RTC.isrunning() ? (RTC.now().unixtime() - offset) - start : 0)
  + ",\"uprisings\":" + uprisings
  + ",\"offline\":" + offline
  + ",\"reversed\":" + reversed + "}";

  writeLog("Shake hands");
  server.send(200, "text/plain", reply);
}

void requestForState() {
  String reply = "{\"state\":\"" + String(coverage) + ","
  + light
  + (twilight ? "t" : "")
  + (measure > 0 ?  "-" + String(measure) : "") + "\"}";

  server.send(200, "text/plain", reply);
}

void reverseDirection() {
  reversed = !reversed;
  saveTheSettings();
  writeLog("Engine set in the " + String(reversed ? "opposite" : "right") +  " direction");
  server.send(200, "text/plain", "Done");
  sayHelloToTheServer();
}

void resetCoverage() {
  coverage = 0;
  saveTheSettings();
  server.send(200, "text/plain", "Done");
  putDataOnline("detail", "val=0");
}

void makeMeasurement() {
  measurement = !measurement;
  server.send(200, "text/plain", "Done");

  if (!measurement && measure > 0) {
    writeLog("\nMeasurement result: " + String(measure) + " steps");
    steps = measure;
    measure = 0;
    coverage = 100;
    setupStepperPins(false);
    saveTheSettings();
    sayHelloToTheServer();
  }
}


void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (ssid != "" && password != "") {
      writeLog("Reconnection with Wi-Fi");

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

  if (measurement) {
    measure++;
    rotation();
    delay(1);
    return;
  }

  if (timeHasChanged()) {
    if (loopTime % 2 == 0) {
      getOnlineData();
    }
    if (destination != 0) {
      saveTheState();
    }
    checkSmart(lightHasChanged());
  }

  if (destination != 0) {
    if (actual < abs(destination)) {
      actual++;
      rotation();
      delay(1);
    } else {
      Serial.printf("\nBlinds has reached the target position %i%s", coverage, "%");
      destination = 0;
      setupStepperPins(false);
      if (SPIFFS.exists("/resume.txt")) {
        SPIFFS.remove("/resume.txt");
      }
    }
  }
}


bool lightHasChanged() {
  if (destination != 0) {
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

      if (light > 4 && twilight != (light < dayBoundary)) {
        twilight = light < dayBoundary;

        if (RTC.isrunning() && ((RTC.now().unixtime() - offset) - start) > 60
        && (now.unixtime() - offset - sunset) > 3600 && (now.unixtime() - offset - sunrise) > 3600) {
          if (twilight) {
            if (sunset <= sunrise) {
              sunset = now.unixtime() - offset;
              putDataOnline("detail", "set=" + String(sunset) + "&tw=" + twilight + "," + light);
              saveTheSettings();
              sent = true;
            }
          } else {
            if (sunrise <= sunset) {
              sunrise = now.unixtime() - offset;
              putDataOnline("detail", "rise=" + String(sunrise) + "&tw=" + twilight + "," + light);
              saveTheSettings();
              sent = true;
            }
          }
        }

        result = true;
        postDataToTheTwin("{\"twilight\":" + String(twilight) + "}");
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
      newTime = now.unixtime() - offset;
      offset = newOffset;

      if (!jsonObject.containsKey("time") && RTC.isrunning()) {
        newTime = newTime + offset;
        if (abs(newTime - now.unixtime()) > 10) {
          RTC.adjust(DateTime(newTime));
          writeLog("Adjust time");
        }
      }

      settingsChange = true;
    }
  }

  if (jsonObject.containsKey("time") && RTC.isrunning()) {
    newTime = jsonObject["time"].as<uint32_t>() + offset;
    if (abs(newTime - now.unixtime()) > 10) {
      RTC.adjust(DateTime(newTime));
      writeLog("Adjust time");
    }
  }

  if (jsonObject.containsKey("up")) {
    uint32_t newUpdateTime = jsonObject["up"].as<uint32_t>();
    if (updateTime < newUpdateTime) {
      updateTime = newUpdateTime;
    }
  }

  if (jsonObject.containsKey("twin")) {
    String newTwin = jsonObject["twin"].as<String>();
    if (twin != newTwin) {
      twin = newTwin;
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
    int newCoverage = jsonObject["val"].as<int>();
    if (coverage != newCoverage) {
      result += result.length() > 0 ? "&" : "";
      result += "val=" + String(newCoverage);
      setCoverage(newCoverage, false);
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
    if (dayBoundary != newBoundary) {
      dayBoundary = newBoundary;
      settingsChange = true;
    }
  }

  if (settingsChange) {
    writeLog("Received the data:\n " + payload);
    saveTheSettings();
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
  if (!RTC.isrunning() || destination != 0) {
    return;
  }

  bool result = false;
  int newCoverage = 0;
  DateTime now = RTC.now();
  int currentTime = (now.hour() * 60) + now.minute();

  int i = -1;
  while (++i < smartCount && !result) {
    if (smartArray[i].enabled) {
      if (lightHasChanged) {
        if (smartArray[i].loweringAtNight && twilight && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) && coverage != 100) {
          newCoverage = 100;
          result = true;
          writeLog("The smart function activated lowering the blinds at night");
        }
        if (smartArray[i].liftingAtDay && !twilight && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() - 1]) && coverage != 0) {
          newCoverage = 0;
          result = true;
          writeLog("The smart function activated lifting the blinds at day");
        }
      } else {
        if (smartArray[i].access + 60 < now.unixtime() && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])) {
          if (smartArray[i].loweringTime == currentTime) {
            smartArray[i].access = now.unixtime();
            if (coverage != 100) {
              newCoverage = 100;
              result = true;
              writeLog("The smart function activated lowering the blinds on time");
            }
          }
          if (smartArray[i].liftingTime == currentTime) {
            smartArray[i].access = now.unixtime();
            if (coverage != 0) {
              newCoverage = 0;
              result = true;
              writeLog("The smart function activated lifting the blinds on time");
            }
          }
        }
      }
    }
  }

  if (result) {
    putDataOnline("detail", "val=" + String(newCoverage));
    setCoverage(newCoverage, false);
  }
}


void setCoverage(int set, bool calibrate) {
  if (destination == 0) {
    if (calibrate) {
      destination = set;

      writeLog("Calibration. Blinds movement by " + String((int)destination) + " steps");
    } else {
      destination = ((float)steps / 100.0) * (float)(set - coverage);
      coverage = set;

      writeLog("Blinds movement by " + String((int)destination) + " steps. Changed state to " + set + "%");
      saveTheSettings();
      saveTheState();
    }
    actual = 0;
  } else {
    if (!calibrate) {
      float actualPosition = abs(destination) - actual;
      if (destination < 0) {
        actualPosition *= -1;
      }
      destination = (((float)steps / 100.0) * (float)(set - coverage)) + actualPosition;
      coverage = set;

      writeLog("Blinds moved by " + String(actual) + " steps, now movement by " + String((int)destination) + " steps. Changed state to " + set + "%");
      actual = 0;

      saveTheSettings();
      saveTheState();
    }
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

    if (destination > 0) {
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
    if (step > 7) {
      step = 0;
    }
    if (step < 0) {
      step = 7;
    }
  }
}
