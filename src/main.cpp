#include <c_online.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  LittleFS.begin();
  Wire.begin();

  keep_log = LittleFS.exists("/log.txt");

  note("iDom Blinds " + String(version));
  Serial.print("\nDevice ID: " + WiFi.macAddress());
  offline = !LittleFS.exists("/online.txt");
  Serial.printf("\nThe device is set to %s mode", offline ? "OFFLINE" : "ONLINE");

  sprintf(host_name, "blinds_%s", String(WiFi.macAddress()).c_str());
  WiFi.hostname(host_name);

  if (!readSettings(0)) {
    readSettings(1);
  }
  resume();

  RTC.begin();
  if (RTC.isrunning()) {
    start_time = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
  }
  Serial.printf("\nRTC initialization %s", start_time != 0 ? "completed" : "failed!");

  int new_light = analogRead(light_sensor_pin);
  if (new_light > boundary || (sunset > 0 && sunrise > 0)) {
    light = new_light;
  }

  for (int i = 0; i < 3; i++) {
    pinMode(bipolar_enable_pin[i], OUTPUT);
  }
  pinMode(bipolar_direction_pin, OUTPUT);
  pinMode(bipolar_step_pin, OUTPUT);
  setStepperOff();

  if (ssid != "" && password != "") {
    connectingToWifi();
  } else {
    initiatingWPS();
  }
}


void setStepperOff() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(bipolar_enable_pin[i], HIGH);
  }
  digitalWrite(bipolar_direction_pin, LOW);
  digitalWrite(bipolar_step_pin, LOW);
}

String toPercentages(int value, int steps) {
  return String(value > 0 && steps > 0 ? value * 100 / steps : 0);
}

int toSteps(int value, int steps) {
  return value > 0 && steps > 0 ? value * steps / 100 : 0;
}


bool readSettings(bool backup) {
  File file = LittleFS.open(backup ? "/backup.txt" : "/settings.txt", "r");
  if (!file) {
    note("The " + String(backup ? "backup" : "settings") + " file cannot be read");
    return false;
  }

  DynamicJsonDocument json_object(1024);
  deserializeJson(json_object, file.readString());
  file.close();

  if (json_object.isNull() || json_object.size() < 5) {
    note(String(backup ? "Backup" : "Settings") + " file error");
    return false;
  }

  if (json_object.containsKey("ssid")) {
    ssid = json_object["ssid"].as<String>();
  }
  if (json_object.containsKey("password")) {
    password = json_object["password"].as<String>();
  }

  if (json_object.containsKey("smart")) {
    smart_string = json_object["smart"].as<String>();
    setSmart();
  }
  if (json_object.containsKey("uprisings")) {
    uprisings = json_object["uprisings"].as<int>() + 1;
  }
  if (json_object.containsKey("offset")) {
    offset = json_object["offset"].as<int>();
  }
  if (json_object.containsKey("dst")) {
    dst = json_object["dst"].as<bool>();
  }
  if (json_object.containsKey("sunset")) {
    sunset = json_object["sunset"].as<int>();
  }
  if (json_object.containsKey("sunrise")) {
    sunrise = json_object["sunrise"].as<int>();
  }
  if (json_object.containsKey("boundary")) {
    boundary = json_object["boundary"].as<int>();
  }
  if (json_object.containsKey("reversed")) {
    reversed = json_object["reversed"].as<bool>();
  }

  if (json_object.containsKey("steps1")) {
    steps1 = json_object["steps1"].as<int>();
  }
  if (json_object.containsKey("steps2")) {
    steps2 = json_object["steps2"].as<int>();
  }
  if (json_object.containsKey("steps3")) {
    steps3 = json_object["steps3"].as<int>();
  }

  if (steps1 != 0 && steps2 != 0 && steps3 != 0) {
    separately = true;
  } else {
      if (json_object.containsKey("separately")) {
        separately = json_object["separately"].as<bool>();
      }
  }
  if (json_object.containsKey("inverted")) {
    inverted_sequence = json_object["inverted"].as<bool>();;
  }

  if (json_object.containsKey("destination1")) {
    destination1 = json_object["destination1"].as<int>();
    if (destination1 < 0) {
      destination1 = 0;
    }
    if (destination1 > steps1) {
      destination1 = steps1;
    }
    actual1 = destination1;
  }
  if (json_object.containsKey("destination2")) {
    destination2 = json_object["destination2"].as<int>();
    if (destination2 < 0) {
      destination2 = 0;
    }
    if (destination2 > steps2) {
      destination2 = steps2;
    }
    actual2 = destination2;
  }
  if (json_object.containsKey("destination3")) {
    destination3 = json_object["destination3"].as<int>();
    if (destination3 < 0) {
      destination3 = 0;
    }
    if (destination3 > steps3) {
      destination3 = steps3;
    }
    actual3 = destination3;
  }

  String logs;
  serializeJson(json_object, logs);
  note("Reading the " + String(backup ? "backup" : "settings") + " file:\n " + logs);

  saveSettings();

  return true;
}

