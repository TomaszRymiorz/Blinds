#include <c_online.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  LittleFS.begin();
  Wire.begin();

  keep_log = LittleFS.exists("/log.txt");

  note("iDom Blinds ." + String(version));
  offline = !LittleFS.exists("/online.txt");
  Serial.print(offline ? " OFFLINE" : " ONLINE");

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

  int new_light = analogRead(light_sensor_pin);
  if ((new_light > boundary || (sunset > 0 && sunrise > 0)) && new_light > 8) {
    light = new_light;
    twilight = light < boundary;
    twilight_sensor = twilight;
  }

  for (int i = 0; i < 3; i++) {
    pinMode(bipolar_enable_pin[i], OUTPUT);
  }
  pinMode(bipolar_direction_pin, OUTPUT);
  pinMode(bipolar_step_pin, OUTPUT);
  setStepperOff();
  setupOTA();

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

  if (json_object.isNull() || json_object.size() < 5) {
    note(String(backup ? "Backup" : "Settings") + " file error");
    file.close();
    return false;
  }

  file.seek(0);
  note("Reading the " + String(backup ? "backup" : "settings") + " file:\n " + file.readString());
  file.close();

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

  if (steps1 > 0 && steps2 > 0 && steps3 > 0) {
    separately = true;
  } else {
      if (json_object.containsKey("separately")) {
        separately = json_object["separately"].as<bool>();
      }
  }
  if (json_object.containsKey("inverted")) {
    inverted_sequence = json_object["inverted"].as<bool>();
  }
  if (json_object.containsKey("tandem")) {
    tandem = json_object["tandem"].as<bool>();
  }
  if (json_object.containsKey("location")) {
    geo_location = json_object["location"].as<String>();
  }
  if (json_object.containsKey("sensors")) {
    also_sensors = json_object["sensors"].as<bool>();
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

  saveSettings(false);

  return true;
}

void saveSettings() {
  saveSettings(true);
}

void saveSettings(bool log) {
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
  json_object["separately"] = (steps1 > 0 && steps2 > 0 && steps3 > 0) || separately;
  json_object["inverted"] = inverted_sequence;
  json_object["tandem"] = tandem;
  json_object["location"] = geo_location;
  json_object["sensors"] = also_sensors;

  json_object["steps1"] = steps1;
  json_object["steps2"] = steps2;
  json_object["steps3"] = steps3;
  json_object["destination1"] = destination1;
  json_object["destination2"] = destination2;
  json_object["destination3"] = destination3;

  if (writeObjectToFile("settings", json_object)) {
    if (log) {
      String logs;
      serializeJson(json_object, logs);
      note("Saving settings:\n " + logs);
    }

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

  DynamicJsonDocument json_object(200);
  deserializeJson(json_object, file.readString());
  file.close();

  if (json_object.isNull() || json_object.size() < 1) {
    return;
  }

  if (json_object.containsKey("1")) {
    actual1 = json_object["1"].as<int>();
  }
  if (json_object.containsKey("2")) {
    actual2 = json_object["2"].as<int>();
  }
  if (json_object.containsKey("3")) {
    actual3 = json_object["3"].as<int>();
  }

  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    String logs = "";
    if (destination1 != actual1) {
      logs += "\n1 to " + String(destination1 - actual1) + " steps to " + toPercentages(destination1, steps1) + "%";
    }
    if (destination2 != actual2) {
      logs += "\n2 to " + String(destination2 - actual2) + " steps to " + toPercentages(destination2, steps2) + "%";
    }
    if (destination3 != actual3) {
      logs += "\n3 to " + String(destination3 - actual3) + " steps to " + toPercentages(destination3, steps3) + "%";
    }
    note("Resume: " + logs);
  } else {
    if (LittleFS.exists("/resume.txt")) {
      LittleFS.remove("/resume.txt");
    }
  }
}

