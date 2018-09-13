#include "core.h"

void setup() {
  Serial.begin(74880);
  while (!Serial) {}
  delay(3000);

  Serial.print("\niDom Blinds 1");
  Serial.printf("\nBlinds ID: %s", WiFi.macAddress().c_str());

  Serial.print("\n SD card initialization ");
  if (!SD.begin(sd_pin)) {
    Serial.print("failed!");
    while (true) {
      delay(0);
    };
  }
  Serial.print("completed");

  Serial.print("\n RTC initialization ");
  Wire.begin();
  if (RTC.begin()) {
    Serial.print("completed");
  } else {
    Serial.print("failed");
  }

  if (!RTC.isrunning()) {
    Serial.print("\n RTC is NOT running!");
  }

  offline = !SD.exists("online.txt");
  Serial.printf("\n The blinds is set to %s mode", offline ? "OFFLINE" : "ONLINE");

  readOffset();
  readSteps();
  readCoverage();
  readSmart();
  readSunset();
  readSunrise();

  reversed = SD.exists("reversed.txt");
  Serial.printf("\n Engine set in the %s direction", reversed ? "opposite" : "right");

  if (readWiFiConfiguration()) {
    connectingToWifi();
  } else {
    initiatingWPS();
  }

  uprisingsCounter();

  setupStepperPins();

  if (RTC.isrunning()) {
    start = RTC.now().unixtime();
  }
}

void setupStepperPins() {
  for (int i = 0; i < 3; i++) {
    pinMode(multiplexer_pin[i], OUTPUT);
    digitalWrite(multiplexer_pin[i], LOW);
  }

  pinMode(stepper_pin, OUTPUT);
  digitalWrite(stepper_pin, LOW);
}

void readSteps() {
  String s = readFromSD("steps");
  if (s != "-1") {
    steps = s.toInt();
    Serial.printf("\n Steps: %i", steps);
  }
}

void readCoverage() {
  String s = readFromSD("coverage");
  if (s != "-1") {
    coverage = s.toInt();
    Serial.printf("\n Coverage: %i", coverage);
    Serial.print('%');
  }
}

void readSunset() {
  String s = readFromSD("sunset");
  if (s != "-1") {
    sunset = s.toInt();
  }
}

void readSunrise() {
  String s = readFromSD("sunrise");
  if (s != "-1") {
    sunrise = s.toInt();
  }
}


void writeSteps() {
  writeOnSD("steps", String(steps), "", "// Wysokość okna wyrażona w ilości kroków silnika.");
  setupStepperPins();
}

void writeCoverage() {
  writeOnSD("coverage", String(coverage), "", "// Aktualna pozycja rolety.");
  setupStepperPins();
}


void startRestServer() {
  server.on("/hello", HTTP_POST, handshake);
  server.on("/set", HTTP_PUT, receivedTheData);
  server.on("/state", HTTP_GET, requestForState);
  server.on("/reversed", HTTP_POST, reverseDirection);
  server.on("/reset", HTTP_POST, resetCoverage);
  server.on("/measurement", HTTP_POST, makeMeasurement);
  server.begin();
  Serial.print("\nStarting the REST server");
}

void handshake() {
  String reply;
  if (readData(server.arg("plain")) == -1) {
    serialPrint("Shake hands");

    uint32_t active = 0;
    if (RTC.isrunning()) {
      active = RTC.now().unixtime() - start;
    }

    reply = "{\"id\":\"" +  WiFi.macAddress()
    + "\",\"coverage\":" + coverage
    + ",\"twilight\":" + twilight
    + ",\"sensor\":" + daylight
    + ",\"sunset\":" + (sunset > 0 ? (sunset - offset) : 0)
    + ",\"sunrise\":" + (sunrise > 0 ? (sunrise - offset) : 0)
    + ",\"smart\":\"" + smartString
    + "\",\"steps\":" + steps
    + ",\"rtc\":" + RTC.isrunning()
    + ",\"active\":" + active
    + ",\"uprisings\":" + uprisings
    + ",\"reversed\":" + reversed + "}";
  } else {
    reply = "0";
  }

  server.send(200, "text/plain", reply);
}

void requestForState() {
  String reply = "{\"state\":\"" + String(coverage)
  + (twilight ? "t" : "")
  + (measure > 0 ?  ("-" + String(measure)) : "") + "\"}";

  server.send(200, "text/plain", reply);
}

void receivedTheData() {
  Serial.print("\n Received the data");
  if (!server.hasArg("plain")) {
    server.send(200, "text/plain", "Body not received");
    return;
  }
  server.send(200, "text/plain", "Data has received");

  readData(server.arg("plain"));
}