void saveSettings() {
  DynamicJsonDocument json_object(1024);

  json_object["ssid"] = ssid;
  json_object["password"] = password;

  json_object["smart"] = smart_string;
  json_object["uprisings"] = uprisings;
  json_object["offset"] = offset;
  json_object["dst"] = dst;
  json_object["sunset"] = sunset;
  json_object["sunrise"] = sunrise;
  json_object["boundary"] = boundary;
  json_object["reversed"] = reversed;
  json_object["separately"] = (steps1 != 0 && steps2 != 0 && steps3 != 0) || separately;
  json_object["inverted"] = inverted_sequence;

  json_object["steps1"] = steps1;
  json_object["steps2"] = steps2;
  json_object["steps3"] = steps3;
  json_object["destination1"] = destination1;
  json_object["destination2"] = destination2;
  json_object["destination3"] = destination3;

  if (writeObjectToFile("settings", json_object)) {
    String logs;
    serializeJson(json_object, logs);
    note("Saving settings:\n " + logs);

    writeObjectToFile("backup", json_object);
  } else {
    note("Saving the settings failed!");
  }
}

void resume() {
  File file = LittleFS.open("/resume.txt", "r");
  if (!file) {
    return;
  }

  DynamicJsonDocument json_object(1024);
  deserializeJson(json_object, file.readString());
  file.close();

  if (!json_object.isNull() && json_object.size() > 0) {
    String logs = "";

    if (json_object.containsKey("1")) {
      actual1 = json_object["1"].as<int>();
      if (actual1 != destination1) {
        logs = "\n1 to " + String(destination1 - actual1) + " steps to " + toPercentages(destination1, steps1) + "%";
      }
    }
    if (json_object.containsKey("2")) {
      actual2 = json_object["2"].as<int>();
      if (actual2 != destination2) {
        logs += "\n2 to " + String(destination2 - actual2) + " steps to " + toPercentages(destination2, steps2) + "%";
      }
    }
    if (json_object.containsKey("3")) {
      actual3 = json_object["3"].as<int>();
      if (actual3 != destination3) {
        logs += "\n3 to " + String(destination3 - actual3) + " steps to " + toPercentages(destination3, steps3) + "%";
      }
    }

    if (actual1 != destination1 || actual2 != destination2 || actual3 != destination3) {
      note("Resume: " + logs);
    } else {
      if (LittleFS.exists("/resume.txt")) {
        LittleFS.remove("/resume.txt");
      }
    }
  }
}

