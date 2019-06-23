#include "core.h"

void setup() {
  Serial.begin(74880);
  while (!Serial) {}

  SPIFFS.begin();
  Wire.begin();

  note("iDom Blinds " + String(bipolar ? "st" : "nx"));
  Serial.print("\n Roller blind ID: " + WiFi.macAddress());
  offline = !SPIFFS.exists("/online.txt");
  Serial.printf("\n The roller blind is set to %s mode", offline ? "OFFLINE" : "ONLINE");

  sprintf(hostName, "blinds_%s", String(WiFi.macAddress()).c_str());
  WiFi.hostname(hostName);

  if (!readSettings(false)) {
    readSettings(true);
  }
  resume();

  RTC.begin();
  if (RTC.isrunning()) {
    start = RTC.now().unixtime() - offset;
  }
  Serial.printf("\n RTC initialization %s", start != 0 ? "completed" : "failed!");

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
      pinMode(bipolar_pin[0], OUTPUT);
      pinMode(bipolar_pin[1], OUTPUT);
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

String toPercentages(int value) {
  return String(value > 0 ? value * 100 / steps : 0);
}

int toSteps(int value) {
  return value > 0 ? value * steps / 100 : 0;
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
  note("The " + String(backup ? "backup" : "settings") + " settings file has been read:\n " + logs);

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
  jsonObject["steps"] = steps;
  jsonObject["boundary"] = boundary;
  jsonObject["destination"] = destination;
  jsonObject["reversed"] = reversed;

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
  server.on("/measurement", HTTP_POST, makeMeasurement);
  server.on("/cancelmeasurement", HTTP_POST, cancelMeasurement);
  server.on("/endmeasurement", HTTP_POST, endMeasurement);
  server.on("/admin/reset", HTTP_POST, setMin);
  server.on("/admin/setmax", HTTP_POST, setMax);
  server.on("/admin/onlineswitch", HTTP_POST, onlineswitch);
  server.on("/admin/delete/wifisettings", HTTP_DELETE, deleteWiFiSettings);
  server.begin();

  note("Launch of services. MDNS responder " + String(hostName) + (MDNS.begin(hostName) ? " started." : " unsuccessful!"));

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
  + ",\"active\":" + (start != 0 ? RTC.now().unixtime() - offset - start : 0)
  + ",\"uprisings\":" + uprisings
  + ",\"offline\":" + offline
  + ",\"wings\":" + wings
  + ",\"reversed\":" + reversed + "}";

  Serial.print("\nShake hands");
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
  server.send(200, "text/plain", "Done");
  sayHelloToTheServer();
  note("Engine set in the " + String(reversed ? "opposite" : "right") +  " direction");
}

void setMin() {
  destination = 0;
  actual = 0;
  saveSettings();
  server.send(200, "text/plain", "Done");
  putDataOnline("detail", "val=0");
}

void setMax() {
  destination = steps;
  actual = steps;
  saveSettings();
  server.send(200, "text/plain", "Done");
  putDataOnline("detail", "val=100");
}

void makeMeasurement() {
  if (destination != 0 || actual != destination) {
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
  digitalWrite(bipolar_direction_pin, destination < actual ? reversed : !reversed);
  server.send(200, "text/plain", "Done");
}

void endMeasurement() {
  if (!measurement) {
    return;
  }

  measurement = false;
  server.send(200, "text/plain", "{\"steps\":" + String(actual) + "}");

  steps = actual;
  destination = actual;
  setStepperPins(false);
  saveSettings();
  sayHelloToTheServer();
  note("\nMeasurement result: " + String(actual) + " steps");
}

void loop() {
  if (!WiFi.isConnected() && measurement) {
    measurement = false;
    return;
  }

  server.handleClient();
  MDNS.update();

  if (measurement) {
    actual++;
    rotation();
    return;
  }

  if (calibration != 0) {
    if (calibration < 0) {
      calibration++;
    } else {
      calibration--;
    }
    rotation();
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
    checkSmart(lightHasChanged());
  }

  if (destination != actual) {
    if (destination > actual) {
      actual++;
    } else {
      actual--;
    }
    rotation();
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
            putDataOnline("detail", "set=" + String(sunset) + "&tw=" + String(twilight) + "," + light);
            saveSettings();
            sent = true;
          }
        } else {
          if (sunrise <= sunset) {
            sunrise = RTC.now().unixtime() - offset;
            putDataOnline("detail", "rise=" + String(sunrise) + "&tw=" + String(twilight) + "," + light);
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
    putDataOnline("detail", "tw=" + String(twilight) + "," + light);
  }

  return result;
}

void readData(String payload, bool perWiFi) {
  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(payload);

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
    if (bipolar && jsonObject.containsKey("wings")) {
      calibration_wings = jsonObject["wings"].as<int>();
    } else {
      calibration_wings = 0;
    }
    setCoverage(jsonObject["calibrate"].as<int>(), true);
    return;
  }

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
        start = RTC.now().unixtime() - offset;
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
    int newDestination = toSteps(jsonObject["val"].as<int>());
    if (destination != newDestination) {
      setCoverage(newDestination, false);
      result += result.length() > 0 ? "&" : "";
      result += "val=" + toPercentages(newDestination);
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
    if (twilight != newTwilight) {
      twilight = newTwilight;
      note("Received basic data: " + payload);
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

void checkSmart(bool lightChanged) {
  if (!RTC.isrunning()) {
    return;
  }

  bool result = false;
  int newDestination = 0;
  DateTime now = RTC.now();
  int currentTime = (now.hour() * 60) + now.minute();
  String log = "The smart function has";

  int i = -1;
  while (++i < smartCount && !result) {
    if (smartArray[i].enabled) {
      if (lightChanged) {
        if (twilight && smartArray[i].loweringAtNight && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) && destination != steps) {
          newDestination = steps;
          result = true;
          log += " activated lowering the roller blind for the night";
        }
        if (!twilight && smartArray[i].liftingAtDay && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() != 0 ? now.dayOfTheWeek() - 1 : now.dayOfTheWeek() + 6]) && destination != 0) {
          newDestination = 0;
          result = true;
          log += " activated lifting the roller blind for the day";
        }
      } else {
        if (smartArray[i].access + 60 < now.unixtime()) {
          if (smartArray[i].loweringTime == currentTime && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])) {
            smartArray[i].access = now.unixtime();
            if (destination != steps) {
              newDestination = steps;
              result = true;
              log += " activated lowering the roller blind on time";
            }
          }
          if (smartArray[i].liftingTime == currentTime && (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) || strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() != 0 ? now.dayOfTheWeek() - 1 : now.dayOfTheWeek() + 6]))) {
            smartArray[i].access = now.unixtime();
            if (destination != 0) {
              newDestination = 0;
              result = true;
              log += " activated lifting the roller blind on time";
            }
          }
        }
      }
    }
  }

  if (result) {
    note(log);
    putDataOnline("detail", "val=" + toPercentages(newDestination));
    setCoverage(newDestination, false);
  } else {
    if (lightChanged) {
      note(log + "n't activated anything.");
    }
  }
}