void saveTheState() {
  DynamicJsonDocument json_object(200);

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
  server.on("/measurement/start", HTTP_POST, makeMeasurement);
  server.on("/measurement/cancel", HTTP_POST, cancelMeasurement);
  server.on("/measurement/end", HTTP_POST, endMeasurement);
  server.on("/log", HTTP_GET, requestForLogs);
  server.on("/log", HTTP_DELETE, clearTheLog);
  server.on("/admin/update", HTTP_POST, manualUpdate);
  server.on("/admin/reset", HTTP_POST, setMin);
  server.on("/admin/setmax", HTTP_POST, setMax);
  server.on("/admin/setasmax", HTTP_POST, setAsMax);
  server.on("/admin/sensor", HTTP_POST, initiateTheLightSensor);
  server.on("/admin/sensor", HTTP_DELETE, deactivateTheLightSensor);
  server.on("/admin/log", HTTP_POST, activationTheLog);
  server.on("/admin/log", HTTP_DELETE, deactivationTheLog);
  server.begin();

  note(String(host_name) + (MDNS.begin(host_name) ? " started" : " unsuccessful!"));

  MDNS.addService("idom", "tcp", 8080);

  getTime();
  getOfflineData();
}

String getBlindsDetail() {
  return "";
  // This function is only available with a ready-made iDom device.
}

String getValue() {
  return "";
  // This function is only available with a ready-made iDom device.
}

String getBlindsPosition() {
  return "";
  // This function is only available with a ready-made iDom device.
}

String getSensorDetail() {
  return light > -1 ? (String(light) + String(twilight ? "t" : "") + (twilight_counter > 0 ? ("," + String(twilight_counter)) : "")) : -1;
}

void handshake() {
  if (server.hasArg("plain")) {
    readData(server.arg("plain"), true);
  }

  String reply = "\"id\":\"" + WiFi.macAddress()
  + "\",\"value\":\"" + toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3)
  + "\",\"pos\":\"" + toPercentages(actual1, steps1) + "." + toPercentages(actual2, steps2) + "." + toPercentages(actual3, steps3)
  + "\",\"light\":\"" + getSensorDetail()
  + "\",\"twilight\":" + twilight
  + ",\"cloudiness\":" + cloudiness
  + ",\"sunset\":" + (light > -1 || sunset > 0 ? sunset : 0)
  + ",\"sunrise\":" + (light > -1 || sunrise > 0 ? sunrise : 0)
  + ",\"next_sunset\":" + next_sunset
  + ",\"next_sunrise\":" + next_sunrise
  + ",\"sun_check\":" + last_sun_check
  + ",\"steps\":\"" + steps1 + "." + steps2 + "." + steps3
  + "\",\"boundary\":" + boundary
  + ",\"reversed\":" + reversed
  + ",\"separately\":" + separately
  + ",\"inverted\":" + inverted_sequence
  + ",\"tandem\":" + tandem
  + ",\"location\":\"" + geo_location
  + "\",\"sensors\":" + also_sensors
  + ",\"version\":" + version
  + ",\"smart\":\"" + smart_string
  + "\",\"rtc\":" + RTC.isrunning()
  + ",\"dst\":" + dst
  + ",\"offset\":" + offset
  + ",\"time\":" + (RTC.isrunning() ? String(RTC.now().unixtime() - offset - (dst ? 3600 : 0)) : "0")
  + ",\"active\":" + String(start_time > 0 ? RTC.now().unixtime() - offset - (dst ? 3600 : 0) - start_time : 0)
  + ",\"uprisings\":" + uprisings
  + ",\"offline\":" + offline;

  Serial.print("\nHandshake");
  server.send(200, "text/plain", "{" + reply + "}");
}

void requestForState() {
  String reply = "\"state1\":\"" + toPercentages(destination1, steps1) + (!measurement && destination1 != actual1 ? "^" + toPercentages(actual1, steps1) : "")
  + "\",\"state2\":\"" + toPercentages(destination2, steps2) + (!measurement && destination2 != actual2 ? "^" + toPercentages(actual2, steps2) : "")
  + "\",\"state3\":\"" + toPercentages(destination3, steps3) + (!measurement && destination3 != actual3 ? "^" + toPercentages(actual3, steps3) : "")
  + "\"";

  if (light > -1) {
    reply += ",\"light\":\"" + getSensorDetail() + "\"";
  }

  server.send(200, "text/plain", "{" + reply + "}");
}