void saveTheState() {
  DynamicJsonDocument json_object(1024);

  json_object["1"] = actual1;
  json_object["2"] = actual2;
  json_object["3"] = actual3;

  writeObjectToFile("resume", json_object);
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
  server.on("/basicdata", HTTP_POST, exchangeOfBasicData);
  server.on("/priority", HTTP_POST, confirmationOfPriority);
  server.on("/reversed", HTTP_POST, reverseDirection);
  server.on("/measurement/start", HTTP_POST, makeMeasurement);
  server.on("/measurement/cancel", HTTP_POST, cancelMeasurement);
  server.on("/measurement/end", HTTP_POST, endMeasurement);
  server.on("/log", HTTP_GET, requestForLogs);
  server.on("/log", HTTP_DELETE, clearTheLog);
  server.on("/admin/reset", HTTP_POST, setMin);
  server.on("/admin/setmax", HTTP_POST, setMax);
  server.on("/admin/setasmax", HTTP_POST, setAsMax);
  server.on("/admin/sensor", HTTP_POST, initiateTheLightSensor);
  server.on("/admin/sensor", HTTP_DELETE, deactivateTheLightSensor);
  server.on("/admin/log", HTTP_POST, activationTheLog);
  server.on("/admin/log", HTTP_DELETE, deactivationTheLog);
  server.on("/admin/wifisettings", HTTP_DELETE, deleteWiFiSettings);
  server.begin();

  note("Launch of services. " + String(host_name) + (MDNS.begin(host_name) ? " started." : " unsuccessful!"));

  MDNS.addService("idom", "tcp", 8080);

  if (!offline) {
    prime = true;
  }
  networked_devices = WiFi.macAddress();
  getOfflineData(true, true);
}

String getBlindsDetail() {
  return String(steps1) + "." + steps2 + "." + steps3 + "," + RTC.isrunning() + "," + String(start_time) + "," + reversed + "," + uprisings + "," + version + "," + boundary + "," + separately + "," + inverted_sequence;
}

String getSensorDetail() {
  return String(light) + String(twilight ? "t" : "") + (twilight_counter > 0 ? ("," + String(twilight_counter)) : "");
}

void handshake() {
  readData(server.arg("plain"), true);

  String reply = "\"id\":\"" + WiFi.macAddress()
  + "\",\"value\":\"" + toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3)
  + "\",\"light\":\"" + getSensorDetail()
  + "\",\"sunset\":" + (light > -1 || sunset > 0 ? sunset : 0)
  + ",\"sunrise\":" + (light > -1 || sunrise > 0 ? sunrise : 0)
  + ",\"steps\":\"" + steps1 + "." + steps2 + "." + steps3
  + "\",\"boundary\":" + boundary
  + ",\"reversed\":" + reversed
  + ",\"separately\":" + separately
  + ",\"inverted\":" + inverted_sequence
  + ",\"version\":" + version
  + ",\"smart\":\"" + smart_string
  + "\",\"rtc\":" + RTC.isrunning()
  + ",\"dst\":" + dst
  + ",\"offset\":" + offset
  + ",\"time\":" + String(RTC.now().unixtime() - offset - (dst ? 3600 : 0))
  + ",\"active\":" + String(start_time != 0 ? RTC.now().unixtime() - offset - (dst ? 3600 : 0) - start_time : 0)
  + ",\"uprisings\":" + uprisings
  + ",\"offline\":" + offline
  + ",\"prime\":" + prime
  + ",\"devices\":\"" + networked_devices + "\"";

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

  + (light > -1 ? ",\"light\":\"" + getSensorDetail() + "\"" : "");

  server.send(200, "text/plain", "{" + reply + "}");
}

