#include "core.h"

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  LittleFS.begin();
  Wire.begin();

  keep_log = LittleFS.exists("/log.txt");

  #ifdef physical_clock
    rtc.begin();
    note("iDom Blinds " + String(version) + "." + String(core_version));
  #else
    note("iDom Blinds " + String(version) + "." + String(core_version) + "wo");
  #endif

  sprintf(host_name, "blinds_%s", String(WiFi.macAddress()).c_str());
  WiFi.hostname(host_name);

  if (!readSettings(0)) {
    delay(1000);
    readSettings(1);
  }
  resume();

  if (RTCisrunning()) {
    start_u_time = rtc.now().unixtime() - offset - (dst ? 3600 : 0);
  }

  light_sensor = analogRead(light_sensor_pin);
  has_a_sensor = (light_sensor > boundary || (dawn_u_time > 0 && dusk_u_time > 0 && light_sensor > 8));
  if (has_a_sensor) {
    sensor_twilight = light_sensor < boundary;
  } else {
    light_sensor = -1;
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
    if (destination[0] == actual[0] && destination[1] == actual[1] && destination[2] == actual[2]) {
      ArduinoOTA.handle();
    }
    server.handleClient();
    MDNS.update();
  } else {
    if (!auto_reconnect) {
      connectingToWifi(true);
    }
    cancelMeasurement();
  }

  if (measurement) {
    measurementRotation();
    return;
  }

  if (hasTimeChanged()) {
    if (destination[0] != actual[0] || destination[1] != actual[1] || destination[2] != actual[2]) {
      if (loop_u_time % 2 == 0) {
        if (loop_u_time % 4 == 0) {
          smartAction(5, false);
        }
      } else {
        saveTheState();
        automation();
      }
    } else {
      automation();
    }
  }

  if (destination[0] != actual[0] || destination[1] != actual[1] || destination[2] != actual[2]) {
    rotation();
    if (destination[0] == actual[0] && destination[1] == actual[1] && destination[2] == actual[2]) {
      setStepperOff();
      if (LittleFS.exists("/resume.txt")) {
        LittleFS.remove("/resume.txt");
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
  DeserializationError deserialization_error = deserializeJson(json_object, file);

  if (deserialization_error) {
    note(String(backup ? "Backup" : "Settings") + " error: " + String(deserialization_error.f_str()));
    file.close();
    return false;
  }

  file.seek(0);
  note("Reading the " + String(backup ? "backup" : "settings") + " file:\n " + file.readString());
  file.close();

  if (json_object.containsKey("log")) {
    last_accessed_log = json_object["log"].as<int>();
  }
  if (json_object.containsKey("ssid")) {
    ssid = json_object["ssid"].as<String>();
  }
  if (json_object.containsKey("password")) {
    password = json_object["password"].as<String>();
  }
  if (json_object.containsKey("uprisings")) {
    uprisings = json_object["uprisings"].as<int>() + 1;
  }
  if (json_object.containsKey("offset")) {
    offset = json_object["offset"].as<int>();
  }
  dst = json_object.containsKey("dst");
  if (json_object.containsKey("smart")) {
    if (json_object.containsKey("ver")) {
      setSmart(json_object["smart"].as<String>());
    } else {
      setSmart(oldSmart2NewSmart(json_object["smart"].as<String>()));
    }
  }
  smart_lock = json_object.containsKey("smart_lock");
  if (json_object.containsKey("location")) {
    geo_location = json_object["location"].as<String>();
    if (geo_location.length() > 2) {
      sun.setPosition(geo_location.substring(0, geo_location.indexOf("x")).toDouble(), geo_location.substring(geo_location.indexOf("x") + 1).toDouble(), 0);
    }
  }
  if (json_object.containsKey("sunset")) {
    sunset_u_time = json_object["sunset"].as<int>();
  }
  if (json_object.containsKey("sunrise")) {
    sunrise_u_time = json_object["sunrise"].as<int>();
  }
  sensor_twilight = json_object.containsKey("sensor_twilight");
  calendar_twilight = json_object.containsKey("twilight");
  if (json_object.containsKey("boundary")) {
    boundary = json_object["boundary"].as<int>();
  }
  reversed = json_object.containsKey("reversed");
  separately = json_object.containsKey("separately");
  tandem = json_object.containsKey("tandem");
  for (int i = 0; i < 3; i++) {
    if (json_object.containsKey("fixit")) {
      fixit[i] = json_object["fixit"][i].as<int>();
    } else {
      if (json_object.containsKey("fixit" + String(i + 1))) {
        fixit[i] = json_object["fixit" + String(i + 1)].as<int>();
      }
    }
    if (json_object.containsKey("cycles")) {
      cycles[i] = json_object["cycles"][i].as<int>();
    } else {
      if (json_object.containsKey("cycles" + String(i + 1))) {
        cycles[i] = json_object["cycles" + String(i + 1)].as<int>();
      }
    }
    if (json_object.containsKey("day_night")) {
      day_night[i] = json_object["day_night"][i].as<int>();
    } else {
      if (json_object.containsKey("day_night" + String(i + 1))) {
        day_night[i] = json_object["day_night" + String(i + 1)].as<int>();
      }
    }
    if (json_object.containsKey("steps")) {
      steps[i] = json_object["steps"][i].as<int>();
    } else {
      if (json_object.containsKey("steps" + String(i + 1))) {
        steps[i] = json_object["steps" + String(i + 1)].as<int>();
      }
    }
    if (json_object.containsKey("destination") || json_object.containsKey("destination" + String(i + 1))) {
      if (json_object.containsKey("destination")) {
        destination[i] = json_object["destination"][i].as<int>();
      } else {
        destination[i] = json_object["destination" + String(i + 1)].as<int>();
      }
      if (destination[i] < 0) {
        destination[i] = 0;
      }
      if (destination[i] > steps[i]) {
        destination[i] = steps[i];
      }
      actual[i] = destination[i];
    }
  }
  if (json_object.containsKey("dusk")) {
    dusk_u_time = json_object["dusk"].as<int>();
  }
  if (json_object.containsKey("dawn")) {
    dawn_u_time = json_object["dawn"].as<int>();
  }
  if (json_object.containsKey("overstep")) {
    overstep_u_time = json_object["overstep"].as<int>();
  }

  saveSettings(false);

  return true;
}

void saveSettings() {
  saveSettings(true);
}

void saveSettings(bool log) {
  DynamicJsonDocument json_object(1024);

  json_object["ver"] = String(version) + "." + String(core_version);
  if (last_accessed_log > 0) {
    json_object["log"] = last_accessed_log;
  }
  if (ssid.length() > 0) {
    json_object["ssid"] = ssid;
  }
  if (password.length() > 0) {
    json_object["password"] = password;
  }
  json_object["uprisings"] = uprisings;
  if (offset > 0) {
    json_object["offset"] = offset;
  }
  if (dst) {
    json_object["dst"] = dst;
  }
  if (smart_count > 0) {
    json_object["smart"] = getSmartString(true);
  }
  if (smart_lock) {
    json_object["smart_lock"] = smart_lock;
  }
  if (geo_location != default_location) {
    json_object["location"] = geo_location;
  }
  if (sunset_u_time > 0) {
    json_object["sunset"] = sunset_u_time;
  }
  if (sunrise_u_time > 0) {
    json_object["sunrise"] = sunrise_u_time;
  }
  if (sensor_twilight) {
    json_object["sensor_twilight"] = sensor_twilight;
  }
  if (calendar_twilight) {
    json_object["twilight"] = calendar_twilight;
  }
  if (boundary != default_boundary) {
    json_object["boundary"] = boundary;
  }
  if (reversed) {
    json_object["reversed"] = reversed;
  }
  if (separately) {
    json_object["separately"] = separately;
  }
  if (inverted_sequence) {
    json_object["inverted"] = inverted_sequence;
  }
  if (tandem) {
    json_object["tandem"] = tandem;
  }
  for (int i = 0; i < 3; i++) {
    if (fixit[0] + fixit[1] + fixit[2] > 0) {
      json_object["fixit"][i] = fixit[i];
    }
    if (cycles[0] + cycles[1] + cycles[2] > 0) {
      json_object["cycles"][i] = cycles[i];
    }
    if (day_night[0] + day_night[1] + day_night[2] > 0) {
      json_object["day_night"][i] = day_night[i];
    }
    if (steps[0] + steps[1] + steps[2] > 0) {
      json_object["steps"][i] = steps[i];
    }
    if (destination[0] + destination[1] + destination[2] > 0) {
      json_object["destination"][i] = destination[i];
    }
  }
  if (dusk_u_time > 0) {
    json_object["dusk"] = dusk_u_time;
  }
  if (dawn_u_time > 0) {
    json_object["dawn"] = dawn_u_time;
  }
  if (overstep_u_time > 0) {
    json_object["overstep"] = overstep_u_time;
  }

  if (writeObjectToFile("settings", json_object)) {
    if (log) {
      String log_text;
      serializeJson(json_object, log_text);
      note("Saving settings:\n " + log_text);
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

  StaticJsonDocument<100> json_object;
  DeserializationError deserialization_error = deserializeJson(json_object, file);
  file.close();

  if (deserialization_error) {
    note("Resume error: " + String(deserialization_error.c_str()));
    return;
  }

  for (int i = 0; i < 3; i++) {
    if (json_object.containsKey("actual")) {
      actual[i] = json_object["actual"][i].as<int>();
    } else {
      if (json_object.containsKey(String(i + 1))) {
        actual[i] = json_object[String(i + 1)].as<int>();
      }
    }
  }

  if (destination[0] != actual[0] || destination[1] != actual[1] || destination[2] != actual[2]) {
    String log_text = "";
    for (int i = 0; i < 3; i++) {
      if (destination[i] != actual[i]) {
        log_text += "\n " + String(i + 1) + " to " + String(destination[i] - actual[i]) + " steps to " + toPercentages(destination[i], steps[i]) + "%";
      }
    }
    note("Resume: " + log_text);
  } else {
    if (LittleFS.exists("/resume.txt")) {
      LittleFS.remove("/resume.txt");
    }
  }
}

void saveTheState() {
  StaticJsonDocument<100> json_object;

  for (int i = 0; i < 3; i++) {
    json_object["actual"][i] = actual[i];
  }

  writeObjectToFile("resume", json_object);
}


String getFixit() {
  return getFixit(";");
}

String getFixit(String separator) {
  return fixit[0] + fixit[1] + fixit[2] > 0 ? (String(fixit[0]) + separator + String(fixit[1]) + separator + String(fixit[2])) : "0";
}

String getCycles() {
  return getCycles(";");
}

String getCycles(String separator) {
  return cycles[0] + cycles[1] + cycles[2] > 0 ? (String(cycles[0]) + separator + String(cycles[1]) + separator + String(cycles[2])) : "0";
}

String getDayNight() {
  return getDayNight(";");
}

String getDayNight(String separator) {
  return day_night[0] + day_night[1] + day_night[2] > 0 ? (String(day_night[0]) + separator + String(day_night[1]) + separator + String(day_night[2])) : "0";
}

String getSteps() {
  return getSteps(";");
}

String getSteps(String separator) {
  return steps[0] + steps[1] + steps[2] > 0 ? (String(steps[0]) + separator + String(steps[1]) + separator + String(steps[2])) : "0";
}

String getValue() {
  return getValue(";");
}

String getValue(String separator) {
  return destination[0] + destination[1] + destination[2] > 0 ? (toPercentages(destination[0], steps[0]) + separator + toPercentages(destination[1], steps[1]) + separator + toPercentages(destination[2], steps[2])) : "0";
}

int getValue(int number) {
  return toPercentages(destination[number], steps[number]).toInt();
}

String getActual() {
  return getActual(false);
}

String getActual(bool complete) {
  return actual[0] + actual[1] + actual[2] > 0 || complete ? (toPercentages(actual[0], steps[0]) + ";" + toPercentages(actual[1], steps[1]) + ";" + toPercentages(actual[2], steps[2])) : "0";
}

String getSensorDetail(bool basic) {
  return has_a_sensor ? (String(light_sensor) + (sensor_twilight ? "t" : "") + (!basic && twilight_counter > 0 ? (";" + String(twilight_counter)) : "")) : "-1";
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
  server.on("/test/smartdetail", HTTP_GET, getSmartDetail);
  server.on("/test/smartdetail/raw", HTTP_GET, getRawSmartDetail);
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
  bool per_rest_client = false;

  if (server.hasArg("plain")) {
    readData(server.arg("plain"), true);
  } else {
    per_rest_client = true;
  }

  String reply = "\"id\":\"" + WiFi.macAddress() + "\"";
  reply += ",\"version\":" + String(version) + "." + String(core_version);
  reply += ",\"offline\":true";
  if (keep_log) {
    reply += ",\"last_accessed_log\":" + String(last_accessed_log);
  }
  if (start_u_time > 0) {
    reply += ",\"start\":" + String(start_u_time);
  } else {
    reply += ",\"active\":" + String(millis() / 1000);
  }
  reply += ",\"uprisings\":" + String(uprisings);
  if (offset > 0) {
    reply += ",\"offset\":" + String(offset);
  }
  if (dst) {
    reply += ",\"dst\":true";
  }
  if (RTCisrunning()) {
    #ifdef physical_clock
      reply += ",\"rtc\":true";
    #endif
    reply += ",\"time\":" + String(rtc.now().unixtime() - offset - (dst ? 3600 : 0));
  }
  if (smart_count > 0) {
    reply += ",\"smart\":\"" + getSmartString(true) + "\"";
  }
  if (smart_lock) {
    reply += ",\"smart_lock\":true";
  }
  if (geo_location.length() > 2) {
    reply += ",\"location\":\"" + geo_location + "\"";
  }
  if (last_sun_check > -1) {
    reply += ",\"sun_check\":" + String(last_sun_check);
  }
  if (next_sunset > -1) {
    reply += ",\"next_sunset\":" + String(next_sunset);
  }
  if (next_sunrise > -1) {
    reply += ",\"next_sunrise\":" + String(next_sunrise);
  }
  if (sunset_u_time > 0) {
    reply += ",\"sunset\":" + String(sunset_u_time);
  }
  if (sunrise_u_time > 0) {
    reply += ",\"sunrise\":" + String(sunrise_u_time);
  }
  if (light_sensor > -1) {
    reply += ",\"light\":" + String(light_sensor);
  }
  if (sensor_twilight) {
    reply += ",\"sensor_twilight\":true";
  }
  if (calendar_twilight) {
    reply += ",\"twilight\":true";
  }
  reply += ",\"boundary\":" + String(boundary);
  if (reversed) {
    reply += ",\"reversed\":true";
  }
  if (separately) {
    reply += ",\"separately\":true";
  }
  if (inverted_sequence) {
    reply += ",\"inverted\":true";
  }
  if (tandem) {
    reply += ",\"tandem\":true";
  }
  if (getFixit() != "0") {
    reply += ",\"fixit\":[" + getFixit(per_rest_client ? "," : ";")  + "]";
  }
  if (getCycles() != "0") {
    reply += ",\"cycles\":[" + getCycles(per_rest_client ? "," : ";") + "]";
  }
  if (getDayNight() != "0") {
    reply += ",\"day_night\":[" + getDayNight(per_rest_client ? "," : ";") + "]";
  }
  if (getSteps() != "0") {
    reply += ",\"steps\":[" + getSteps(per_rest_client ? "," : ";") + "]";
  }
  if (getValue() != "0") {
    reply += ",\"value\":[" + getValue(per_rest_client ? "," : ";") + "]";
  }
  if (getActual() != getValue()) {
    reply += ",\"pos\":[" + getActual(per_rest_client ? "," : ";") + "]";
  }
  if (has_a_sensor) {
    reply += ",\"has_a_sensor\":true";
  }
  if (dusk_u_time > 0) {
    reply += ",\"dusk\":" + String(dusk_u_time);
  }
  if (dawn_u_time > 0) {
    reply += ",\"dawn\":" + String(dawn_u_time);
  }
  if (overstep_u_time > 0) {
    reply += ",\"overstep\":" + String(overstep_u_time);
  }

  Serial.print("\nHandshake");
  server.send(200, "text/plain", "{" + reply + "}");
}

void requestForState() {
  String reply = "\"value\":[" + getValue() + "]";

  if (!measurement && getActual() != getValue()) {
    reply += ",\"pos\":[" + getActual() + "]";
  }

  if (has_a_sensor) {
    reply += ",\"light\":\"" + getSensorDetail(false) + "\"";
  }

  server.send(200, "text/plain", "{" + reply + "}");
}

void exchangeOfBasicData() {
  if (server.hasArg("plain")) {
    readData(server.arg("plain"), true);
  }

  String reply = "\"ip\":\"" + WiFi.localIP().toString() + "\"" + ",\"id\":\"" + WiFi.macAddress() + "\"";

  reply += ",\"offset\":" + String(offset) + ",\"dst\":" + String(dst);

  if (RTCisrunning()) {
    reply += ",\"time\":" + String(rtc.now().unixtime() - offset - (dst ? 3600 : 0));
  }

  if (has_a_sensor) {
    reply += ",\"light\":\"" + getSensorDetail(true) + "\"";
  }

  server.send(200, "text/plain", "{" + reply + "}");
}

void readData(const String& payload, bool per_wifi) {
  DynamicJsonDocument json_object(1024);
  DeserializationError deserialization_error = deserializeJson(json_object, payload);

  if (deserialization_error) {
    note("Read data error: " + String(deserialization_error.c_str()) + "\n" + payload);
    return;
  }

  if (json_object.containsKey("calibrate")) {
    wings = 123;
    if (json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }

    calibration(json_object["calibrate"].as<int>(), json_object.containsKey("positioning"));
    return;
  }

  bool settings_change = false;
  bool details_change = false;
  bool twilight_change = false;
  bool smart_change = false;

  if (json_object.containsKey("ip") && json_object.containsKey("id")) {
      for (int i = 0; i < devices_count; i++) {
        if (devices_array[i].ip == json_object["ip"].as<String>()) {
          devices_array[i].mac = json_object["id"].as<String>();
        }
      }
  }

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
    if (dst != strContains(json_object["dst"].as<String>(), 1)) {
      dst = !dst;
      settings_change = true;
      if (RTCisrunning() && !json_object.containsKey("time")) {
        rtc.adjust(DateTime(rtc.now().unixtime() + (dst ? 3600 : -3600)));
        note(dst ? "Summer time" : "Winter time");
      }
    }
  }

  if (json_object.containsKey("time")) {
    int new_u_time = json_object["time"].as<int>() + offset + (dst ? 3600 : 0);
    if (new_u_time > 1546304461) {
      if (RTCisrunning()) {
        if (abs(new_u_time - (int)rtc.now().unixtime()) > 60) {
          rtc.adjust(DateTime(new_u_time));
          note("Adjust time");
        }
      } else {
        #ifdef physical_clock
          rtc.adjust(DateTime(new_u_time));
        #else
          rtc.begin(DateTime(new_u_time));
        #endif
        note("RTC begin");
        start_u_time = (millis() / 1000) + rtc.now().unixtime() - offset - (dst ? 3600 : 0);
        if (RTCisrunning()) {
          details_change = true;
        }
      }
    }
  }

  if (json_object.containsKey("smart")) {
    if (getSmartString(true) != json_object["smart"].as<String>()) {
      setSmart(json_object["smart"].as<String>());
      settings_change = true;
      if (per_wifi) {
        smart_change = true;
      }
    }
  }

  if (json_object.containsKey("smart_lock")) {
    if (smart_lock != strContains(json_object["smart_lock"].as<String>(), 1)) {
      smart_lock = !smart_lock;
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("location")) {
    if (geo_location != json_object["location"].as<String>()) {
      geo_location = json_object["location"].as<String>();
      if (geo_location.length() > 2) {
        sun.setPosition(geo_location.substring(0, geo_location.indexOf("x")).toDouble(), geo_location.substring(geo_location.indexOf("x") + 1).toDouble(), 0);
      } else {
        last_sun_check = -1;
        next_sunset = -1;
        next_sunrise = -1;
        sunset_u_time = 0;
        sunrise_u_time = 0;
        calendar_twilight = false;
      }
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("boundary")) {
    if (boundary != json_object["boundary"].as<int>()) {
      boundary = json_object["boundary"].as<int>();
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("reversed")) {
    if (reversed != strContains(json_object["reversed"].as<String>(), 1)) {
      reversed = !reversed;
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("separately")) {
    if (separately != strContains(json_object["separately"].as<String>(), 1)) {
      separately = !separately;
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("inverted")) {
    if (inverted_sequence != strContains(json_object["inverted"].as<String>(), 1)) {
      inverted_sequence = !inverted_sequence;
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("tandem")) {
    if (tandem != strContains(json_object["tandem"].as<String>(), 1)) {
      tandem = !tandem;
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("fixit")) {
    if (getFixit() != json_object["fixit"].as<String>()) {
      if (strContains(json_object["fixit"].as<String>(), ";")) {
        if (steps[0] > 0) {
          fixit[0] = json_object["fixit"].as<String>().substring(0, json_object["fixit"].as<String>().indexOf(";")).toInt();
        }
        if (steps[1] > 0) {
          fixit[1] = json_object["fixit"].as<String>().substring(json_object["fixit"].as<String>().indexOf(";") + 1, json_object["fixit"].as<String>().lastIndexOf(";")).toInt();
        }
        if (steps[2] > 0) {
          fixit[2] = json_object["fixit"].as<String>().substring(json_object["fixit"].as<String>().lastIndexOf(";") + 1).toInt();
        }
      } else {
        for (int i = 0; i < 3; i++) {
          if (steps[i] > 0) {
            fixit[i] = json_object["fixit"].as<int>();
          }
        }
      }
      settings_change = true;
      details_change = true;
    }
  }

  if (json_object.containsKey("day_night")) {
    if (getDayNight() != json_object["day_night"].as<String>()) {
      if (strContains(json_object["day_night"].as<String>(), ";")) {
        if (steps[0] > 0) {
          day_night[0] = json_object["day_night"].as<String>().substring(0, json_object["day_night"].as<String>().indexOf(";")).toInt();
        }
        if (steps[1] > 0) {
          day_night[1] = json_object["day_night"].as<String>().substring(json_object["day_night"].as<String>().indexOf(";") + 1, json_object["day_night"].as<String>().lastIndexOf(";")).toInt();
        }
        if (steps[2] > 0) {
          day_night[2] = json_object["day_night"].as<String>().substring(json_object["day_night"].as<String>().lastIndexOf(";") + 1).toInt();
        }
      } else {
        for (int i = 0; i < 3; i++) {
          if (steps[i] > 0) {
            day_night[i] = json_object["day_night"].as<int>();
          }
        }
      }
      settings_change = true;
      details_change = true;
    }
  }

  for (int i = 0; i < 3; i++) {
    if (json_object.containsKey("steps" + String(i + 1)) && actual[i] == destination[i] && (!tandem || i == 0)) {
      if (steps[i] != json_object["steps" + String(i + 1)].as<int>()) {
        steps[i] = json_object["steps" + String(i + 1)].as<int>();
        settings_change = true;
        details_change = true;
      }
    }
  }

  if (json_object.containsKey("light") && !has_a_sensor) {
    if (sensor_twilight != strContains(json_object["light"].as<String>(), "t")) {
      sensor_twilight = !sensor_twilight;
      twilight_change = true;
      settings_change = true;
      if (RTCisrunning()) {
        int current_time = (rtc.now().hour() * 60) + rtc.now().minute();
        if (sensor_twilight) {
          if (abs(current_time - dusk_time) > 60) {
            dusk_time = current_time;
          }
        } else {
          if (abs(current_time - dawn_time) > 60) {
            dawn_time = current_time;
          }
        }
      }
    }
    if (strContains(json_object["light"].as<String>(), "t")) {
		  light_sensor = json_object["light"].as<String>().substring(0, json_object["light"].as<String>().indexOf("t")).toInt();
    } else {
		  light_sensor = json_object["light"].as<int>();
    }
  }

  if (json_object.containsKey("val") || json_object.containsKey("blinds")) {
    String new_value = json_object.containsKey("val") ? json_object["val"].as<String>() : json_object["blinds"].as<String>();

    if ((destination[0] != actual[0] || destination[1] != actual[1] || destination[2] != actual[2])
    && !settings_change && !details_change && !smart_change && per_wifi && !json_object.containsKey("apk")
    && (steps[0] == 0 || (steps[0] > 0 && destination[0] == toSteps((strContains(new_value, ";") ? new_value.substring(0, new_value.indexOf(";")).toInt() : new_value.toInt()), steps[0])))
    && (steps[1] == 0 || (steps[1] > 0 && destination[1] == toSteps((strContains(new_value, ";") ? new_value.substring(new_value.indexOf(";") + 1, new_value.lastIndexOf(";")).toInt() : new_value.toInt()), steps[1])))
    && (steps[2] == 0 || (steps[2] > 0 && destination[2] == toSteps((strContains(new_value, ";") ? new_value.substring(new_value.lastIndexOf(";") + 1).toInt() : new_value.toInt()), steps[2])))) {
      for (int i = 0; i < 3; i++) {
        if (steps[i] > 0 && destination[i] != actual[i]) {
          destination[i] = actual[i] - 1;
        }
      }
    } else {
      if (steps[0] > 0) {
        destination[0] = toSteps((strContains(new_value, ";") ? new_value.substring(0, new_value.indexOf(";")).toInt() : new_value.toInt()), steps[0]);
      }
      if (steps[1] > 0) {
        destination[1] = toSteps((strContains(new_value, ";") ? new_value.substring(new_value.indexOf(";") + 1, new_value.lastIndexOf(";")).toInt() : new_value.toInt()), steps[1]);
      }
      if (steps[2] > 0) {
        destination[2] = toSteps((strContains(new_value, ";") ? new_value.substring(new_value.lastIndexOf(";") + 1).toInt() : new_value.toInt()), steps[2]);
      }
    }
    if (destination[0] != actual[0] || destination[1] != actual[1] || destination[2] != actual[2]) {
      if (smart_lock != ((destination[0] == steps[0] && steps[0] > 0) || (destination[1] == steps[1] && steps[1] > 0) || (destination[2] == steps[2] && steps[2] > 0))) {
        smart_lock = !smart_lock;
        settings_change = true;
        details_change = true;
      }
    }
  }

  if (settings_change) {
    note("Received the data:\n " + payload);
    saveSettings();
  }
  if (json_object.containsKey("light") && !has_a_sensor) {
    smartAction(0, twilight_change);
  }
  if (json_object.containsKey("location") && RTCisrunning()) {
    getSunriseSunset(rtc.now());
  }
  if (json_object.containsKey("val") || json_object.containsKey("blinds")) {
    if (destination[0] != actual[0] || destination[1] != actual[1] || destination[2] != actual[2]) {
      prepareRotation(per_wifi ? (json_object.containsKey("apk") ? "apk" : "local") : "cloud");
    }
  }
}

void automation() {
  if (!RTCisrunning()) {
    smartAction();
    return;
  }

  DateTime now = rtc.now();
  int current_time = (now.hour() * 60) + now.minute();

  if (now.second() == 0) {
    if (current_time == 60) {
      ntpClient.update();
      readData("{\"time\":" + String(ntpClient.getEpochTime()) + "}", false);

      if (last_accessed_log++ > 14) {
        deactivationTheLog();
      }
    }
  }

  if (current_time == 120 || current_time == 180) {
    if (now.month() == 3 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 120 && !dst) {
      int new_u_time = now.unixtime() + 3600;
      rtc.adjust(DateTime(new_u_time));
      dst = true;
      note("Setting summer time");
      saveSettings();
      getSunriseSunset(now);
    }
    if (now.month() == 10 && now.day() > 24 && days_of_the_week[now.dayOfTheWeek()][0] == 's' && current_time == 180 && dst) {
      int new_u_time = now.unixtime() - 3600;
      rtc.adjust(DateTime(new_u_time));
      dst = false;
      note("Setting winter time");
      saveSettings();
      getSunriseSunset(now);
    }
  }

  if (geo_location.length() < 2) {
    if (current_time == 181) {
      smart_lock = false;
      saveSettings();
    }
  } else {
    if (now.second() == 0 && ((current_time > 181 && last_sun_check != now.day()) || next_sunset == -1 || next_sunrise == -1)) {
      getSunriseSunset(now);
    }

    if (next_sunset > -1 && next_sunrise > -1) {
      if ((!calendar_twilight && current_time == next_sunset) || (calendar_twilight && current_time == next_sunrise)) {
        if (current_time == next_sunset) {
          calendar_twilight = true;
          sunset_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
        }
        if (current_time == next_sunrise) {
          calendar_twilight = false;
          sunrise_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
        }
        smart_lock = false;
        saveSettings();
      }
    }
  }

  smartAction();
}

int hasTheLightChanged() {
  if (loop_u_time % 60 != 0) {
    return -1;
  }

  int new_light_value = analogRead(light_sensor_pin);
  bool result = false;
  bool twilight_change = false;

  if (has_a_sensor) {
    if (abs(light_sensor - new_light_value) > (new_light_value < 30 ? 5 : 20)) {
      light_sensor = new_light_value;
      result = true;
    }
  } else {
    if (new_light_value > boundary || (dawn_u_time > 0 && dusk_u_time > 0 && new_light_value > 8)) {
      light_sensor = new_light_value;
      has_a_sensor = true;
      result = true;
    }
  }

  bool settings_change = false;

  if (has_a_sensor) {
    if (block_twilight_counter) {
      if (light_sensor < boundary - (boundary < 100 ? 0 : 50) || light_sensor > boundary + 50) {
        block_twilight_counter = false;
      }
    } else {
      if (sensor_twilight != (light_sensor < (sensor_twilight ? boundary - (boundary < 100 ? 0 : 50) : boundary + 50))) {
        if (++twilight_counter > 9 && (sensor_twilight ? light_sensor > boundary : light_sensor < boundary)) {
          sensor_twilight = !sensor_twilight;
          twilight_change = true;
          result = true;
          block_twilight_counter = true;
          twilight_counter = 0;
          if (RTCisrunning()) {
            overstep_u_time = rtc.now().unixtime() - offset - (dst ? 3600 : 0);
          }
          settings_change = true;
        }
      } else {
        twilight_counter = 0;
      }
    }

    if (RTCisrunning()) {
      DateTime now = rtc.now();
      int current_time = (now.hour() * 60) + now.minute();

      if (light_sensor < 100 && ((now.unixtime() - offset - (dst ? 3600 : 0) - dawn_u_time > 72000) || (dawn_u_time < dusk_u_time && current_time > 720 && current_time < 1380))) {
        dawn_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
        dawn_time = current_time;
        settings_change = true;
      }
      if (light_sensor > 100 && ((now.unixtime() - offset - (dst ? 3600 : 0) - dusk_u_time > 72000) || (dusk_u_time < dawn_u_time && current_time > 60 && current_time < 720))) {
        if (++daybreak_counter > 9) {
          dusk_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
          dusk_time = current_time;
          daybreak_counter = 0;
          settings_change = true;
        }
      } else {
        daybreak_counter = 0;
      }
    }

    if (settings_change) {
      saveSettings();
    }
    if (result) {
      putMultiOfflineData("{\"light\":\"" + getSensorDetail(true) + "\"}", false);
    }
  }

  if (has_a_sensor && result) {
    smartAction(0, twilight_change);
    return 0;
  } else {
    return -1;
  }
}

void smartAction() {
  if (hasTheLightChanged() == -1) {
    smartAction(-1, false);
  }
}


void setMin() {
  wings = 123;
  if (server.hasArg("plain")) {
    StaticJsonDocument<50> json_object;
    deserializeJson(json_object, server.arg("plain").c_str());

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  for (int i = 0; i < 3; i++) {
    if (strContains(wings, i + 1) && steps[i] > 0) {
      destination[i] = 0;
      actual[i] = 0;
    }
  }
  wings = 0;

  saveSettings();
  server.send(200, "text/plain", "Done");
}

void setMax() {
  wings = 123;
  if (server.hasArg("plain")) {
    StaticJsonDocument<50> json_object;
    deserializeJson(json_object, server.arg("plain").c_str());

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  for (int i = 0; i < 3; i++) {
    if (strContains(wings, i + 1) && steps[i] > 0) {
      destination[i] = steps[i];
      actual[i] = steps[i];
    }
  }
  wings = 0;

  saveSettings();
  server.send(200, "text/plain", "Done");
}

void setAsMax() {
  wings = 123;
  if (server.hasArg("plain")) {
    StaticJsonDocument<50> json_object;
    deserializeJson(json_object, server.arg("plain").c_str());

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  for (int i = 0; i < 3; i++) {
    if (strContains(wings, i + 1) && steps[i] > 0) {
      steps[i] = actual[i];
      destination[i] = actual[i];
    }
  }
  wings = 0;

  saveSettings();
  server.send(200, "text/plain", "Done");
}

void initiateTheLightSensor() {
  if (!has_a_sensor) {
    has_a_sensor = true;
    light_sensor = boundary;
    sensor_twilight = calendar_twilight;
  }
  server.send(200, "text/plain", "Done");
}

void deactivateTheLightSensor() {
  if (has_a_sensor) {
    has_a_sensor = false;
    light_sensor = -1;
    sensor_twilight = false;
    dusk_u_time = 0;
    dawn_u_time = 0;
    overstep_u_time = 0;
    saveSettings();
  }
  server.send(200, "text/plain", "Done");
}

void makeMeasurement() {
  if (measurement) {
    return;
  }

  wings = 123;
  if (server.hasArg("plain")) {
    StaticJsonDocument<50> json_object;
    deserializeJson(json_object, server.arg("plain").c_str());

    if (!json_object.isNull() && json_object.containsKey("wings")) {
      wings = json_object["wings"].as<int>();
    }
  }

  for (int i = 0; i < 3; i++) {
    if (strContains(wings, i + 1) && !(destination[i] == 0 || actual[i] == 0)) {
      server.send(200, "text/plain", "Cannot execute");
      return;
    }
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
  wings = 0;

  server.send(200, "text/plain", "Done");
}

void endMeasurement() {
  if (!measurement) {
    return;
  }

  measurement = false;
  setStepperOff();

  for (int i = 0; i < 3; i++) {
    if (strContains(wings, i + 1)) {
      steps[i] = actual[i];
      destination[i] = actual[i];
    }
  }
  wings = 0;

  note("Measurement completed");
  saveSettings();
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
  String log_text = "";

  for (int i = 0; i < 3; i++) {
    if (steps[i] > 0 && destination[i] != actual[i] && (!tandem || i == 0)) {
      if (actual[i] == steps[i] && destination[i] == 0) {
        if (fixit[i] != 0) {
          actual[i] += fixit[i];
        }
        cycles[i]++;
        if (tandem) {
          cycles[1]++;
        }
      }
      log_text += "\n " + (tandem ? "tandem" : String(i + 1)) + " by " + String(destination[i] - actual[i]) + " steps to " + toPercentages(destination[i], steps[i]) + "%";
    }
  }

  if (log_text.length() > 0) {
    note("Movement (" + orderer + "): " + log_text);
    saveTheState();
    saveSettings();
  }
}

void calibration(int set, bool positioning) {
  if (destination[0] != actual[0] || destination[1] != actual[1] || destination[2] != actual[2]) {
    wings = 0;
    return;
  }

  bool settings_change = false;
  String log_text = "";

  for (int i = 0; i < 3; i++) {
    if (strContains(wings, i + 1) && (!tandem || i == 0)) {
      if (actual[i] == 0 || positioning) {
        actual[i] -= set / 2;
        log_text += "\n " + (tandem ? "tandem" : String(i + 1)) + " by " + String(set) + " steps.";
      } else {
        if (actual[i] == steps[i]) {
          steps[i] += set / 2;
          destination[i] = steps[i];
          settings_change = true;
          log_text += "\n " + (tandem ? "tandem" : String(i + 1)) + " by " + String(set) + " steps. Steps set at " + String(steps[i]) + ".";
        }
      }
    }
  }
  wings = 0;

  note("Calibration: " + log_text);
  saveTheState();

  if (settings_change) {
    saveSettings();
  }
}

void measurementRotation() {
  for (int i = 0; i < 3; i++) {
    if (strContains(wings, i + 1) && (!tandem || i == 0)) {
      digitalWrite(bipolar_enable_pin[i], LOW);
      if (tandem) {
        digitalWrite(bipolar_enable_pin[1], LOW);
      }
      actual[i]++;
    }
  }

  digitalWrite(bipolar_step_pin, HIGH);
  digitalWrite(bipolar_step_pin, LOW);
  delay(4);
}

void rotation() {
  if (destination[0] != actual[0] && (!inverted_sequence ||
    ((destination[1] == actual[1] || (!separately && ((destination[1] > actual[1] && destination[0] > actual[0]) || (destination[1] < actual[1] && destination[0] < actual[0])))) &&
    (destination[2] == actual[2] || (!separately && ((destination[2] > actual[2] && destination[0] > actual[0]) || (destination[2] < actual[2] && destination[0] < actual[0]))))))) {
    digitalWrite(bipolar_direction_pin, destination[0] < actual[0] ? reversed : !reversed);
    digitalWrite(bipolar_enable_pin[0], LOW);
    if (tandem) {
      digitalWrite(bipolar_enable_pin[1], LOW);
    }
    if (destination[0] > actual[0]) {
      actual[0]++;
    } else {
      actual[0]--;
    }
  } else {
    digitalWrite(bipolar_enable_pin[0], HIGH);
    if (tandem) {
      digitalWrite(bipolar_enable_pin[1], HIGH);
    }
  }

  if (!tandem) {
    if (destination[1] != actual[1] && (inverted_sequence ?
      (destination[2] == actual[2] || (!separately && ((destination[2] > actual[2] && destination[1] > actual[1]) || (destination[2] < actual[2] && destination[1] < actual[1])))) :
      (destination[0] == actual[0] || (!separately && ((destination[0] > actual[0] && destination[1] > actual[1]) || (destination[0] < actual[0] && destination[1] < actual[1])))))) {
      digitalWrite(bipolar_direction_pin, destination[1] < actual[1] ? reversed : !reversed);
      digitalWrite(bipolar_enable_pin[1], LOW);
      if (destination[1] > actual[1]) {
        actual[1]++;
      } else {
        actual[1]--;
      }
    } else {
      digitalWrite(bipolar_enable_pin[1], HIGH);
    }

    if (destination[2] != actual[2] && (inverted_sequence ||
      ((destination[0] == actual[0] || (!separately && ((destination[0] > actual[0] && destination[2] > actual[2]) || (destination[0] < actual[0] && destination[2] < actual[2])))) &&
      (destination[1] == actual[1] || (!separately && ((destination[1] > actual[1] && destination[2] > actual[2]) || (destination[1] < actual[1] && destination[2] < actual[2]))))))) {
      digitalWrite(bipolar_direction_pin, destination[2] < actual[2] ? reversed : !reversed);
      digitalWrite(bipolar_enable_pin[2], LOW);
      if (destination[2] > actual[2]) {
        actual[2]++;
      } else {
        actual[2]--;
      }
    } else {
      digitalWrite(bipolar_enable_pin[2], HIGH);
    }
  }

  digitalWrite(bipolar_step_pin, HIGH);
  digitalWrite(bipolar_step_pin, LOW);
  delay(4);
}