void exchangeOfBasicData() {
  if (server.hasArg("plain")) {
    readData(server.arg("plain"), true);
  }

  String reply = "\"offset\":" + String(offset) + ",\"dst\":" + String(dst);

  if (RTC.isrunning()) {
    reply += ",\"time\":" + String(RTC.now().unixtime() - offset - (dst ? 3600 : 0));
  }

  if (light > -1) {
    reply += (String(reply.length() > 0 ? "," : "") + "\"light\":\"" + String(light) + String(twilight_sensor ? "t" : "") + "\"");
  }

  server.send(200, "text/plain", "{" + reply + "}");
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
  putOnlineData("val=0.0.0&pos=0.0.0");
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
  putOnlineData("val=100.100.100&pos=100.100.100");
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
  putOnlineData("val=100.100.100&pos=100.100.100");
}

void makeMeasurement() {
  if (measurement) {
    return;
  }

  wings = 123;
  if (server.hasArg("plain")) {
    DynamicJsonDocument json_object(1024);
    deserializeJson(json_object, server.arg("plain"));

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  if ((strContains(wings, "1") && !(destination1 == 0 || actual1 == 0))
  || (strContains(wings, "2") && !(destination2 == 0 || actual2 == 0))
  || (strContains(wings, "3") && !(destination3 == 0 || actual3 == 0))) {
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

  if (strContains(wings, "1")) {
    steps1 = actual1;
    destination1 = actual1;
  }
  if (strContains(wings, "2")) {
    steps2 = actual2;
    destination2 = actual2;
  }
  if (strContains(wings, "3")) {
    steps3 = actual3;
    destination3 = actual3;
  }
  note("Measurement completed");
  saveSettings();
  putOnlineData("val=" + getValue() + "&pos=" + getBlindsPosition() + "&detail=" + getBlindsDetail());

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

  if (destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
    ArduinoOTA.handle();
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
        putOnlineData("pos=" + getBlindsPosition() + (sending_error ? "&val=" + getValue() : ""), false, true);
      } else {
        saveTheState();
      }
    } else {
      getOnlineData();
    }
    automaticSettings();
  }

  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    rotation();
    if (destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
      putOnlineData("pos=" + getBlindsPosition());
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
      if (geo_location.length() < 2) {
        return false;
      }
    }
  } else {
    if (abs(light - new_light) > (new_light < 30 ? 5 : 20)) {
      light = new_light;
      change = true;
    }
  }

  DateTime now = RTC.now();
  int current_time = RTC.isrunning() ? (now.hour() * 60) + now.minute() : 0;
  bool result = false;
  bool sent = false;

  if (geo_location.length() > 2) {
    if ((current_time > 51 && last_sun_check != now.day()) || next_sunset == -1 || next_sunrise == -1) {
      getSunriseSunset(now.day());
    }

    if (RTC.isrunning() && next_sunset > -1 && next_sunrise > -1) {
      if (current_time == next_sunset && !twilight) {
        twilight = true;
        result = true;
      }
      if (current_time == next_sunrise && twilight) {
        twilight = false;
        result = true;
      }
    }
  }

  if (light > -1) {
    if (block_twilight_counter) {
      if (light < boundary - (boundary < 100 ? 0 : 50) || light > boundary + 50) {
        block_twilight_counter = false;
      }
    } else {
      if (twilight_sensor != (light < (twilight_sensor ? boundary - (boundary < 100 ? 0 : 50) : boundary + 50))) {
        change = true;
        if (++twilight_counter > 9 && (twilight_sensor ? light > boundary : light < boundary)) {
          if (geo_location.length() < 2) {
            twilight = light < boundary;
          } else {
            if (also_sensors) {
              twilight = light < boundary;
            } else {
              cloudiness = twilight;
            }
          }
          twilight_sensor = !twilight_sensor;
          result = true;
          block_twilight_counter = true;
          twilight_counter = 0;
          putMultiOfflineData("{\"light\":\"" + String(light) + String(twilight_sensor ? "t" : "") + "\"}");
        }
      } else {
        twilight_counter = 0;
      }
    }
  }

  if (RTC.isrunning()) {
    if (twilight) {
      if (((light > -1 && light < 100) || (light == -1 && geo_location.length() > 2 && current_time == next_sunset)) && (now.unixtime() - offset - (dst ? 3600 : 0) - sunset > 72000)) { // || sunset <= sunrise // && (light == -1 || also_sensors)
        sunset = now.unixtime() - offset - (dst ? 3600 : 0);
        putOnlineData("set=" + String(sunset) + "&light=" + getSensorDetail());
        if (!result) {
          saveSettings();
        }
        sent = true;
      }
    }
    if ((light > 100 || (light == -1 && geo_location.length() > 2 && current_time == next_sunrise)) && (now.unixtime() - offset - (dst ? 3600 : 0) - sunrise > 72000)) { // || sunrise <= sunset // && (light == -1 || also_sensors)
      if (++daybreak_counter > 9 || geo_location.length() > 2) {
        sunrise = now.unixtime() - offset - (dst ? 3600 : 0);
        putOnlineData("rise=" + String(sunrise) + "&light=" + getSensorDetail());
        if (!result) {
          saveSettings();
        }
        sent = true;
        daybreak_counter = 0;
      }
    } else {
      daybreak_counter = 0;
    }
  }

  if (!sent && change) {
    putOnlineData("light=" + getSensorDetail(), false, false);
  }

  return result;
}