void exchangeOfBasicData() {
  readData(server.arg("plain"), true);

  String reply = RTC.isrunning() ? ("\"time\":" + String(RTC.now().unixtime() - offset - (dst ? 3600 : 0))
  + ",\"offset\":" + offset
  + ",\"dst\":" + String(dst)) : "";

  reply += light > -1 ? (String(reply.length() > 0 ? "," : "") + "\"light\":\"" + String(light) + String(twilight ? "t" : "") + "\"") : "";

  reply += !offline && prime ? (String(reply.length() > 0 ? "," : "") + "\"prime\":" + String(prime)) : "";

  reply += String(reply.length() > 0 ? "," : "") + "\"id\":\"" + String(WiFi.macAddress()) + "\"";

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

void setAsMax() {
  steps1 = actual1;
  steps2 = actual2;
  steps3 = actual3;
  destination1 = actual1;
  destination2 = actual2;
  destination3 = actual3;
  saveSettings();
  server.send(200, "text/plain", "Done");
  putOnlineData("detail", "val=100.100.100");
}

void makeMeasurement() {
  if (measurement) {
    return;
  }

  DynamicJsonDocument json_object(1024);
  deserializeJson(json_object, server.arg("plain"));

  if (json_object.isNull()) {
    server.send(200, "text/plain", "Body not received");
    return;
  }

  if (json_object.containsKey("wings")) {
    wings = json_object["wings"].as<int>();
  } else {
    wings = 123;
  }

  if ((strContains(String(wings), "1") && !(destination1 == 0 || actual1 == 0))
  || (strContains(String(wings), "2") && !(destination2 == 0 || actual2 == 0))
  || (strContains(String(wings), "3") && !(destination3 == 0 || actual3 == 0))) {
    server.send(200, "text/plain", "Cannot execute");
    return;
  }

  measurement = true;
  digitalWrite(bipolar_direction_pin, !reversed);

  server.send(200, "text/plain", "Done");
}

void cancelMeasurement() {
  if (!measurement) {
    return;
  }

  measurement = false;
  setStepperOff();

  server.send(200, "text/plain", "Done");
}

void endMeasurement() {
  if (!measurement) {
    return;
  }

  measurement = false;
  setStepperOff();

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
  note("Measurement completed");
  saveSettings();
  sayHelloToTheServer();

  server.send(200, "text/plain", "Done");
}

void initiateTheLightSensor() {
  light = 100;
  server.send(200, "text/plain", "Done");
}

void deactivateTheLightSensor() {
  light = -1;
  sunrise = 0;
  sunset = 0;
  saveSettings();
  introductionToServer();

  server.send(200, "text/plain", "Done");
}


void loop() {
  if (WiFi.status() == WL_CONNECTED) {
  } else {
    if (!sending_error) {
      note("Wi-Fi connection lost");
    }
    sending_error = true;
    cancelMeasurement();
  }

  server.handleClient();
  MDNS.update();

  if (measurement) {
    measurementRotation();
    return;
  }

  if (hasTimeChanged()) {
    if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
      if (loop_time % 2 == 0) {
        saveTheState();
      }
    }
    if (loop_time % 2 == 0) {
      getOnlineData();
    }
    automaticSettings();
  }

  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    rotation();
    if (destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
      Serial.print("\nRoller blind reached the target position");
      setStepperOff();
      if (LittleFS.exists("/resume.txt")) {
        LittleFS.remove("/resume.txt");
      }
    }
  }
}


bool hasTheLightChanged() {
  if (loop_time % 60 != 0) {
    return false;
  }

  int new_light = analogRead(light_sensor_pin);
  bool change = false;

  if (light == -1) {
    if (new_light > boundary) {
      light = new_light;
      change = true;
    } else {
      return false;
    }
  } else {
    if (abs(light - new_light) > (new_light < 30 ? 5 : 20)) {
      light = new_light;
      change = true;
    }
  }

  bool result = false;
  bool sent = false;

  if (block_twilight_counter) {
    if (light < boundary - (boundary < 100 ? 0 : 50) || light > boundary + 50) {
      block_twilight_counter = false;
    }
  } else {
    if (twilight != (light < (twilight ? boundary - (boundary < 100 ? 0 : 50) : boundary + 50))) {
      change = true;
      if (++twilight_counter > 9 && (twilight ? light > boundary : light < boundary)) {
        twilight = light < boundary;
        result = true;
        block_twilight_counter = true;
        twilight_counter = 0;
        putMultiOfflineData("{\"light\":\"" + String(light) + String(twilight ? "t" : "") + "\"}");
      }
    } else {
      twilight_counter = 0;
    }
  }

  if (RTC.isrunning()) {
    if (twilight) {
      if (light < 100 && (sunset <= sunrise || (RTC.now().unixtime() - offset - (dst ? 3600 : 0) - sunset > 72000))) {
        sunset = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
        putOnlineData("detail", "set=" + String(sunset) + "&light=" + getSensorDetail());
        saveSettings();
        sent = true;
      }
    }
    if (light > 100 && sunrise <= sunset) {
      if (++daybreak_counter > 9) {
        sunrise = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
        putOnlineData("detail", "rise=" + String(sunrise) + "&light=" + getSensorDetail());
        saveSettings();
        sent = true;
        daybreak_counter = 0;
      }
    } else {
      daybreak_counter = 0;
    }
  }

  if (!sent && change) {
    putOnlineData("detail", "light=" + getSensorDetail(), false);
  }

  return result;
}