void reverseDirection() {
  reversed = !reversed;

  SPI.begin();
  if (!reversed && SD.exists("reversed.txt")) {
    SD.remove("reversed.txt");
  }
  if (reversed) {
    writeOnSD("reversed", "", "", "// Obecność tego pliku wskazuje na odwrotny kierunek obrotów silnika.");
  }
  SPI.end();
  setupStepperPins();
  Serial.printf("\nEngine set in the %s direction", reversed ? "opposite" : "right");
  server.send(200, "text/plain", "Done");
}

void resetCoverage() {
  coverage = 0;
  writeCoverage();
  server.send(200, "text/plain", "Done");
}

void makeMeasurement() {
  measurement = !measurement;
  server.send(200, "text/plain", "Done");
}


void loop() {
  server.handleClient();

  if (destination != 0) {
    if (actual < abs(destination)) {
      actual++;
      if (destination > 0) {
        if (reversed) {
          uncover(lag);
        } else {
          cover(lag);
        }
      } else {
        if (reversed) {
          cover(lag + 1);
        } else {
          uncover(lag + 1);
        }
      }
    } else {
      destination = 0;
      calibration = 0;
    }
    return;
  }

  if (daylightHasChanged()) {
    checkSmart(true);
    if (offline) {
      postToTwin("{\"twilight\":" + String(twilight) + "}");
    } else {
      putDataOnServer("twilight=" + String(twilight));
    }
  }

  if (timeHasChanged()) {
    if (!offline) {
      getOnlineData();
    }

    checkSmart(false);
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (reconnect) {
      serialPrint("Reconnection with Wi-Fi");
      if (!connectingToWifi()) {
        initiatingWPS();
      }
    } else {
      if (initiatingWPS()) {
        setupStepperPins();
      }
    }
    return;
  }

  if (measurement) {
    measure++;
    if (reversed) {
      uncover(lag);
    } else {
      cover(lag);
    }
    return;
  }

  if (!measurement && measure > 0) {
    Serial.printf("\nMeasurement result: %i steps", measure);
    steps = measure;
    writeSteps();
    measure = 0;
    coverage = 100;
    writeCoverage();
    return;
  }
}

bool daylightHasChanged() {
  if (!RTC.isrunning()) {
    return false;
  }

  DateTime now = RTC.now();
  daylight = analogRead(twilight_pin);
  delay(3);

  if (twilight != (daylight < 100) && (daylight < 90 || daylight > 110)) {
    if (twilightLoopTime == 0) {
      twilightLoopTime = now.unixtime();
    } else {
      if (twilightLoopTime + 60 < now.unixtime()) {
        twilight = daylight < 100;

        if (twilight) {
          sunset = now.unixtime();
          writeOnSD("sunset", String(sunset), "", "// Czas zachodzu słońca.");
        } else {
          sunrise = now.unixtime();
          writeOnSD("sunrise", String(sunrise), "", "// Czas wschodu słońca.");
        }

        twilightLoopTime = 0;
        return true;
      }
    }
  } else {
    twilightLoopTime = 0;
  }

  return false;
}

int readData(String payload) {
  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(payload);

  if (!jsonObject.success()) {
    if (payload.length() > 0) {
      Serial.print("\n " + payload);
      Serial.print("\n Parsing failed!");
    }
    return 0;
  }

  if (jsonObject.containsKey("offset")) {
    if (offset != jsonObject["offset"].as<int>()) {
      offset = jsonObject["offset"].as<int>();
      writeOffset();
      setupStepperPins();
    }
  }

  if (jsonObject.containsKey("access") && RTC.isrunning()) {
    uint32_t t = jsonObject["access"].as<uint32_t>() + offset;
    DateTime now = RTC.now();
    if (t - now.unixtime() > 10) {
      RTC.adjust(DateTime(t));
      serialPrint(" Adjust time");
    }
  }

  if (jsonObject.containsKey("twin")) {
    twin = jsonObject["twin"].as<String>();
  }

  if (jsonObject.containsKey("id") && jsonObject["id"].as<String>() == "idom") {
    return -1;
  }

  if (jsonObject.containsKey("smart")) {
    if (smartString != jsonObject["smart"].as<String>()) {
      smartString = jsonObject["smart"].as<String>();
      setSmart();
      writeSmart();
      setupStepperPins();
    }
  }

  if (jsonObject.containsKey("coverage")) {
    setCoverage(jsonObject["coverage"].as<int>(), false);
  }

  if (jsonObject.containsKey("calibrate")) {
    String calibrationData = jsonObject["calibrate"].as<String>();
    calibration = strContains(calibrationData, "s") ? 2 : (strContains(calibrationData, "p") ? 1 : 0);
    setCoverage(calibrationData.substring(1, calibrationData.length()).toInt(), true);
  }

  return jsonObject.success();
}

