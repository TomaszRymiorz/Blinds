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
  if (!RTC.begin()) {
    Serial.print("failed");
  } else {
    Serial.print("completed");
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

  reversed = SD.exists("reversed.txt");
  Serial.printf("\n Engine set in the %s direction", reversed ? "opposite" : "right");

  if (readWiFiConfiguration()) {
    connectingToWifi();
  } else {
    initiatingWPS();
  }

  setupStepperPins();
}

void setupStepperPins() {
  for (int i = 0; i < 4; i++) {
    pinMode(stepper_pin[i], OUTPUT);
  }
}


void readSteps() {
  String s = readFromSD("steps");
  if (s != "-1") {
    steps = s.toInt();
    Serial.printf("\n Steps: %s", s.c_str());
  }
}

void readCoverage() {
  String s = readFromSD("coverage");
  if (s != "-1") {
    coverage = s.toInt();
    Serial.printf("\n Coverage: %s", s.c_str());
    Serial.print('%');
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
    serialPrint("Identity confirmed");

    reply = "{\"id\":\"" +  WiFi.macAddress()
    + "\",\"coverage\":\"" + String(coverage)
    + "\",\"twilight\":\"" + String(twilight)
    + "\",\"smart\":\"" + smartString
    + "\",\"steps\":\"" + steps
    + "\",\"rtc\":\"" + RTC.isrunning()
    + "\",\"reversed\":\"" + reversed + "\"}";
  } else {
    reply = "0";
  }
  server.send(200, "text/plain", reply);
}

void requestForState() {
  String reply = "{\"state\":\"" + String(coverage) + (twilight ? "t" : "") + "-" + (measure > 0 ? String(measure) : "") + "\"}";
  server.send(200, "text/plain", reply);
  readData(server.arg("plain"));
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
  if (SD.exists("reversed.txt") && !reversed) {
    SD.remove("reversed.txt");
  }
  if (!SD.exists("reversed.txt") && reversed) {
    writeOnSD("reversed", "", "", "// Obecność tego pliku wskazuje na odwrotny kierunek obrotów silnika.");
  }
  SPI.end();
  setupStepperPins();
  Serial.printf("\nEngine set in the %s direction", reversed ? "opposite" : "right");
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
        blackout = false;
      }
    } else {
      destination = 0;
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (reconnect) {
      serialPrint("Reconnection with Wi-Fi");
      connectingToWifi();
    } else {
      if (initiatingWPS()) {
        setupStepperPins();
      }
    }
    return;
  }

  server.handleClient();

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

  if (daylightHasChanged()) {
    if (offline) {
      postToTwin("{\"twilight\":\"" + String(twilight) + "\"}");
    } else {
      putDataOnServer("twilight=" + String(twilight));
    }
  }

  if (timeHasChanged()) {
    if (!offline) {
      getOnlineData();
    }

    checkSmart();
  }
}

bool daylightHasChanged() {
  int light = analogRead(twilight_pin);
  delay(3);

  if (twilight != (light < 100)) {
    twilight = light < 100;
    return true;
  }
  return false;
}

int readData(String payload) {
  DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(3));
  JsonObject& jsonObject = jsonBuffer.parseObject(payload);

  if (!jsonObject.success()) {
    if (payload.length() > 0) {
      Serial.print("\n" + payload);
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

  if (jsonObject.containsKey("id") && jsonObject["id"].as<String>() == WiFi.macAddress()) {
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
    setCoverage(jsonObject["calibrate"].as<int>(), true);
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
  bool coverAtNight;
  int coverTime;
  int uncoverTime;

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
      coverTime = strContains(smart, "_") ? smart.substring(0, smart.indexOf("_")).toInt() : -1;
      uncoverTime = strContains(smart, "-") ? smart.substring(smart.indexOf("-") + 1, smart.length()).toInt() : -1;

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

      coverAtNight = strContains(smart, "n");

      smartArray[i] = (Smart) {days, coverAtNight, coverTime, uncoverTime, 0};
    }
  }
}

void checkSmart() {
  if (!RTC.isrunning()) {
    return;
  }

  DateTime now = RTC.now();
  int currentTime = (now.hour() * 60) + now.minute();
  bool result = false;
  int blinds = 0;

  for (int i = 0; i < smartCount; i++) {
    if (smartArray[i].access + 60 < now.unixtime()) {
      if (smartArray[i].coverTime == currentTime
        && strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()])) {
          blinds = 100;
          result = true;
          smartArray[i].access = now.unixtime();
          serialPrint("The smart function activated the cover on time");
        }

      if (smartArray[i].uncoverTime == currentTime
        && (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) || (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() - 1]) && currentTime < 360))) {
          blinds = 0;
          result = true;
          smartArray[i].access = now.unixtime();
          serialPrint("The smart function activated the uncover time");
        }
    }

    if (smartArray[i].coverAtNight
      && (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek()]) || (strContains(smartArray[i].days, daysOfTheWeek[now.dayOfTheWeek() - 1]) && currentTime < 600))) {
        if (twilight && !blackout) {
          blinds = 100;
          result = true;
          blackout = true;
          serialPrint("The smart function activated the cover at night");
        }
        if (blackout && !twilight) {
          blinds = 0;
          result = true;
          blackout = false;
          serialPrint("The smart function activated the uncover at day");
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

      Serial.printf("\nCalibration. Blinds movement by %smm.", String(destination).c_str());
    } else {
      int value = set - coverage;
      destination = ((float)steps / 100.0) * (float)value;
      coverage = set;
      writeCoverage();

      Serial.printf("\nBlinds movement by %s steps. Changed state to %s", String(destination).c_str(), String(set).c_str());
      Serial.print('%');
    }
    actual = 0;
  }
}

void cover(int lag) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(stepper_pin[i], HIGH);
    delay(lag);
    digitalWrite(stepper_pin[i], LOW);
  }
}

void uncover(int lag) {
  for (int i = 4; i >= 0; i--) {
    digitalWrite(stepper_pin[i], HIGH);
    delay(lag);
    digitalWrite(stepper_pin[i], LOW);
  }
}