void readData(String payload, bool per_wifi) {
  DynamicJsonDocument json_object(1024);
  deserializeJson(json_object, payload);

  if (json_object.isNull()) {
    if (payload.length() > 0) {
      Serial.print("\n Parsing failed!");
    }
    return;
  }

  if (json_object.containsKey("apk")) {
    per_wifi = json_object["apk"].as<bool>();
  }

  if (json_object.containsKey("id")) {
    String new_networked_devices = json_object["id"].as<String>();
    if (!strContains(networked_devices, new_networked_devices)) {
      networked_devices +=  "," + new_networked_devices;
    }
  }

  if (json_object.containsKey("prime")) {
    prime = false;
  }

  if (json_object.containsKey("calibrate")) {
    if (json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    } else {
      wings = 123;
    }

    calibration(json_object["calibrate"].as<int>(), json_object.containsKey("bypass"));
    return;
  }

  bool settings_change = false;
  bool details_change = false;
  String result = "";

  uint32_t new_time = 0;
  if (json_object.containsKey("offset")) {
    int new_offset = json_object["offset"].as<int>();
    if (offset != new_offset) {
      if (RTC.isrunning() && !json_object.containsKey("time")) {
        RTC.adjust(DateTime((RTC.now().unixtime() - offset) + new_offset));
        note("Time zone change");
      }

      offset = new_offset;
      settings_change = true;
    }
  }

  if (json_object.containsKey("dst")) {
    bool new_dst = json_object["dst"].as<bool>();
    if (dst != new_dst) {
      if (RTC.isrunning() && !json_object.containsKey("time")) {
        if (new_dst) {
          new_time = RTC.now().unixtime() + 3600;
        } else {
          new_time = RTC.now().unixtime() - 3600;
        }
        RTC.adjust(DateTime(new_time));
        note(new_dst ? "Summer time" : "Winter time");
      }

      dst = new_dst;
      settings_change = true;
    }
  }

  if (json_object.containsKey("time")) {
    new_time = json_object["time"].as<uint32_t>() + offset + (dst ? 3600 : 0);
    if (new_time > 1546304461) {
      if (RTC.isrunning()) {
        if (abs(new_time - RTC.now().unixtime()) > 60) {
          RTC.adjust(DateTime(new_time));
        }
      } else {
        RTC.adjust(DateTime(new_time));
        note("Adjust time");
        start_time = RTC.now().unixtime() - offset - (dst ? 3600 : 0);
        if (RTC.isrunning()) {
          sayHelloToTheServer();
        }
      }
    }
  }

  if (json_object.containsKey("up")) {
    uint32_t new_update_time = json_object["up"].as<uint32_t>();
    if (update_time < new_update_time) {
      update_time = new_update_time;
    }
  }

  if (json_object.containsKey("smart")) {
    String new_smart_string = json_object["smart"].as<String>();
    if (smart_string != new_smart_string) {
      smart_string = new_smart_string;
      setSmart();
      result = "smart=" + getSmartString();
      settings_change = true;
    }
  }

  if (json_object.containsKey("val")) {
    String new_value = json_object["val"].as<String>();
    int new_destination1 = steps1 > 0 ? toSteps(new_value.substring(0, new_value.indexOf(".")).toInt(), steps1) : destination1;
    int new_destination2 = destination2;
    int new_destination3 = destination3;

    new_value = new_value.substring(new_value.indexOf(".") + 1);
    new_destination2 = steps2 > 0 ? toSteps(new_value.substring(0, new_value.indexOf(".")).toInt(), steps2) : destination2;
    new_destination3 = steps3 > 0 ? toSteps(new_value.substring(new_value.indexOf(".") + 1).toInt(), steps3) : destination3;

    if (destination1 != new_destination1 || destination2 != new_destination2 || destination3 != new_destination3) {
      destination1 = new_destination1;
      destination2 = new_destination2;
      destination3 = new_destination3;
      prepareRotation();
      result += String(result.length() > 0 ? "&" : "") + "val=" + toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3);
    }
  }

  int new_steps;
  if (json_object.containsKey("steps1")) {
    new_steps = json_object["steps1"].as<int>();
    if (steps1 != new_steps && actual1 == 0) {
      steps1 = new_steps;
      details_change = true;
    }
  }

  if (json_object.containsKey("steps2")) {
    new_steps = json_object["steps2"].as<int>();
    if (steps2 != new_steps && actual2 == 0) {
      steps2 = new_steps;
      details_change = true;
    }
  }

  if (json_object.containsKey("steps3")) {
    new_steps = json_object["steps3"].as<int>();
    if (steps3 != new_steps && actual3 == 0) {
      steps3 = new_steps;
      details_change = true;
    }
  }

  if (json_object.containsKey("boundary")) {
    int new_boundary = json_object["boundary"].as<int>();
    if (boundary != new_boundary) {
      boundary = new_boundary;
      details_change = true;
    }
  }

  if (json_object.containsKey("separately")) {
    bool new_separately = json_object["separately"].as<bool>();
    if (separately != new_separately) {
      separately = new_separately;
      details_change = true;
    }
  }

  if (json_object.containsKey("inverted")) {
    bool new_inverted_sequence = json_object["inverted"].as<bool>();
    if (inverted_sequence != new_inverted_sequence) {
      inverted_sequence = new_inverted_sequence;
      details_change = true;
    }
  }

  if (light == -1 && json_object.containsKey("light")) {
    String new_light = json_object["light"].as<String>();
    if (twilight != strContains(new_light, "t")) {
      twilight = !twilight;
      automaticSettings(true);
    }
  }

  if (settings_change || details_change) {
    note("Received the data:\n " + payload);
    saveSettings();
  }
  if (per_wifi && (result.length() > 0 || details_change)) {
    if (details_change) {
      result += String(result.length() > 0 ? "&" : "") + "detail=" + getBlindsDetail();
    }
    putOnlineData("detail", result);
  }
}