void setCoverage(int set, bool calibrate) {
  if (calibrate) {
    if (destination != actual) {
      return;
    }

    if (actual != 0 && actual == steps) {
      steps += bipolar ? set / 2 : set;
      destination += bipolar ? set / 2 : set;
      saveSettings();
      note("Calibration. Roller blind movement by " + String(set) + " steps. Steps set at " + String(steps) + ".");
    }
    if (actual == 0) {
      calibration = bipolar ? set / 2 : set;
      note("Calibration" + String(calibration_wings == 2 ? " second wings" : calibration_wings == 1 ? " primary wings" : "") + ". Roller blind movement by " + String(set) + " steps.");
    }
    if (bipolar) {
      digitalWrite(bipolar_enable_pin, LOW);
      digitalWrite(bipolar_direction_pin, set < 0 ? reversed : !reversed);
    }
  } else {
    destination = set;
    if (bipolar) {
      digitalWrite(bipolar_enable_pin, LOW);
      digitalWrite(bipolar_direction_pin, destination < actual ? reversed : !reversed);
    }
    note("Roller blinds movement by " + String(destination - actual) + " steps. Change of state to " + toPercentages(destination) + "%.");
    saveSettings();
    saveTheState();
  }
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
  digitalWrite(bipolar_pin[0], (calibration == 0 || strContains(String(calibration_wings), "1")) ? HIGH : LOW);
  digitalWrite(bipolar_pin[0], LOW);
  digitalWrite(bipolar_pin[1], (calibration == 0 || strContains(String(calibration_wings), "2")) ? HIGH : LOW);
  digitalWrite(bipolar_pin[1], LOW);
}

void unipolarRotation() {
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