void readData(String payload, bool per_wifi) {
  DynamicJsonDocument json_object(1024);
  deserializeJson(json_object, payload);

  if (json_object.isNull()) {
    if (payload.length() > 0) {
      note("Parsing failed!");
    }
    return;
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

  if (json_object.containsKey("offset")) {
    if (offset != json_object["offset"].as<int>()) {
      if (RTC.isrunning() && !json_object.containsKey("time")) {
        RTC.adjust(DateTime((RTC.now().unixtime() - offset) + json_object["offset"].as<int>()));
        note("Time zone change");
      }

      offset = json_object["offset"].as<int>();
      settings_change = true;
    }
  }

  if (json_object.containsKey("dst")) {
    if (dst != strContains(json_object["dst"].as<String>(), "1")) {
      dst = !dst;
      settings_change = true;

      if (RTC.isrunning() && !json_object.containsKey("time")) {
        RTC.adjust(DateTime(RTC.now().unixtime() + (dst ? 3600 : -3600)));
        note(dst ? "Summer time" : "Winter time");
      }
    }
  }

  if (json_object.containsKey("time")) {
    uint32_t new_time = json_object["time"].as<uint32_t>() + offset + (dst ? 3600 : 0);
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
          details_change = true;
        }
      }
    }
  }

  if (json_object.containsKey("smart")) {
    if (smart_string != json_object["smart"].as<String>()) {
      smart_string = json_object["smart"].as<String>();
      setSmart();
      if (per_wifi) {
        result += String(result.length() > 0 ? "&" : "") + "smart=" + getSmartString();
      }
      settings_change = true;
    }
  }

  if (json_object.containsKey("val")) {
    String new_value = json_object["val"].as<String>();
    if (steps1 > 0) {
      destination1 = toSteps(new_value.substring(0, new_value.indexOf(".")).toInt(), steps1);
    }

    new_value = new_value.substring(new_value.indexOf(".") + 1);
    if (steps2 > 0) {
      destination2 = toSteps(new_value.substring(0, new_value.indexOf(".")).toInt(), steps2);
    }
    if (steps3 > 0) {
      destination3 = toSteps(new_value.substring(new_value.indexOf(".") + 1).toInt(), steps3);
    }

    if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
      prepareRotation(per_wifi ? (json_object.containsKey("apk") ? "apk" : "local") : "cloud");
      if (per_wifi) {
        result += String(result.length() > 0 ? "&" : "") + "val=" + getValue();
      }
    }
  }

  if (json_object.containsKey("steps1") && actual1 == 0) {
    if (steps1 != json_object["steps1"].as<int>()) {
      steps1 = json_object["steps1"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("steps2") && actual2 == 0) {
    if (steps2 != json_object["steps2"].as<int>()) {
      steps2 = json_object["steps2"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("steps3") && actual3 == 0) {
    if (steps3 != json_object["steps3"].as<int>()) {
      steps3 = json_object["steps3"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("reversed")) {
    if (reversed != strContains(json_object["reversed"].as<String>(), "1")) {
      reversed = !reversed;
      details_change = true;
    }
  }

  if (json_object.containsKey("boundary")) {
    if (boundary != json_object["boundary"].as<int>()) {
      boundary = json_object["boundary"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("separately")) {
    if (separately != strContains(json_object["separately"].as<String>(), "1")) {
      separately = !separately;
      details_change = true;
    }
  }

  if (json_object.containsKey("inverted")) {
    if (inverted_sequence != strContains(json_object["inverted"].as<String>(), "1")) {
      inverted_sequence = !inverted_sequence;
      details_change = true;
    }
  }

  if (json_object.containsKey("tandem")) {
    if (tandem != strContains(json_object["tandem"].as<String>(), "1")) {
      tandem = !tandem;
      details_change = true;
    }
  }

  if (json_object.containsKey("location")) {
    if (geo_location != json_object["location"].as<String>()) {
      geo_location = json_object["location"].as<String>();
      getSunriseSunset(RTC.now().day());
      details_change = true;
    }
  }

  if (json_object.containsKey("sensors")) {
    if (also_sensors != strContains(json_object["sensors"].as<String>(), "1")) {
      also_sensors = !also_sensors;
      details_change = true;
    }
  }

  if (json_object.containsKey("light") && light == -1) {
    if (((geo_location.length() < 2 || also_sensors) && twilight != strContains(json_object["light"].as<String>(), "t"))
    || (geo_location.length() > 2 && !also_sensors && cloudiness != strContains(json_object["light"].as<String>(), "t"))) {
      if (geo_location.length() < 2) {
        twilight = !twilight;
      } else {
        if (also_sensors) {
          twilight = !twilight;
        } else {
          cloudiness = !cloudiness;
        }
      }
      automaticSettings(true);
    }
  }

  if (settings_change || details_change) {
    note("Received the data:\n " + payload);
    saveSettings();
  }
  if (!offline && (result.length() > 0 || details_change)) {
    if (details_change) {
      result += String(result.length() > 0 ? "&" : "") + "detail=" + getBlindsDetail();
    }
    putOnlineData(result);
  }
}

void setSmart() {
  if (smart_string.length() < 2) {
    smart_count = 0;
    return;
  }

  int count = 1;
  smart_count = 1;
  for (char b: smart_string) {
    if (b == ',') {
      count++;
    }
    if (b == smart_prefix) {
      smart_count++;
    }
  }

  if (smart_array != 0) {
    delete [] smart_array;
  }
  smart_array = new Smart[smart_count];
  smart_count = 0;

  String single_smart_string;

  for (int i = 0; i < count; i++) {
    single_smart_string = get1(smart_string, i);
    if (strContains(single_smart_string, String(smart_prefix))) {

      if (strContains(single_smart_string, "/")) {
        smart_array[smart_count].enabled = false;
        single_smart_string = single_smart_string.substring(1);
      } else {
        smart_array[smart_count].enabled = true;
      }

      if (strContains(single_smart_string, "_")) {
        smart_array[smart_count].lowering_time = single_smart_string.substring(0, single_smart_string.indexOf("_")).toInt();
        single_smart_string = single_smart_string.substring(single_smart_string.indexOf("_") + 1);
      } else {
        smart_array[smart_count].lowering_time = -1;
      }

      if (strContains(single_smart_string, "-")) {
        smart_array[smart_count].lifting_time = single_smart_string.substring(single_smart_string.indexOf("-") + 1).toInt();
        single_smart_string = single_smart_string.substring(0, single_smart_string.indexOf("-"));
      } else {
        smart_array[smart_count].lifting_time = -1;
      }

      if (strContains(single_smart_string, "4")) {
        smart_array[smart_count].wing = "123";
      } else {
        smart_array[smart_count].wing = strContains(single_smart_string, "1") ? "1" : "";
        smart_array[smart_count].wing += strContains(single_smart_string, "2") ? "2" : "";
        smart_array[smart_count].wing += strContains(single_smart_string, "3") ? "3" : "";
        if (smart_array[smart_count].wing == "") {
          smart_array[smart_count].wing = "123";
        }
      }

      if (strContains(single_smart_string, "w")) {
        smart_array[smart_count].days = "w";
      } else {
        smart_array[smart_count].days = strContains(single_smart_string, "o") ? "o" : "";
        smart_array[smart_count].days += strContains(single_smart_string, "u") ? "u" : "";
        smart_array[smart_count].days += strContains(single_smart_string, "e") ? "e" : "";
        smart_array[smart_count].days += strContains(single_smart_string, "h") ? "h" : "";
        smart_array[smart_count].days += strContains(single_smart_string, "r") ? "r" : "";
        smart_array[smart_count].days += strContains(single_smart_string, "a") ? "a" : "";
        smart_array[smart_count].days += strContains(single_smart_string, "s") ? "s" : "";
      }

      smart_array[smart_count].lowering_at_night = strContains(single_smart_string, "n");
      smart_array[smart_count].lifting_at_day = strContains(single_smart_string, "d");
      smart_array[smart_count].lowering_at_night_and_time = strContains(single_smart_string, "n&");
      smart_array[smart_count].lifting_at_day_and_time = strContains(single_smart_string, "d&");
      smart_array[smart_count].react_to_cloudiness = strContains(single_smart_string, "z");
      smart_array[smart_count].access = 0;

      smart_count++;
    }
  }
  note("Smart contains " + String(smart_count) + " of " + String(smart_prefix));
}

bool automaticSettings() {
  return automaticSettings(hasTheLightChanged());
}

bool automaticSettings(bool light_changed) {
  bool result = false;
  DateTime now = RTC.now();
  String log = "Smart ";
  int current_time = -1;

  if (RTC.isrunning()) {
    current_time = (now.hour() * 60) + now.minute();

    if (current_time == 120 || current_time == 180) {
      if (now.month() == 3 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 120 && !dst) {
        int new_time = now.unixtime() + 3600;
        RTC.adjust(DateTime(new_time));
        dst = true;
        note("Smart set to summer time");
        saveSettings();
        getSunriseSunset(now.day());
      }
      if (now.month() == 10 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 180 && dst) {
        int new_time = now.unixtime() - 3600;
        RTC.adjust(DateTime(new_time));
        dst = false;
        note("Smart set to winter time");
        saveSettings();
        getSunriseSunset(now.day());
      }
    }

    if (current_time == 61 && now.second() == 0 && destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
      checkForUpdate();
    }
  }

  int i = -1;
  while (++i < smart_count) {
    if (smart_array[i].enabled && (strContains(smart_array[i].days, "w") || (RTC.isrunning() && strContains(smart_array[i].days, days_of_the_week[now.dayOfTheWeek()])))) {
      if (light_changed) {
        if (smart_array[i].lowering_at_night
        && (!smart_array[i].lowering_at_night_and_time || (smart_array[i].lowering_at_night_and_time && smart_array[i].lowering_time > -1 && smart_array[i].lowering_time < current_time) || (smart_array[i].react_to_cloudiness && cloudiness))
        && (twilight || (smart_array[i].react_to_cloudiness && cloudiness))) {
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
          log += "lowering at ";
          log += smart_array[i].react_to_cloudiness && cloudiness ? "cloudiness" : "dusk";
          log += smart_array[i].lowering_at_night_and_time && twilight ? " and time" : "";
        }
        if (smart_array[i].lifting_at_day
        && (!smart_array[i].lifting_at_day_and_time || (smart_array[i].lifting_at_day_and_time && smart_array[i].lifting_time > -1 && smart_array[i].lifting_time < current_time) || (smart_array[i].react_to_cloudiness && !cloudiness))
        && (!twilight || (smart_array[i].react_to_cloudiness && !cloudiness))) {
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
          log += "lifting at ";
          log += smart_array[i].react_to_cloudiness && !cloudiness ? "sunshine" : "dawn";
          log += smart_array[i].lifting_at_day_and_time && !twilight ? " and time" : "";
        }
      } else {
        if (RTC.isrunning() && smart_array[i].access + 60 < now.unixtime()) {
          if (smart_array[i].lowering_time == current_time
          && (!smart_array[i].lowering_at_night_and_time || (smart_array[i].lowering_at_night_and_time && twilight))) {
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
          && (!smart_array[i].lifting_at_day_and_time || (smart_array[i].lifting_at_day_and_time && !twilight))) {
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

  if (result && (destination1 != actual1 || destination2 != actual2 || destination3 != actual3)) {
    note(log);
    putOnlineData("val=" + getValue());
    prepareRotation("smart");
  } else {
    if (light_changed) {
      note("Smart didn't activate anything.");
    }
  }
  return result;
}


void prepareRotation(String orderer) {
  String logs = "";
  if (destination1 != actual1) {
    logs = "\n 1 by " + String(destination1 - actual1) + " steps to " + toPercentages(destination1, steps1) + "%";
  }
  if (destination2 != actual2) {
    logs += "\n 2 by " + String(destination2 - actual2) + " steps to " + toPercentages(destination2, steps2) + "%";
  }
  if (destination3 != actual3) {
    logs += "\n 3 by " + String(destination3 - actual3) + " steps to " + toPercentages(destination3, steps3) + "%";
  }
  note("Movement (" + orderer + "): " + logs);

  saveSettings();
  saveTheState();
}

void calibration(int set, bool bypass) {
  if (!bypass && (destination1 != actual1 || destination2 != actual2 || destination3 != actual3)) {
    return;
  }

  bool settings_change = false;
  String logs = "";

  if (strContains(wings, "1")) {
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
  if (!tandem) {
    if (strContains(wings, "2")) {
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
    if (strContains(wings, "3")) {
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
  if (strContains(wings, "1")) {
    digitalWrite(bipolar_enable_pin[0], LOW);
    if (tandem) {
      digitalWrite(bipolar_enable_pin[1], LOW);
    }
    actual1++;
  }
  if (!tandem) {
    if (strContains(wings, "2")) {
      digitalWrite(bipolar_enable_pin[1], LOW);
      actual2++;
    }
    if (strContains(wings, "3")) {
      digitalWrite(bipolar_enable_pin[2], LOW);
      actual3++;
    }
  }

  digitalWrite(bipolar_step_pin, HIGH);
  digitalWrite(bipolar_step_pin, LOW);
  delay(4);
}

void rotation() {
  if (destination1 != actual1 && (!inverted_sequence ||
    ((destination2 == actual2 || (!separately && ((destination2 > actual2 && destination1 > actual1) || (destination2 < actual2 && destination1 < actual1)))) &&
    (destination3 == actual3 || (!separately && ((destination3 > actual3 && destination1 > actual1) || (destination3 < actual3 && destination1 < actual1))))))) {
    digitalWrite(bipolar_direction_pin, destination1 < actual1 ? reversed : !reversed);
    digitalWrite(bipolar_enable_pin[0], LOW);
    if (tandem) {
      digitalWrite(bipolar_enable_pin[1], LOW);
    }
    if (destination1 > actual1) {
      actual1++;
    } else {
      actual1--;
    }
  } else {
    digitalWrite(bipolar_enable_pin[0], HIGH);
    if (tandem) {
      digitalWrite(bipolar_enable_pin[1], HIGH);
    }
  }

  if (!tandem) {
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
      ((destination1 == actual1 || (!separately && ((destination1 > actual1 && destination3 > actual3) || (destination1 < actual1 && destination3 < actual3)))) &&
      (destination2 == actual2 || (!separately && ((destination2 > actual2 && destination3 > actual3) || (destination2 < actual2 && destination3 < actual3))))))) {
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
  }

  digitalWrite(bipolar_step_pin, HIGH);
  digitalWrite(bipolar_step_pin, LOW);
  delay(4);
}