void setSmart() {
  if (smart_string.length() < 2) {
    smart_count = 0;
    return;
  }

  String smart;
  bool enabled;
  String wing;
  String days;
  bool lowering_at_night;
  bool lifting_at_day;
  int lowering_time;
  int lifting_time;
  bool lowering_at_night_and_time;
  bool lifting_at_day_and_time;

  int count = 1;
  smart_count = 1;
  for (byte b: smart_string) {
    if (b == ',') {
      count++;
    }
    if (b == 'b') {
      smart_count++;
    }
  }

  if (smart_array != 0) {
    delete [] smart_array;
  }
  smart_array = new Smart[smart_count];
  smart_count = 0;

  for (int i = 0; i < count; i++) {
    smart = get1(smart_string, i);
    if (smart.length() > 0 && strContains(smart, "b")) {
      enabled = !strContains(smart, "/");
      smart = enabled ? smart : smart.substring(1);

      lowering_time = strContains(smart, "_") ? smart.substring(0, smart.indexOf("_")).toInt() : -1;
      lifting_time = strContains(smart, "-") ? smart.substring(smart.indexOf("-") + 1).toInt() : -1;

      smart = strContains(smart, "_") ? smart.substring(smart.indexOf("_") + 1) : smart;
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

      lowering_at_night = strContains(smart, "n");
      lifting_at_day = strContains(smart, "d");

      lowering_at_night_and_time = strContains(smart, "n&");
      lifting_at_day_and_time = strContains(smart, "d&");

      smart_array[smart_count++] = (Smart) {enabled, wing, days, lowering_at_night, lifting_at_day, lowering_time, lifting_time, lowering_at_night_and_time, lifting_at_day_and_time, 0};
    }
  }
}

bool automaticSettings() {
  return automaticSettings(hasTheLightChanged());
}