void setSmart() {
  if (smartString.length() == 0) {
    smartCount = 0;
    return;
  }

  String smart;
  String days;
  bool loweringAtNight;
  bool liftingAtDay;
  int loweringTime;
  int liftingTime;

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
    smart = get1Smart(smartString, i);
    if (smart.length() > 0 && strContains(smart, "b")) {
      loweringTime = strContains(smart, "_") ? smart.substring(0, smart.indexOf("_")).toInt() : -1;
      liftingTime = strContains(smart, "-") ? smart.substring(smart.indexOf("-") + 1, smart.length()).toInt() : -1;

      smart = strContains(smart, "_") ? smart.substring(smart.indexOf("_") + 1, smart.length()) : smart;
      smart = strContains(smart, "-") ? smart.substring(0, smart.indexOf("-")) : smart;

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

      smartArray[i] = (Smart) {days, loweringAtNight, liftingAtDay, 0, loweringTime, liftingTime, 0};
    }
  }
}

void checkSmart(bool daynight) {
  if (!RTC.isrunning()) {
    return;
  }

  DateTime now = RTC.now();
  int currentTime = (now.hour() * 60) + now.minute();
  int blinds = 0;
  bool result = false;

  for (int i = 0; i < smartCount; i++) {
    result = false;

    if (daynight) {
      if (smartArray[i].loweringAtNight && twilight
        && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])
        && (smartArray[i].blackout + 72000) < now.unixtime()) {
          blinds = 100;
          result = true;
          smartArray[i].blackout = now.unixtime();
          serialPrint("The smart function activated lowering the blinds at night");
        }
      if (smartArray[i].liftingAtDay && !twilight
        && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() - 1])) {
          blinds = 0;
          result = true;
          serialPrint("The smart function activated lifting the blinds at day");
        }
    } else {
      if (smartArray[i].access + 60 < now.unixtime()) {
        if (smartArray[i].loweringTime == currentTime
          && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])) {
            blinds = 100;
            result = true;
            smartArray[i].access = now.unixtime();
            serialPrint("The smart function activated the cover on time");
          }
        if (smartArray[i].liftingTime == currentTime
          && (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) || (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() - 1]) && currentTime < 360))) {
            blinds = 0;
            result = true;
            smartArray[i].access = now.unixtime();
            serialPrint("The smart function activated the uncover time");
          }
      }
    }

    if (result) {
      if (!offline) {
        putDataOnServer("coverage=" + blinds);
      }

      setCoverage(blinds, false);
    }
  }
}


void setCoverage(int set, bool calibrate) {
  if (destination == 0) {
    if (calibrate) {
      destination = set;

      Serial.printf("\nCalibration. Blinds movement by %imm. (%i)", (int)destination, calibration);
    } else {
      destination = ((float)steps / 100.0) * (float)(set - coverage);
      coverage = set;
      writeCoverage();

      Serial.printf("\nBlinds movement by %i steps. Changed state to %i", (int)destination, set);
      Serial.print('%');
    }
    actual = 0;
  } else {
    if (!calibrate) {
      float actualPosition = abs(destination) - actual;
      if (destination < 0) {
        actualPosition *= -1;
      }

      float newDestination = ((float)steps / 100.0) * (float)(set - coverage);

      destination = newDestination + actualPosition;

      coverage = set;
      writeCoverage();

      Serial.printf("\nBlinds moved by %i steps, now movement by %i steps. Changed state to %i", actual, (int)destination, set);
      Serial.print('%');
      actual = 0;
    }
  }
}

void cover(int lag) {
  for (int i = 0; i < 8; i++) {
    selectMuxPin(i);

    bool cancel = false;
    if (calibration != 0) {
      if (calibration == 1) {
        cancel = i > 3;
      } else {
      cancel = i < 4;
      }
    }

    if (!cancel) {
      digitalWrite(stepper_pin, HIGH);
      delay(lag);
      digitalWrite(stepper_pin, LOW);
    }
  }
}

void uncover(int lag) {
  for (int i = 7; i >= 0; i--) {
    selectMuxPin(i);

    bool cancel = false;
    if (calibration != 0) {
      if (calibration == 1) {
        cancel = i > 3;
      } else {
      cancel = i < 4;
      }
    }

    if (!cancel) {
      digitalWrite(stepper_pin, HIGH);
      delay(lag);
      digitalWrite(stepper_pin, LOW);
    }
  }
}

void selectMuxPin(byte pin) {
  if (pin < 8) {
    for (int i = 0; i < 3; i++) {
      if (pin & (1 << i)) {
        digitalWrite(multiplexer_pin[i], HIGH);
      } else {
        digitalWrite(multiplexer_pin[i], LOW);
      }
    }
  }
}
