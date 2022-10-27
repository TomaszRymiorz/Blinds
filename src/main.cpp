#include <c_online.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  LittleFS.begin();
  Wire.begin();

  keep_log = LittleFS.exists("/log.txt");

  #ifdef RTC_DS1307
    rtc.begin();
  #endif

  note("iDom Blinds " + String(version) + "." + String(core_version));

  offline = !LittleFS.exists("/online.txt");
  Serial.print(offline ? " OFFLINE" : " ONLINE");

  sprintf(host_name, "blinds_%s", String(WiFi.macAddress()).c_str());
  WiFi.hostname(host_name);

  if (!readSettings(0)) {
    readSettings(1);
  }
  resume();

  if (RTCisrunning()) {
    start_time = rtc.now().unixtime() - offset - (dst ? 3600 : 0);
  }

  light = analogRead(light_sensor_pin);
  if (light > boundary || (sunset > 0 && sunrise > 0 && light > 8)) {
    twilight_sensor = light < boundary;
  } else {
    light = -1;
  }

  for (int i = 0; i < 3; i++) {
    pinMode(bipolar_enable_pin[i], OUTPUT);
  }
  pinMode(bipolar_direction_pin, OUTPUT);
  pinMode(bipolar_step_pin, OUTPUT);
  setStepperOff();
  setupOTA();
  connectingToWifi(false);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {

    if (destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
      ArduinoOTA.handle();
    }
    server.handleClient();
    MDNS.update();
  } else {
    if (!auto_reconnect) {
      connectingToWifi(true);
    } else {
      if (!sending_error) {
        sending_error = true;
        note("Wi-Fi connection lost");
      }
    }
    cancelMeasurement();
  }

  if (measurement) {
    measurementRotation();
    return;
  }

  if (hasTimeChanged()) {
    if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
      if (loop_time % 2 == 0) {
        putOnlineData("pos=" + getPosition() + (sending_error ? "&val=" + getValue() : ""), false, true);
      } else {
        saveTheState();
      }
    } else {
      getOnlineData();
    }
    if (light_delay > 0) {
      if (--light_delay == 0) {
        automaticSettings(true);
      }
      automaticSettings(false);
    } else {
      automaticSettings();
    }
  }

  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    rotation();
    if (destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
      putOnlineData("pos=" + getPosition());
      setStepperOff();
      if (LittleFS.exists("/resume.txt")) {
        LittleFS.remove("/resume.txt");
      }
      if (wings != 0 && (fixit1 != 0 || fixit2 != 0 || fixit3 != 0)) {
        calibration(0, false, true);
      }
    }
  }
}


String toPercentages(int value, int steps) {
  return String(value > 0 && steps > 0 ? (int)round((value + 0.0) * 100 / steps) : 0);
}