bool automaticSettings(bool twilight_changed) {
  bool result = false;
  DateTime now = RTC.now();
  String log = "Smart ";
  int current_time = 0;

  if (RTC.isrunning()) {
    current_time = (now.hour() * 60) + now.minute();

    if (current_time == 120 || current_time == 180) {
      if (now.month() == 3 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 120 && !dst) {
        int new_time = RTC.now().unixtime() + 3600;
        RTC.adjust(DateTime(new_time));
        dst = true;
        saveSettings();
        note("Smart set to summer time");
      }
      if (now.month() == 10 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 180 && dst) {
        int new_time = RTC.now().unixtime() - 3600;
        RTC.adjust(DateTime(new_time));
        dst = false;
        saveSettings();
        note("Smart set to winter time");
      }
    }
  }

  int i = -1;
  while (++i < smart_count) {
    if (smart_array[i].enabled) {
      if (twilight_changed) {
        if (twilight && smart_array[i].lowering_at_night
          && (!smart_array[i].lowering_at_night_and_time || (smart_array[i].lowering_at_night_and_time && smart_array[i].lowering_time < current_time))
          && (strContains(smart_array[i].days, "w") || (RTC.isrunning() && strContains(smart_array[i].days, days_of_the_week[now.dayOfTheWeek()])))) {
          if (strContains(smart_array[i].wing, "1")) {
            destination1 = steps1;
          }
          if (strContains(smart_array[i].wing, "2")) {
            destination2 = steps2;
          }
          if (strContains(smart_array[i].wing, "3")) {
            destination3 = steps3;
          }
          result = true;
          log += "lowering at dusk";
          log += smart_array[i].lowering_at_night_and_time ? " and time" : "";
        }
        if (!twilight && smart_array[i].lifting_at_day
          && (!smart_array[i].lifting_at_day_and_time || (smart_array[i].lifting_at_day_and_time && smart_array[i].lifting_time < current_time))
          && (strContains(smart_array[i].days, "w") || (RTC.isrunning() && strContains(smart_array[i].days, days_of_the_week[now.dayOfTheWeek()])))) {
          if (strContains(smart_array[i].wing, "1")) {
            destination1 = 0;
          }
          if (strContains(smart_array[i].wing, "2")) {
            destination2 = 0;
          }
          if (strContains(smart_array[i].wing, "3")) {
            destination3 = 0;
          }
          result = true;
          log += "lifting at dawn";
          log += smart_array[i].lifting_at_day_and_time ? " and time" : "";
        }
      } else {
        if (RTC.isrunning()) {
          if (smart_array[i].access + 60 < now.unixtime()) {
            if (smart_array[i].lowering_time == current_time
              && (!smart_array[i].lowering_at_night_and_time || (smart_array[i].lowering_at_night_and_time && twilight))
              && (strContains(smart_array[i].days, "w") || strContains(smart_array[i].days, days_of_the_week[now.dayOfTheWeek()]))) {
              smart_array[i].access = now.unixtime();
              if (strContains(smart_array[i].wing, "1")) {
                destination1 = steps1;
              }
              if (strContains(smart_array[i].wing, "2")) {
                destination2 = steps2;
              }
              if (strContains(smart_array[i].wing, "3")) {
                destination3 = steps3;
              }
              result = true;
              log += "lowering at time";
              log += smart_array[i].lowering_at_night_and_time ? " and dusk" : "";
            }
            if (smart_array[i].lifting_time == current_time
              && (!smart_array[i].lifting_at_day_and_time || (smart_array[i].lifting_at_day_and_time && !twilight))
              && (strContains(smart_array[i].days, "w") || strContains(smart_array[i].days, days_of_the_week[now.dayOfTheWeek()]))) {
              smart_array[i].access = now.unixtime();
              if (strContains(smart_array[i].wing, "1")) {
                destination1 = 0;
              }
              if (strContains(smart_array[i].wing, "2")) {
                destination2 = 0;
              }
              if (strContains(smart_array[i].wing, "3")) {
                destination3 = 0;
              }
              result = true;
              log += "lifting at time";
              log += smart_array[i].lifting_at_day_and_time ? " and dawn" : "";
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

    return true;
  } else {
    if (twilight_changed) {
      note("Smart didn't activate anything.");
    }
    return false;
  }
}


void prepareRotation() {
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

void calibration(int set, bool bypass) {
  if (!bypass && (destination1 != actual1 || destination2 != actual2 || destination3 != actual3)) {
    return;
  }

  bool settings_change = false;
  String logs = "";

  if (strContains(String(wings), "1")) {
    if (actual1 == 0) {
      actual1 -= set / 2;
    } else
      if (actual1 == steps1) {
        steps1 += set / 2;
        destination1 = steps1;
        settings_change = true;
        logs += "\n 1 by " + String(set) + " steps. Steps set at " + String(steps1) + ".";
      }
  }
  if (strContains(String(wings), "2")) {
    if (actual2 == 0) {
      actual2 -= set / 2;
    } else
      if (actual2 == steps2) {
        steps2 += set / 2;
        destination2 = steps2;
        settings_change = true;
        logs += "\n 2 by " + String(set) + " steps. Steps set at " + String(steps2) + ".";
      }
  }
  if (strContains(String(wings), "3")) {
    if (actual3 == 0) {
      actual3 -= set / 2;
    } else
      if (actual3 == steps3) {
        steps3 += set / 2;
        destination3 = steps3;
        settings_change = true;
        logs += "\n 3 by " + String(set) + " steps. Steps set at " + String(steps3) + ".";
      }
  }

  if (settings_change) {
    note("Calibration: " + logs);
    saveSettings();
    saveTheState();
  } else {
    note("Zero calibration. " + String(wings) + " by " + String(set) + " steps.");
  }
}

void measurementRotation() {
  if (strContains(String(wings), "1")) {
    digitalWrite(bipolar_enable_pin[0], LOW);
    actual1++;
  }
  if (strContains(String(wings), "2")) {
    digitalWrite(bipolar_enable_pin[1], LOW);
    actual2++;
  }
  if (strContains(String(wings), "3")) {
    digitalWrite(bipolar_enable_pin[2], LOW);
    actual3++;
  }

  digitalWrite(bipolar_step_pin, HIGH);
  digitalWrite(bipolar_step_pin, LOW);
  delay(4);
}

void rotation() {
  if (destination1 != actual1 && (!inverted_sequence ||
    ((destination2 == actual2 || (!separately && ((destination2 > actual2 && destination1 > actual1) || (destination2 > actual2 && destination1 > actual1)))) &&
    (destination3 == actual3 || (!separately && ((destination3 > actual3 && destination1 > actual1) || (destination3 > actual3 && destination1 > actual1))))))) {
    digitalWrite(bipolar_direction_pin, destination1 < actual1 ? reversed : !reversed);
    digitalWrite(bipolar_enable_pin[0], LOW);
    if (destination1 > actual1) {
      actual1++;
    } else {
      actual1--;
    }
  } else {
    digitalWrite(bipolar_enable_pin[0], HIGH);
  }

  if (destination2 != actual2 && (inverted_sequence ?
    (destination3 == actual3 || (!separately && ((destination3 > actual3 && destination2 > actual2) || (destination3 < actual3 && destination2 < actual2)))) :
    (destination1 == actual1 || (!separately && ((destination1 > actual1 && destination2 > actual2) || (destination1 < actual1 && destination2 < actual2)))))) {
    digitalWrite(bipolar_direction_pin, destination2 < actual2 ? reversed : !reversed);
    digitalWrite(bipolar_enable_pin[1], LOW);
    if (destination2 > actual2) {
      actual2++;
    } else {
      actual2--;
    }
  } else {
    digitalWrite(bipolar_enable_pin[1], HIGH);
  }

  if (destination3 != actual3 && (inverted_sequence ||
    ((destination1 == actual1 || (!separately && ((destination1 > actual1 && destination3 > actual3) || (destination1 > actual1 && destination3 > actual3)))) &&
    (destination2 == actual2 || (!separately && ((destination2 > actual2 && destination3 > actual3) || (destination2 > actual2 && destination3 > actual3))))))) {
    digitalWrite(bipolar_direction_pin, destination3 < actual3 ? reversed : !reversed);
    digitalWrite(bipolar_enable_pin[2], LOW);
    if (destination3 > actual3) {
      actual3++;
    } else {
      actual3--;
    }
  } else {
    digitalWrite(bipolar_enable_pin[2], HIGH);
  }

  digitalWrite(bipolar_step_pin, HIGH);
  digitalWrite(bipolar_step_pin, LOW);
  delay(4);
}