int toSteps(int value, int steps) {
  return value > 0 && steps > 0 ? round((value + 0.0) * steps / 100) : 0;
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
  if (json_object.containsKey("cycles1")) {
    cycles1 = json_object["cycles1"].as<int>();
  }
  if (json_object.containsKey("cycles2")) {
    cycles2 = json_object["cycles2"].as<int>();
  }
  if (json_object.containsKey("cycles3")) {
    cycles3 = json_object["cycles3"].as<int>();
  }
  if (json_object.containsKey("log")) {
    last_accessed_log = json_object["log"].as<int>();
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
  if (json_object.containsKey("overstep")) {
    overstep = json_object["overstep"].as<int>();
  }
  if (json_object.containsKey("twilight")) {
    twilight = json_object["twilight"].as<bool>();
  }
  if (json_object.containsKey("twilight_sensor")) {
    twilight_sensor = json_object["twilight_sensor"].as<bool>();
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

  // if (steps1 > 0 && steps2 > 0 && steps3 > 0) {
  //   separately = true;
  // } else {
  if (json_object.containsKey("separately")) {
    separately = json_object["separately"].as<bool>();
  }
  // }
  if (json_object.containsKey("inverted")) {
    inverted_sequence = json_object["inverted"].as<bool>();
  }
  if (json_object.containsKey("tandem")) {
    tandem = json_object["tandem"].as<bool>();
  }
  if (json_object.containsKey("fixit1")) {
    fixit1 = json_object["fixit1"].as<int>();
  }
  if (json_object.containsKey("fixit2")) {
    fixit2 = json_object["fixit2"].as<int>();
  }
  if (json_object.containsKey("fixit3")) {
    fixit3 = json_object["fixit3"].as<int>();
  }
  if (json_object.containsKey("lock")) {
    lock = json_object["lock"].as<bool>();
  }
  if (json_object.containsKey("location")) {
    geo_location = json_object["location"].as<String>();
    if (geo_location.length() > 2) {
      sun.setPosition(geo_location.substring(0, geo_location.indexOf("x")).toDouble(), geo_location.substring(geo_location.indexOf("x") + 1).toDouble(), 0);
    }
  }
  if (json_object.containsKey("dawn_delay")) {
    dawn_delay = json_object["dawn_delay"].as<int>();
  }
  if (json_object.containsKey("dusk_delay")) {
    dusk_delay = json_object["dusk_delay"].as<int>();
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

  if (smart_string.length() > 2) {
    json_object["smart"] = smart_string;
  }
  json_object["uprisings"] = uprisings;
  if (cycles1 > 0) {
    json_object["cycles1"] = cycles1;
  }
  if (cycles2 > 0) {
    json_object["cycles2"] = cycles2;
  }
  if (cycles3 > 0) {
    json_object["cycles3"] = cycles3;
  }
  if (last_accessed_log > 0) {
    json_object["log"] = last_accessed_log;
  }
  if (offset > 0) {
    json_object["offset"] = offset;
  }
  if (dst) {
    json_object["dst"] = dst;
  }
  if (sunset > 0) {
    json_object["sunset"] = sunset;
  }
  if (sunrise > 0) {
    json_object["sunrise"] = sunrise;
  }
  if (overstep > 0) {
    json_object["overstep"] = overstep;
  }
  if (twilight) {
    json_object["twilight"] = twilight;
  }
  if (twilight_sensor) {
    json_object["twilight_sensor"] = twilight_sensor;
  }
  if (boundary != default_boundary) {
    json_object["boundary"] = boundary;
  }
  if (reversed) {
    json_object["reversed"] = reversed;
  }
  // if ((steps1 > 0 && steps2 > 0 && steps3 > 0) || separately) {
  //   json_object["separately"] = (steps1 > 0 && steps2 > 0 && steps3 > 0) || separately;
  // }
  if (separately) {
    json_object["separately"] = separately;
  }
  if (inverted_sequence) {
    json_object["inverted"] = inverted_sequence;
  }
  if (tandem) {
    json_object["tandem"] = tandem;
  }
  if (fixit1 > 0) {
    json_object["fixit1"] = fixit1;
  }
  if (fixit2 > 0) {
    json_object["fixit2"] = fixit2;
  }
  if (fixit3 > 0) {
    json_object["fixit3"] = fixit3;
  }
  if (lock) {
    json_object["lock"] = lock;
  }
  if (!geo_location.equals(default_location)) {
    json_object["location"] = geo_location;
  }
  if (dusk_delay != 0) {
    json_object["dusk_delay"] = dusk_delay;
  }
  if (dawn_delay != 0) {
    json_object["dawn_delay"] = dawn_delay;
  }

  if (steps1 > 0) {
    json_object["steps1"] = steps1;
  }
  if (steps2 > 0) {
    json_object["steps2"] = steps2;
  }
  if (steps3 > 0) {
    json_object["steps3"] = steps3;
  }
  if (destination1 > 0) {
    json_object["destination1"] = destination1;
  }
  if (destination2 > 0) {
    json_object["destination2"] = destination2;
  }
  if (destination3 > 0) {
    json_object["destination3"] = destination3;
  }

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


String getBlindsDetail() {
  return "";
  // This function is only available with a ready-made iDom device.
}

String getValue() {
  return toPercentages(destination1, steps1) + "." + toPercentages(destination2, steps2) + "." + toPercentages(destination3, steps3);
}

String getPosition() {
  return toPercentages(actual1, steps1) + "." + toPercentages(actual2, steps2) + "."  + toPercentages(actual3, steps3);
}

String getSensorDetail(bool basic) {
  return light > -1 ? (String(light) + (twilight_sensor ? "t" : "") + (!basic && twilight_counter > 0 ? ("," + String(twilight_counter)) : "")) : "-1";
}

String getCycles() {
  return cycles1 + cycles2 + cycles3 > 0 ? String(cycles1) + "." + String(cycles2) + "." + String(cycles3) : "0";
}

String getFixit() {
  return fixit1 + fixit2 + fixit3 > 0 ? String(fixit1) + "." + String(fixit2) + "." + String(fixit3) : "0";
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

  ntpClient.begin();
  ntpClient.update();
  readData("{\"time\":" + String(ntpClient.getEpochTime()) + "}", false);
  getOfflineData();
}

void handshake() {
  if (server.hasArg("plain")) {
    readData(server.arg("plain"), true);
  }

  String reply = "\"id\":\"" + WiFi.macAddress()
  + "\",\"value\":\"" + getValue()
  + "\",\"pos\":\"" + getPosition()
  + "\",\"light\":\"" + getSensorDetail(false)
  + "\",\"twilight\":" + twilight
  + ",\"twilight_sensor\":" + twilight_sensor
  + ",\"sunset\":" + sunset
  + ",\"sunrise\":" + sunrise
  + ",\"next_sunset\":" + next_sunset
  + ",\"next_sunrise\":" + next_sunrise
  + ",\"sun_check\":" + last_sun_check
  + ",\"dusk_delay\":" + dusk_delay
  + ",\"dawn_delay\":" + dawn_delay
  + ",\"steps\":\"" + steps1 + "." + steps2 + "." + steps3
  + "\",\"boundary\":" + boundary
  + ",\"overstep\":" + overstep
  + ",\"reversed\":" + reversed
  + ",\"separately\":" + separately
  + ",\"inverted\":" + inverted_sequence
  + ",\"tandem\":" + tandem
  + ",\"fixit\":\"" + getFixit()
  + "\",\"lock\":" + lock
  + ",\"last_accessed_log\":" + last_accessed_log
  + ",\"location\":\"" + geo_location
  + "\",\"version\":" + version + "." + core_version
  + ",\"smart\":\"" + smart_string
  + "\",\"rtc\":" + RTCisrunning()
  + ",\"dst\":" + dst
  + ",\"offset\":" + offset
  + ",\"time\":" + (RTCisrunning() ? String(rtc.now().unixtime() - offset - (dst ? 3600 : 0)) : "0")
  + ",\"active\":" + String(RTCisrunning() ? (rtc.now().unixtime() - offset - (dst ? 3600 : 0) - start_time) : (millis() / 1000))
  + ",\"uprisings\":" + uprisings
  + ",\"cycles\":\"" + getCycles()
  + "\",\"offline\":" + offline;

  Serial.print("\nHandshake");
  server.send(200, "text/plain", "{" + reply + "}");
}

void requestForState() {
  String reply = "\"state1\":\"" + toPercentages(destination1, steps1) + (!measurement && destination1 != actual1 ? "^" + toPercentages(actual1, steps1) : "")
  + "\",\"state2\":\"" + toPercentages(destination2, steps2) + (!measurement && destination2 != actual2 ? "^" + toPercentages(actual2, steps2) : "")
  + "\",\"state3\":\"" + toPercentages(destination3, steps3) + (!measurement && destination3 != actual3 ? "^" + toPercentages(actual3, steps3) : "")
  + "\"";

  if (light > -1) {
    reply += ",\"light\":\"" + getSensorDetail(false) + "\"";
  }

  server.send(200, "text/plain", "{" + reply + "}");
}

void exchangeOfBasicData() {
  if (server.hasArg("plain")) {
    readData(server.arg("plain"), true);
  }

  String reply = "\"offset\":" + String(offset) + ",\"dst\":" + String(dst);

  if (RTCisrunning()) {
    reply += ",\"time\":" + String(rtc.now().unixtime() - offset - (dst ? 3600 : 0));
  }

  if (light > -1) {
    reply += ",\"light\":\"" + getSensorDetail(true) + "\"";
  }

  server.send(200, "text/plain", "{" + reply + "}");
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
    wings = 123;
    if (json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }

    calibration(json_object["calibrate"].as<int>(), json_object.containsKey("positioning"), false);
    return;
  }

  bool settings_change = false;
  bool details_change = false;
  String result = "";

  if (json_object.containsKey("offset")) {
    if (offset != json_object["offset"].as<int>()) {
      if (RTCisrunning() && !json_object.containsKey("time")) {
        rtc.adjust(DateTime((rtc.now().unixtime() - offset) + json_object["offset"].as<int>()));
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

      if (RTCisrunning() && !json_object.containsKey("time")) {
        rtc.adjust(DateTime(rtc.now().unixtime() + (dst ? 3600 : -3600)));
        note(dst ? "Summer time" : "Winter time");
      }
    }
  }

  if (json_object.containsKey("time")) {
    int new_time = json_object["time"].as<uint32_t>() + offset + (dst ? 3600 : 0);
    if (new_time > 1546304461) {
      if (RTCisrunning()) {
        if (abs(new_time - (int)rtc.now().unixtime()) > 60) {
          rtc.adjust(DateTime(new_time));
          note("Adjust time");
        }
      } else {
        rtc.adjust(DateTime(new_time));
        note("RTC begin");
        start_time = (millis() / 1000) + rtc.now().unixtime() - offset - (dst ? 3600 : 0);
        if (RTCisrunning()) {
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

    if ((destination1 != actual1 || destination2 != actual2 || destination3 != actual3)
    && (steps1 == 0 || (steps1 > 0 && destination1 == toSteps(new_value.substring(0, new_value.indexOf(".")).toInt(), steps1)))
    && (steps2 == 0 || (steps2 > 0 && destination2 == toSteps(new_value.substring(0, new_value.indexOf(".")).toInt(), steps2)))
    && (steps3 == 0 || (steps3 > 0 && destination3 == toSteps(new_value.substring(new_value.indexOf(".") + 1).toInt(), steps3)))) {
      if (steps1 > 0 && destination1 != actual1) {
        destination1 = actual1 - 1;
      }
      if (steps2 > 0 && destination2 != actual2) {
        destination2 = actual2 - 1;
      }
      if (steps3 > 0 && destination3 != actual3) {
        destination3 = actual3 - 1;
      }
    } else {
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
    }

    if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
      lock = (destination1 == steps1 && steps1 > 0) || (destination2 == steps2 && steps2 > 0) || (destination3 == steps3 && steps3 > 0);
      prepareRotation(per_wifi ? (json_object.containsKey("apk") ? "apk" : "local") : "cloud");
      if (per_wifi) {
        result += String(result.length() > 0 ? "&" : "") + "val=" + getValue();
      }
    }
  }

  if (json_object.containsKey("steps1") && actual1 == destination1) {
    if (steps1 != json_object["steps1"].as<int>()) {
      steps1 = json_object["steps1"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("steps2") && actual2 == destination2) {
    if (steps2 != json_object["steps2"].as<int>()) {
      steps2 = json_object["steps2"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("steps3") && actual3 == destination3) {
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

  if (json_object.containsKey("fixit")) {
    if (getFixit() != json_object["fixit"].as<String>()) {
      String new_fixit = json_object["fixit"].as<String>();

      if (strContains(new_fixit, ".")) {
        fixit1 = new_fixit.substring(0, new_fixit.indexOf(".")).toInt();
        new_fixit = new_fixit.substring(new_fixit.indexOf(".") + 1);

        fixit2 = new_fixit.substring(0, new_fixit.indexOf(".")).toInt();
        fixit3 = new_fixit.substring(new_fixit.indexOf(".") + 1).toInt();
      } else {
        fixit1 = new_fixit.toInt();
        fixit2 = 0;
        fixit3 = 0;
      }
      details_change = true;
    }
  }

  if (json_object.containsKey("dusk_delay")) {
    if (dusk_delay != json_object["dusk_delay"].as<int>()) {
      dusk_delay = json_object["dusk_delay"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("dawn_delay")) {
    if (dawn_delay != json_object["dawn_delay"].as<int>()) {
      dawn_delay = json_object["dawn_delay"].as<int>();
      details_change = true;
    }
  }

  if (json_object.containsKey("location")) {
    if (geo_location != json_object["location"].as<String>()) {
      geo_location = json_object["location"].as<String>();
      if (geo_location.length() < 2 && light == -1) {
        sunset = 0;
        sunrise = 0;
      }
      if (geo_location.length() > 2) {
        sun.setPosition(geo_location.substring(0, geo_location.indexOf("x")).toDouble(), geo_location.substring(geo_location.indexOf("x") + 1).toDouble(), 0);
      }
      getSunriseSunset(rtc.now());
      details_change = true;
    }
  }

  if (json_object.containsKey("light") && light == -1) {
    if (twilight_sensor != strContains(json_object["light"].as<String>(), "t")) {
      twilight_sensor = !twilight_sensor;
      settings_change = true;

      if (twilight_sensor ? dusk_delay > 0 : dawn_delay > 0) {
        light_delay = (twilight_sensor ? dusk_delay : dawn_delay) * 60; // * ((twilight_sensor ? dusk_delay : dawn_delay) < 0 ? -1 : 1))
      } else {
        automaticSettings(true);
      }
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
    putOnlineData(result, true);
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

  bool result = false;
  bool settings_change = false;
  bool sent = false;

  if (light > -1) {
    if (block_twilight_counter) {
      if (light < boundary - (boundary < 100 ? 0 : 50) || light > boundary + 50) {
        block_twilight_counter = false;
      }
    } else {
      if (twilight_sensor != (light < (twilight_sensor ? boundary - (boundary < 100 ? 0 : 50) : boundary + 50))) {
        change = true;
        if (++twilight_counter > 9 && (twilight_sensor ? light > boundary : light < boundary)) {
          twilight_sensor = !twilight_sensor;
          result = true;
          block_twilight_counter = true;
          twilight_counter = 0;
          if (RTCisrunning()) {
            DateTime now = rtc.now();
            overstep = now.unixtime() - offset - (dst ? 3600 : 0);
            putOnlineData("rise=" + String(sunrise) + "," + String(overstep) + (light > -1 ? "&light=" + getSensorDetail(false) : ""));
            sent = true;
          }
          settings_change = true;
          putMultiOfflineData("{\"light\":\"" + getSensorDetail(false) + "\"}");
        }
      } else {
        twilight_counter = 0;
      }
    }
  }

  if (RTCisrunning()) {
    DateTime now = rtc.now();
    int current_time = (now.hour() * 60) + now.minute();

    if ((light > -1 ? light < 100 : (geo_location.length() > 2 && current_time == next_sunset)) && ((now.unixtime() - offset - (dst ? 3600 : 0) - sunset > 72000) || (sunset < sunrise && current_time > 720 && current_time < 1380))) {
      sunset = now.unixtime() - offset - (dst ? 3600 : 0);
      putOnlineData("set=" + String(sunset) + (light > -1 ? "&light=" + getSensorDetail(false) : ""));
      settings_change = true;
      sent = true;
    }
    if ((light > -1 ? light > 100 : (geo_location.length() > 2 && current_time == next_sunrise)) && ((now.unixtime() - offset - (dst ? 3600 : 0) - sunrise > 72000) || (sunrise < sunset && current_time > 60 && current_time < 720))) {
      if (++daybreak_counter > 9 || geo_location.length() > 2) {
        sunrise = now.unixtime() - offset - (dst ? 3600 : 0);
        putOnlineData("rise=" + String(sunrise) + (overstep > 0 ? "," + String(overstep) : "") + (light > -1 ? "&light=" + getSensorDetail(false) : ""));
        settings_change = true;
        sent = true;
        daybreak_counter = 0;
      }
    } else {
      daybreak_counter = 0;
    }
  }

  if (settings_change) {
    saveSettings();
  }
  if (!sent && change) {
    putOnlineData("light=" + getSensorDetail(false), false, true);
  }

  if (result) {
    if (twilight_sensor ? dusk_delay > 0 : dawn_delay > 0) {
      light_delay = (twilight_sensor ? dusk_delay : dawn_delay) * 60; // * ((twilight_sensor ? dusk_delay : dawn_delay) < 0 ? -1 : 1))
      return false;
    } else {
      return true;
    }
  }

  return false;
}

bool automaticSettings() {
  return automaticSettings(hasTheLightChanged());
}

bool automaticSettings(bool light_changed) {
  DateTime now = rtc.now();
  int current_time = -1;

  if (RTCisrunning()) {
    current_time = (now.hour() * 60) + now.minute();

    if (geo_location.length() > 2) {
      if (now.second() == 0 && ((current_time > 181 && last_sun_check != now.day()) || next_sunset == -1 || next_sunrise == -1)) {
        getSunriseSunset(now);
      }

      if (next_sunset > -1 && next_sunrise > -1) {
        if ((current_time == (next_sunset + dusk_delay)) || (current_time == (next_sunrise + dawn_delay))) {
          if (twilight && current_time == (next_sunrise + dawn_delay)) {
            twilight = false;
            saveSettings();
          }
          if (!twilight && current_time == (next_sunset + dusk_delay)) {
            twilight = true;
            saveSettings();
          }
          lock = false;
        }
      }
    } else {
        if (current_time == 181) {
          lock = false;
        }
    }

    if (current_time == 120 || current_time == 180) {
      if (now.month() == 3 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 120 && !dst) {
        int new_time = now.unixtime() + 3600;
        rtc.adjust(DateTime(new_time));
        dst = true;
        note("Smart set to summer time");
        saveSettings();
        getSunriseSunset(now);
      }
      if (now.month() == 10 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 180 && dst) {
        int new_time = now.unixtime() - 3600;
        rtc.adjust(DateTime(new_time));
        dst = false;
        note("Smart set to winter time");
        saveSettings();
        getSunriseSunset(now);
      }
    }

    if (current_time == 60 && now.second() == 0) {
      ntpClient.update();
      readData("{\"time\":" + String(ntpClient.getEpochTime()) + "}", false);

      if (last_accessed_log++ > 14) {
        deactivationTheLog();
      }
    }

    if (current_time == (WiFi.localIP()[3] / 2) && now.second() == 0 && destination1 == actual1 && destination2 == actual2 && destination3 == actual3) {
      checkForUpdate(false);
    }
  }

  int i = -1;
  bool result = false;
  bool local_result;
  bool at_time_result;
  bool end_time_result;
  bool at_sunset_result;
  bool at_sunrise_result;
  bool at_dusk_result;
  bool at_dawn_result;
  int destination;
  String log = "";
  while (++i < smart_count) {
    local_result = false;
    at_time_result = false;
    end_time_result = false;
    at_sunset_result = false;
    at_sunrise_result = false;
    at_dusk_result = false;
    at_dawn_result = false;
    destination = -1;
    if (smart_array[i].enabled && (strContains(smart_array[i].days, "w") || (RTCisrunning() && strContains(smart_array[i].days, days_of_the_week[now.dayOfTheWeek()])))) {
      if (current_time > -1) {
        if (smart_array[i].at_time > -1) {
          at_time_result = smart_array[i].at_time == current_time && smart_array[i].access + 60 < now.unixtime();
          local_result |= at_time_result;
          if (!at_time_result && smart_array[i].any_required) {
            at_time_result = smart_array[i].at_time < current_time;
          }
        }
        if (smart_array[i].end_time > -1) {
          end_time_result = smart_array[i].end_time == current_time && smart_array[i].access + 60 < now.unixtime();
          local_result |= end_time_result;
          if (!end_time_result && smart_array[i].any_required) {
            if (smart_array[i].action > -1) {
              end_time_result = smart_array[i].end_time > current_time;
            } else {
              end_time_result = smart_array[i].end_time < current_time;
            }
          }
        }
        if (smart_array[i].at_sunset) {
          at_sunset_result = next_sunset > -1 && (next_sunset + dusk_delay) == current_time && twilight && smart_array[i].access + 60 < now.unixtime();
          local_result |= at_sunset_result;
          if (!at_sunset_result && smart_array[i].any_required) {
            at_sunset_result = next_sunset > -1 && (next_sunset + dusk_delay) < current_time && twilight;
          }
        }
        if (smart_array[i].at_sunrise) {
          at_sunrise_result = next_sunrise > -1 && (next_sunrise + dawn_delay) == current_time && !twilight && smart_array[i].access + 60 < now.unixtime();
          local_result |= at_sunrise_result;
          if (!at_sunrise_result && smart_array[i].any_required) {
            at_sunrise_result = next_sunrise > -1 && (next_sunrise + dawn_delay) < current_time && !twilight;
          }
        }
      }
      if (smart_array[i].at_dusk) {
        at_dusk_result = light_changed && twilight_sensor;
        local_result |= at_dusk_result;
        if (!at_dusk_result && smart_array[i].any_required) {
          at_dusk_result = twilight_sensor;
        }
      }
      if (smart_array[i].at_dawn) {
        at_dawn_result = light_changed && !twilight_sensor && !twilight && !lock;
        local_result |= at_dawn_result;
        if (!at_dawn_result && smart_array[i].any_required) {
          at_dawn_result = !twilight_sensor && !twilight && !lock;
        }
      }

      if (local_result) {
        if (!smart_array[i].any_required
        || ((smart_array[i].at_time == -1 || (smart_array[i].at_time > -1 && at_time_result)) && (smart_array[i].end_time == -1 || (smart_array[i].end_time > -1 && end_time_result))
        && (!smart_array[i].at_sunset || (smart_array[i].at_sunset && at_sunset_result)) && (!smart_array[i].at_sunrise || (smart_array[i].at_sunrise && at_sunrise_result))
        && (!smart_array[i].at_dusk || (smart_array[i].at_dusk && at_dusk_result)) && (!smart_array[i].at_dawn || (smart_array[i].at_dawn && at_dawn_result)))) {
          if (at_time_result) {
            destination = smart_array[i].action > -1 ? smart_array[i].action : 100;
            log = "time";
          }
          if (end_time_result) {
            destination = smart_array[i].action > -1 ? smart_array[i].action : 0;
            log = "time";
          }
          if (at_sunset_result) {
            destination = smart_array[i].action > -1 ? smart_array[i].action : 100;
            if (log.length() > 2) {
              log += " and ";
            }
            log += "sunset";
            if (dusk_delay != 0) {
              log += String(dusk_delay);
            }
          }
          if (at_sunrise_result) {
            destination = smart_array[i].action > -1 ? smart_array[i].action : 0;
            if (log.length() > 2) {
              log += " and ";
            }
            log += "sunrise";
            if (dawn_delay != 0) {
              log += String(dawn_delay);
            }
          }
          if (at_dusk_result) {
            destination = smart_array[i].action > -1 ? smart_array[i].action : 100;
            if (log.length() > 2) {
              log += " and ";
            }
            log += "dusk";
            if (dusk_delay > 0) {
              log += String(dusk_delay);
            }
          }
          if (at_dawn_result) {
            destination = smart_array[i].action > -1 ? smart_array[i].action : 0;
            if (log.length() > 2) {
              log += " and ";
            }
            log += "dawn";
            if (dawn_delay > 0) {
              log += String(dawn_delay);
            }
          }
        }
        if (destination > -1) {
          if (strContains(smart_array[i].wing, "1")) {
            destination1 = toSteps(destination, steps1);
          }
          if (strContains(smart_array[i].wing, "2")) {
            destination2 = toSteps(destination, steps2);
          }
          if (strContains(smart_array[i].wing, "3")) {
            destination3 = toSteps(destination, steps3);
          }
          result |= true;
          smart_array[i].access = now.unixtime();
          log = (smart_array[i].action > -1 ? String(destination) + "%" : destination == 100 ? "Lowering" : "Lifting") + (smart_array[i].any_required ? " after " : " at ") + log;
        }
      }
    }
  }

  if (result && (destination1 != actual1 || destination2 != actual2 || destination3 != actual3)) {
    note(log);
    prepareRotation("smart");
    putOnlineData("val=" + getValue());
  }

  return result;
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
        smart_array[smart_count].at_time = single_smart_string.substring(0, single_smart_string.indexOf("_")).toInt();
        single_smart_string = single_smart_string.substring(single_smart_string.indexOf("_") + 1);
      } else {
        smart_array[smart_count].at_time = -1;
      }

      if (strContains(single_smart_string, "-")) {
        smart_array[smart_count].end_time = single_smart_string.substring(single_smart_string.indexOf("-") + 1).toInt();
        single_smart_string = single_smart_string.substring(0, single_smart_string.indexOf("-"));
      } else {
        smart_array[smart_count].end_time = -1;
      }

      if (isStringDigit(single_smart_string.substring(0, single_smart_string.indexOf(String(smart_prefix))))) {
        smart_array[smart_count].action = single_smart_string.substring(0, single_smart_string.indexOf(String(smart_prefix))).toInt();
        single_smart_string = single_smart_string.substring(single_smart_string.indexOf(String(smart_prefix)));
      } else {
        smart_array[smart_count].action = -1;
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

      smart_array[smart_count].at_sunset = strContains(single_smart_string, "n");
      smart_array[smart_count].at_sunrise = strContains(single_smart_string, "d");
      smart_array[smart_count].at_dusk = strContains(single_smart_string, "<") || strContains(single_smart_string, "z");
      smart_array[smart_count].at_dawn = strContains(single_smart_string, ">") || strContains(single_smart_string, "z");
      smart_array[smart_count].any_required = strContains(single_smart_string, "&");

      smart_array[smart_count].access = 0;

      smart_count++;
    }
  }
  note("Smart contains " + String(smart_count) + " of " + String(smart_prefix));
}

void setMin() {
  wings = 123;
  if (server.hasArg("plain")) {
    DynamicJsonDocument json_object(1024);
    deserializeJson(json_object, server.arg("plain"));

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  if (strContains(wings, "1") && steps1 > 0) {
    destination1 = 0;
    actual1 = 0;
  }
  if (strContains(wings, "2") && steps2 > 0) {
    destination2 = 0;
    actual2 = 0;
  }
  if (strContains(wings, "3") && steps3 > 0) {
    destination3 = 0;
    actual3 = 0;
  }

  saveSettings();
  server.send(200, "text/plain", "Done");
  putOnlineData("val=" + getValue() + "&pos=" + getPosition());
}

void setMax() {
  wings = 123;
  if (server.hasArg("plain")) {
    DynamicJsonDocument json_object(1024);
    deserializeJson(json_object, server.arg("plain"));

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  if (strContains(wings, "1") && steps1 > 0) {
    destination1 = steps1;
    actual1 = steps1;
  }
  if (strContains(wings, "2") && steps2 > 0) {
    destination2 = steps2;
    actual2 = steps2;
  }
  if (strContains(wings, "3") && steps3 > 0) {
    destination3 = steps3;
    actual3 = steps3;
  }

  saveSettings();
  server.send(200, "text/plain", "Done");
  putOnlineData("val=" + getValue() + "&pos=" + getPosition());
}

void setAsMax() {
  wings = 123;
  if (server.hasArg("plain")) {
    DynamicJsonDocument json_object(1024);
    deserializeJson(json_object, server.arg("plain"));

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  if (strContains(wings, "1") && steps1 > 0) {
    steps1 = actual1;
    destination1 = actual1;
  }
  if (strContains(wings, "2") && steps2 > 0) {
    steps2 = actual2;
    destination2 = actual2;
  }
  if (strContains(wings, "3") && steps3 > 0) {
    steps3 = actual3;
    destination3 = actual3;
  }

  saveSettings();
  server.send(200, "text/plain", "Done");
  putOnlineData("val=" + getValue() + "&pos=" + getPosition() + "&detail=" + getBlindsDetail());
}

void initiateTheLightSensor() {
  if (light == -1) {
    light = 100;
    twilight_sensor = twilight;
  }
  server.send(200, "text/plain", "Done");
}

void deactivateTheLightSensor() {
  light = -1;
  twilight_sensor = false;
  if (geo_location.length() < 2) {
    sunrise = 0;
    sunset = 0;
  }
  saveSettings();
  introductionToServer();

  server.send(200, "text/plain", "Done");
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
  putOnlineData("val=" + getValue() + "&pos=" + getPosition() + "&detail=" + getBlindsDetail());

  server.send(200, "text/plain", "Done");
}


void setStepperOff() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(bipolar_enable_pin[i], HIGH);
  }
  digitalWrite(bipolar_direction_pin, LOW);
  digitalWrite(bipolar_step_pin, LOW);
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
  wings = 0;
  if (fixit1 != 0 || fixit2 != 0 || fixit3 != 0) {
    wings += (String(fixit1 != 0 && steps1 > 0 && destination1 == 0 && actual1 == steps1 ? "1" : "")
      + String(fixit2 != 0  && steps2 > 0 && destination2 == 0 && actual2 == steps2 ? "2" : "")
      + String(fixit3 != 0 && steps3 > 0 && destination3 == 0 && actual3 == steps3 ? "3" : "")).toInt();
  }
  note("Movement (" + orderer + "): " + logs);

  if (steps1 > 0 && destination1 == 0 && actual1 == steps1) {
    cycles1++;
    if (tandem) {
      cycles2++;
    }
  }
  if (!tandem) {
    if (steps2 > 0 && destination2 == 0 && actual2 == steps2) {
      cycles2++;
    }
    if (steps3 > 0 && destination3 == 0 && actual3 == steps3) {
      cycles3++;
    }
  }

  saveSettings();
  saveTheState();
}

void calibration(int set, bool positioning, bool fixit) {
  if (destination1 != actual1 || destination2 != actual2 || destination3 != actual3) {
    return;
  }

  bool settings_change = false;
  String logs = "";

  if (strContains(wings, "1")) {
    if (actual1 == 0 || positioning) {
      actual1 -= (fixit ? fixit1 * -1 : set) / 2;
      logs += "\n " + String(tandem ? "tandem" : "1") + " by " + String(fixit ? fixit1 : set) + " steps.";
    } else
      if (actual1 == steps1) {
        steps1 += set / 2;
        destination1 = steps1;
        settings_change = true;
        logs += "\n " + String(tandem ? "tandem" : "1") + " by " + String(set) + " steps. Steps set at " + String(steps1) + ".";
      }
  }
  if (!tandem) {
    if (strContains(wings, "2")) {
      if (actual2 == 0 || positioning) {
        actual2 -= (fixit ? fixit2 * -1 : set) / 2;
        logs += "\n 2 by " + String(fixit ? fixit2 : set) + " steps.";
      } else
        if (actual2 == steps2) {
          steps2 += set / 2;
          destination2 = steps2;
          settings_change = true;
          logs += "\n 2 by " + String(set) + " steps. Steps set at " + String(steps2) + ".";
        }
    }
    if (strContains(wings, "3")) {
      if (actual3 == 0 || positioning) {
        actual3 -= (fixit ? fixit3 * -1 : set) / 2;
        logs += "\n 3 by " + String(fixit ? fixit3 : set) + " steps.";
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
    putOnlineData("detail=" + getBlindsDetail());
  } else {
    if (fixit) {
      note("Fixit: " + logs);
    } else {
      note("Calibration: " + logs);
    }
  }
  wings = 0;
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
