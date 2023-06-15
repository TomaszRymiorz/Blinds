#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <sunset.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "main.h"

#ifdef physical_clock
  RTC_DS1307 rtc;
#else
  RTC_Millis rtc;
#endif

ESP8266WebServer server(80);
WiFiClient wifiClient;
HTTPClient httpClient;
WiFiUDP wifiUdp;
NTPClient ntpClient(wifiUdp);
SunSet sun;

const int core_version = 25;
bool offline = true;
bool keep_log = false;
int last_accessed_log = 0;

const char days_of_the_week[7][2] = {"s", "o", "u", "e", "h", "r", "a"};
char host_name[30] = {0};

struct Device {
  String ip;
  String mac;
};

Device *devices_array;
int devices_count = 0;

String ssid = "";
String password = "";
bool auto_reconnect = false;

uint32_t start_u_time = 0;
uint32_t loop_u_time = 0;
int uprisings = 1;
int offset = 0;
bool dst = false;

struct Smart {
  String smart_string;
  bool enabled;
  String days;
  #if defined(light_switch) || defined(blinds)
    String what;
  #endif
  bool any_trigger_required;
  String action;
  int at_time;
  int start_time;
  int end_time;
  bool at_sunset;
  int sunset_offset;
  bool has_lowering_at_sunset_offset;
  bool at_sunrise;
  int sunrise_offset;
  int at_dusk;
  int local_dusk_time;
  int dusk_offset;
  int dusk_day;
  int at_dawn;
  int local_dawn_time;
  int dawn_offset;
  int dawn_day;
  #ifdef light_switch
  String at_switch;
  int switch_offset;
  int switch_offset_countdown;
  #endif
  #ifdef blinds
    String at_blinds;
    int blinds_offset;
    int blinds_offset_countdown;
  #endif
  #ifdef thermostat
    String at_thermostat;
    int thermostat_offset;
    int thermostat_offset_countdown;
  #endif
  #ifdef chain
    String at_chain;
    int chain_offset;
    int chain_offset_countdown;
  #endif
  String must_be_; // This is a fulfillment condition, not a trigger.
  String twilight_must_be_;
  uint32_t lead_u_time;
};

Smart *smart_array;
int smart_count = 0;
bool smart_lock = false;

const String default_location = "52.2337172x21.0714322";
String geo_location = default_location;
int last_sun_check = -1;
int next_sunset = -1;
int next_sunrise = -1;
uint32_t sunset_u_time = 0;
uint32_t sunrise_u_time = 0;
int dusk_time = -1;
int dawn_time = -1;
int light_sensor = -1;
bool sensor_twilight = false;
bool calendar_twilight = false;

bool strContains(String text, String value);
bool strContains(String text, int value);
bool strContains(int text, int value);
String isStringDigit(String text, String fallback);
bool isStringDigit(String text);
String corectDateTime(int digit);
bool RTCisrunning();
bool hasTimeChanged();
void note(String text);
bool writeObjectToFile(String name, DynamicJsonDocument object);
String get1(String text, int index, char separator);
String oldSmart2NewSmart(const String& smart_string);
String getSmartString(bool raw);
void setSmart(const String& smart_string);
DynamicJsonDocument getSmartJson(bool raw);
void smartAction(int trigger, bool twilight_change);
void connectingToWifi(bool use_wps);
void initiatingWPS();
void activationTheLog();
void deactivationTheLog();
void requestForLogs();
void clearTheLog();
void getSunriseSunset(DateTime now);
int findMDNSDevices();
void receivedOfflineData();
void putOfflineData(String url, String data);
void putMultiOfflineData(String data);
void putMultiOfflineData(String data, bool log);
void getOfflineData();
void setupOTA();
void getSmartDetail();
void getRawSmartDetail();


bool strContains(String text, String value) {
  return text.indexOf(value) > -1;
}

bool strContains(String text, int value) {
  return text.indexOf(String(value)) > -1;
}

bool strContains(int text, int value) {
  return String(text).indexOf(String(value)) > -1;
}

String isStringDigit(String text, String fallback) {
  for (byte i = 0; i < text.length(); i++) {
    if (!(isDigit(text.charAt(i)) || text.charAt(i) == '.' || (text.charAt(i) == '-' && i == 0))) {
      return fallback;
    }
  }
  return text;
}

bool isStringDigit(String text) {
  for (byte i = 0; i < text.length(); i++) {
    if (!isDigit(text.charAt(i))) {
      return false;
    }
  }
  return text.length() > 0;
}

String corectDateTime(int digit) {
  if (digit < 10) {
    return "0" + String(digit);
  }
  return String(digit);
}

bool RTCisrunning() {
  #ifdef physical_clock
    return rtc.isrunning();
  #else
    return rtc.now().unixtime() > 1546304461;
  #endif
}

bool hasTimeChanged() {
  int current_u_time = RTCisrunning() ? rtc.now().unixtime() : millis() / 1000;
  if (abs(current_u_time - (int)loop_u_time) >= 1) {
    loop_u_time = current_u_time;
    return true;
  }
  return false;
}

void note(String text) {
  String log_text = strContains(text, "iDom") ? "\n[" : "[";
  if (RTCisrunning()) {
    DateTime now = rtc.now();
    log_text += now.day();
    log_text += ".";
    log_text += now.month();
    log_text += ".";
    log_text += String(now.year()).substring(2, 4);
    log_text += " ";
    log_text += now.hour();
    log_text += ":";
    log_text += now.minute();
    if (now.second() > 0) {
      log_text += ":";
      log_text += now.second();
    }
  } else {
    log_text += millis() / 1000;
  }
  log_text += "] " + text;

  Serial.print("\n" + log_text);

  if (keep_log) {
    File file = LittleFS.open("/log.txt", "a");
    if (file) {
      file.println(log_text);
      file.close();
    }
  }
}

bool writeObjectToFile(String name, DynamicJsonDocument object) {
  name = "/" + name + ".txt";
  bool result = false;

  File file = LittleFS.open(name, "w");
  if (file && object.size() > 0) {
    result = serializeJson(object, file) > 2;
    file.close();
  }

  return result;
}

String get1(String text, int index, char separator) {
  int found = 0;
  int str_index[] = {0, -1};
  int max_index = text.length() - 1;

  for (int i = 0; i <= max_index && found <= index; i++) {
    if (text.charAt(i) == separator || i == max_index) {
      found++;
      str_index[0] = str_index[1] + 1;
      str_index[1] = (i == max_index) ? i + 1 : i;
    }
  }
  return found > index ? text.substring(str_index[0], str_index[1]) : "";
}

String oldSmart2NewSmart(const String& smart_string) {
  String result;

  if (smart_string.length() < 2) {
    return "";
  }

  int count = 1;
  for (char c: smart_string) {
    if (c == ',') {
      count++;
    }
  }

  String single_smart_string = "";

  for (int i = 0; i < count; i++) {
    single_smart_string = get1(smart_string, i, ',');
    if (strContains(single_smart_string, String(smart_prefix))) {
      if (result.length() > 0) {
        result += ",";
      }

      result += String(smart_prefix);

      if (strContains(single_smart_string, "/")) {
        result += "/";
        single_smart_string = single_smart_string.substring(1);
      }

      String time_result = "";

      #ifdef thermostat
        if (strContains(single_smart_string, "_") || strContains(single_smart_string, "-")) {
          time_result += "h(";
          if (strContains(single_smart_string, "_")) {
            time_result += single_smart_string.substring(0, single_smart_string.indexOf("_"));
            single_smart_string = single_smart_string.substring(single_smart_string.indexOf("_") + 1);
          } else {
            time_result += "-1";
          }
          time_result += ";";
          if (strContains(single_smart_string, "-")) {
            time_result += single_smart_string.substring(single_smart_string.indexOf("-") + 1);
            single_smart_string = single_smart_string.substring(0, single_smart_string.indexOf("-"));
          } else {
            time_result += "-1";
          }
          time_result += ")";
        }
      #else
        if (strContains(single_smart_string, "_")) {
          time_result += single_smart_string.substring(0, single_smart_string.indexOf("_") + 1);
          single_smart_string = single_smart_string.substring(single_smart_string.indexOf("_") + 1);
        }
        if (strContains(single_smart_string, "-")) {
          time_result += "h(-1;";
          time_result += single_smart_string.substring(single_smart_string.indexOf("-") + 1);
          time_result += ")";
          single_smart_string = single_smart_string.substring(0, single_smart_string.indexOf("-"));
        }
      #endif

      String action_result = "";

      if (isStringDigit(single_smart_string.substring(0, single_smart_string.indexOf(String(smart_prefix))))) {
        action_result += "|";
        action_result += single_smart_string.substring(0, single_smart_string.indexOf(String(smart_prefix)));
        single_smart_string = single_smart_string.substring(single_smart_string.indexOf(String(smart_prefix)) + 1);
      }

      if (!strContains(single_smart_string, "w")) {
        result += strContains(single_smart_string, "o") ? "o" : "";
        result += strContains(single_smart_string, "u") ? "u" : "";
        result += strContains(single_smart_string, "e") ? "e" : "";
        result += strContains(single_smart_string, "h") ? "h" : "";
        result += strContains(single_smart_string, "r") ? "r" : "";
        result += strContains(single_smart_string, "a") ? "a" : "";
        result += strContains(single_smart_string, "s") ? "s" : "";
      }

      #if defined(light_switch) || defined(blinds)
        if (!(strContains(single_smart_string, "4") || strContains(single_smart_string, "123"))) {
          result += strContains(single_smart_string, "1") ? "1" : "";
          result += strContains(single_smart_string, "2") ? "2" : "";
          result += strContains(single_smart_string, "3") ? "3" : "";
        }
      #endif

      result += action_result;

      if (strContains(single_smart_string, "&")) {
        result += "&";
      } else {
        result += "|";
      }

      result += time_result;

      if (strContains(single_smart_string, "n")) {
        result += "n";
      }
      if (strContains(single_smart_string, "d")) {
        result += "d";
      }
      if (strContains(single_smart_string, "<")) {
        result += "<";
      }
      if (strContains(single_smart_string, ">")) {
        result += ">";
      }
      if (strContains(single_smart_string, "z")) {
        result += "z";
      }

      #ifdef light_switch
        if (strContains(single_smart_string, "6") || strContains(single_smart_string, "7")
        || strContains(single_smart_string, "8") || strContains(single_smart_string, "9")) {
          result += "r(";
          if (strContains(single_smart_string, "6")) {
            result += "1";
          }
          if (strContains(single_smart_string, "7")) {
            result += "2";
          }
          if (strContains(single_smart_string, "8")) {
            result += "-1";
          }
          if (strContains(single_smart_string, "9")) {
            result += "-2";
          }
          result += ")";
        }
      #endif
    }
  }

  return result;
}

String getSmartString(bool raw) {
  int i = -1;
  String result = "";
  while (++i < smart_count) {
    if (result.length() > 1) {
      result += ",";
    }
    result += smart_array[i].smart_string;
  }
  if (!raw) {
    result.replace("&", "%26");
  }
  return result;
}

DynamicJsonDocument getSmartJson(bool raw) {
  DynamicJsonDocument json_object(smart_count * 400);
  int i = -1;
  bool local_result;
  int count = -1;

  while (++i < smart_count) {
    local_result = (smart_array[i].at_sunset && smart_array[i].has_lowering_at_sunset_offset) || smart_array[i].lead_u_time > 0
    || (smart_array[i].at_dusk > -1 && (smart_array[i].local_dusk_time > 0 || smart_array[i].dusk_day > -1))
    || (smart_array[i].at_dawn > -1 && (smart_array[i].local_dawn_time > 0 || smart_array[i].dawn_day > -1));
    #ifdef light_switch
      local_result |= smart_array[i].at_switch != "?" && smart_array[i].switch_offset_countdown > 0;
    #endif
    #ifdef blinds
    local_result |= smart_array[i].at_blinds != "?" && smart_array[i].blinds_offset_countdown > 0;
    #endif
    #ifdef thermostat
      local_result |= smart_array[i].at_thermostat != "?" && smart_array[i].thermostat_offset_countdown > 0;
    #endif
    #ifdef chain
      local_result |= smart_array[i].at_chain != "?" && smart_array[i].chain_offset_countdown > 0;
    #endif
    if (!raw || local_result) {
      count++;
      json_object[String(count)]["smart"] = smart_array[i].smart_string;
    }
    if (!raw) {
      if (!smart_array[i].enabled) {
        json_object[String(count)]["enabled"] = false;
      }
      if (smart_array[i].days != "ouehras") {
        json_object[String(count)]["days"] = smart_array[i].days;
      }
      #if defined(light_switch) || defined(blinds)
        if (smart_array[i].what != "?") {
          json_object[String(count)]["what"] = smart_array[i].what.toInt();
        }
      #endif
      if (smart_array[i].action != "?") {
        if (strContains(smart_array[i].action, ";") && !strContains(smart_array[i].action, ".")) {
          for (int j = 0; j < 3; j++) {
            json_object[String(count)]["action"][j] = get1(smart_array[i].action, j, ';').toInt();
          }
        } else {
          json_object[String(count)]["action"] = smart_array[i].action;
        }
      }
      if (smart_array[i].any_trigger_required) {
        json_object[String(count)]["any_trigger_required"] = true;
      }
      if (smart_array[i].at_time > -1) {
        json_object[String(count)]["at_time"] = String(smart_array[i].at_time / 60) + ":" + (smart_array[i].at_time % 60 < 10 ? "0" + String(smart_array[i].at_time % 60) : String(smart_array[i].at_time % 60));
      }
      if (smart_array[i].start_time > -1 && smart_array[i].end_time > -1) {
          json_object[String(count)]["between_hours"][0] = String(smart_array[i].start_time / 60) + ":" + (smart_array[i].start_time % 60 < 10 ? "0" + String(smart_array[i].start_time % 60) : String(smart_array[i].start_time % 60));
          json_object[String(count)]["between_hours"][1] = String(smart_array[i].end_time / 60) + ":" + (smart_array[i].end_time % 60 < 10 ? "0" + String(smart_array[i].end_time % 60) : String(smart_array[i].end_time % 60));
      } else {
        if (smart_array[i].start_time > -1) {
          json_object[String(count)]["start_time"] = String(smart_array[i].start_time / 60) + ":" + (smart_array[i].start_time % 60 < 10 ? "0" + String(smart_array[i].start_time % 60) : String(smart_array[i].start_time % 60));
        }
        if (smart_array[i].end_time > -1) {
          json_object[String(count)]["end_time"] = String(smart_array[i].end_time / 60) + ":" + (smart_array[i].end_time % 60 < 10 ? "0" + String(smart_array[i].end_time % 60) : String(smart_array[i].end_time % 60));
        }
      }
    }
    if (smart_array[i].at_sunset) {
      if (!raw) {
        json_object[String(count)]["at_sunset"] = true;
        if (smart_array[i].sunset_offset != 0) {
          json_object[String(count)]["sunset_offset"] = smart_array[i].sunset_offset;
        }
      }
      if (smart_array[i].has_lowering_at_sunset_offset) {
        json_object[String(count)]["has_lowering_at_sunset_offset"] = true;
      }
    }
    if (smart_array[i].at_sunrise && !raw) {
      json_object[String(count)]["at_sunrise"] = true;
      if (smart_array[i].sunrise_offset != 0) {
        json_object[String(count)]["sunrise_offset"] = smart_array[i].sunrise_offset;
      }
    }
    if (smart_array[i].at_dusk > -1) {
      if (!raw) {
        if (smart_array[i].at_dusk > 0) {
          json_object[String(count)]["at_dusk"] = smart_array[i].at_dusk;
        } else {
          json_object[String(count)]["at_dusk"] = true;
        }
        if (smart_array[i].dusk_offset > 0) {
          json_object[String(count)]["dusk_offset"] = smart_array[i].dusk_offset;
        }
      }
      if (smart_array[i].local_dusk_time > 0) {
        if (raw) {
          json_object[String(count)]["local_dusk_time"] = smart_array[i].local_dusk_time;
        } else {
          json_object[String(count)]["local_dusk_time"] = String(smart_array[i].local_dusk_time / 60) + ":" + (smart_array[i].local_dusk_time % 60 < 10 ? "0" + String(smart_array[i].local_dusk_time % 60) : String(smart_array[i].local_dusk_time % 60));
        }
      }
      if (smart_array[i].dusk_day > -1) {
        if (smart_array[i].dusk_day > 0 || raw) {
          json_object[String(count)]["dusk_day"] = smart_array[i].dusk_day;
        } else {
          json_object[String(count)]["dusk_log"] = true;
        }
      }
    }
    if (smart_array[i].at_dawn > -1) {
      if (!raw) {
        if (smart_array[i].at_dawn > 0) {
          json_object[String(count)]["at_dawn"] = smart_array[i].at_dawn;
        } else {
          json_object[String(count)]["at_dawn"] = true;
        }
        if (smart_array[i].dawn_offset > 0) {
          json_object[String(count)]["dawn_offset"] = smart_array[i].dawn_offset;
        }
      }
      if (smart_array[i].local_dawn_time > 0) {
        if (raw) {
          json_object[String(count)]["local_dawn_time"] = smart_array[i].local_dawn_time;
        } else {
          json_object[String(count)]["local_dawn_time"] = String(smart_array[i].local_dawn_time / 60) + ":" + (smart_array[i].local_dawn_time % 60 < 10 ? "0" + String(smart_array[i].local_dawn_time % 60) : String(smart_array[i].local_dawn_time % 60));
        }
      }
      if (smart_array[i].dawn_day > -1) {
        if (smart_array[i].dawn_day > 0 || raw) {
          json_object[String(count)]["dawn_day"] = smart_array[i].dawn_day;
        } else {
          json_object[String(count)]["dawn_log"] = true;
        }
      }
    }
    #ifdef light_switch
      if (smart_array[i].at_switch != "?") {
        if (!raw) {
          json_object[String(count)]["at_switch"] = smart_array[i].at_switch;
          if (smart_array[i].switch_offset > 0) {
            json_object[String(count)]["switch_offset"] = smart_array[i].switch_offset;
          }
        }
        if (smart_array[i].switch_offset_countdown > 0) {
          json_object[String(count)]["switch_offset_countdown"] = smart_array[i].switch_offset_countdown;
        }
      }
    #endif
    #ifdef blinds
      if (smart_array[i].at_blinds != "?") {
        if (!raw) {
          json_object[String(count)]["at_blinds"] = smart_array[i].at_blinds;
          if (smart_array[i].blinds_offset > 0) {
            json_object[String(count)]["blinds_offset"] = smart_array[i].blinds_offset;
          }
        }
        if (smart_array[i].blinds_offset_countdown > 0) {
          json_object[String(count)]["blinds_offset_countdown"] = smart_array[i].blinds_offset_countdown;
        }
      }
    #endif
    #ifdef thermostat
      if (smart_array[i].at_thermostat != "?") {
        if (!raw) {
          json_object[String(count)]["at_thermostat"] = smart_array[i].at_thermostat;
          if (smart_array[i].thermostat_offset > 0) {
            json_object[String(count)]["thermostat_offset"] = smart_array[i].thermostat_offset;
          }
        }
        if (smart_array[i].thermostat_offset_countdown > 0) {
          json_object[String(count)]["thermostat_offset_countdown"] = smart_array[i].thermostat_offset_countdown;
        }
      }
    #endif
    #ifdef chain
      if (smart_array[i].at_chain != "?") {
        if (!raw) {
          json_object[String(count)]["at_chain"] = smart_array[i].at_chain;
          if (smart_array[i].chain_offset > 0) {
            json_object[String(count)]["chain_offset"] = smart_array[i].chain_offset;
          }
        }
        if (smart_array[i].chain_offset_countdown > 0) {
          json_object[String(count)]["chain_offset_countdown"] = smart_array[i].chain_offset_countdown;
        }
      }
    #endif
    if (smart_array[i].must_be_ != "?" && !raw) {
      json_object[String(count)]["must_be"] = smart_array[i].must_be_;
    }
    if (smart_array[i].twilight_must_be_ != "?" && !raw) {
      json_object[String(count)]["twilight_must_be"] = smart_array[i].twilight_must_be_;
    }
    if (smart_array[i].lead_u_time > 0) {
      if (raw) {
        json_object[String(count)]["lead_time"] = smart_array[i].lead_u_time;
      } else {
        DateTime lead_dt(smart_array[i].lead_u_time + offset + (dst ? 3600 : 0));
        json_object[String(count)]["lead_time"] = String(lead_dt.year()) + "-" + corectDateTime(lead_dt.month()) + "-" + corectDateTime(lead_dt.day()) + " " + corectDateTime(lead_dt.hour()) + ":" + corectDateTime(lead_dt.minute());
      }
    }
  }
  if (raw) {
    json_object["count"] = count + 1;
  }

  return json_object;
}

void readSmart() {
  File file = LittleFS.open("/smart.txt", "r");
  if (!file) {
    return;
  }

  DynamicJsonDocument json_object(smart_count * 400);
  DeserializationError deserialization_error = deserializeJson(json_object, file);

  if (deserialization_error) {
    note("Smart file error: " + String(deserialization_error.f_str()));
    file.close();
    return;
  }

  file.close();

  if (!(json_object.containsKey("count") && json_object["count"].as<int>() > 0)) {
    return;
  }

  int count = json_object["count"].as<int>();
  JsonObject json_object_2;
  int result = 0;
  int i = -1;
  while (++i < smart_count) {
    for (int j = 0; j < count; j++) {
      json_object_2 = json_object[String(j)];
      if (smart_array[i].smart_string == json_object_2["smart"].as<String>()) {
        smart_array[i].has_lowering_at_sunset_offset = json_object_2.containsKey("has_lowering_at_sunset_offset");
        if (json_object_2.containsKey("local_dusk_time")) {
          smart_array[i].local_dusk_time = json_object_2["local_dusk_time"].as<int>();
        }
        if (json_object_2.containsKey("dusk_day")) {
          smart_array[i].dusk_day = json_object_2["dusk_day"].as<int>();
        }
        if (json_object_2.containsKey("local_dawn_time")) {
          smart_array[i].local_dawn_time = json_object_2["local_dawn_time"].as<int>();
        }
        if (json_object_2.containsKey("dawn_day")) {
          smart_array[i].dawn_day = json_object_2["dawn_day"].as<int>();
        }
        #ifdef light_switch
          if (json_object_2.containsKey("switch_offset_countdown")) {
            smart_array[i].switch_offset_countdown = json_object_2["switch_offset_countdown"].as<int>();
          }
        #endif
        #ifdef blinds
          if (json_object_2.containsKey("blinds_offset_countdown")) {
            smart_array[i].blinds_offset_countdown = json_object_2["blinds_offset_countdown"].as<int>();
          }
        #endif
        #ifdef thermostat
          if (json_object_2.containsKey("thermostat_offset_countdown")) {
            smart_array[i].thermostat_offset_countdown = json_object_2["thermostat_offset_countdown"].as<int>();
          }
        #endif
        #ifdef chain
          if (json_object_2.containsKey("chain_offset_countdown")) {
            smart_array[i].chain_offset_countdown = json_object_2["chain_offset_countdown"].as<int>();
          }
        #endif
        if (json_object_2.containsKey("lead_time")) {
          smart_array[i].lead_u_time = json_object_2["lead_time"].as<int>();
        }
        result++;
      }
    }
  }
  if (result > 0) {
    note(String(result) + "/" + String(smart_count) + " Smart(s) restored");
  }
}

void setSmart(const String& smart_string) {
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
    single_smart_string = get1(smart_string, i, ',');
    if (smart_prefix == single_smart_string.charAt(0)) {
      smart_array[smart_count].smart_string = single_smart_string;
      smart_array[smart_count].enabled = !strContains(single_smart_string, "/");

      String substring = single_smart_string.substring(0, single_smart_string.indexOf(strContains(single_smart_string, "|") ? "|" : "&"));
      smart_array[smart_count].days = strContains(substring, "o") ? "o" : "";
      smart_array[smart_count].days += strContains(substring, "u") ? "u" : "";
      smart_array[smart_count].days += strContains(substring, "e") ? "e" : "";
      smart_array[smart_count].days += strContains(substring, "h") ? "h" : "";
      smart_array[smart_count].days += strContains(substring, "r") ? "r" : "";
      smart_array[smart_count].days += strContains(substring, "a") ? "a" : "";
      smart_array[smart_count].days += strContains(substring, "s") ? "s" : "";
      if (smart_array[smart_count].days == "") {
        smart_array[smart_count].days = "ouehras";
      }

      #if defined(light_switch) || defined(blinds)
        smart_array[smart_count].what = strContains(substring, 1) ? "1" : "";
        smart_array[smart_count].what += strContains(substring, 2) ? "2" : "";
        smart_array[smart_count].what += strContains(substring, 3) ? "3" : "";
        smart_array[smart_count].what += strContains(substring, 4) ? "123" : "";
        if (smart_array[smart_count].what == "") {
          smart_array[smart_count].what = "?";
        }
      #endif

      smart_array[smart_count].any_trigger_required = strContains(single_smart_string, "&");

      smart_array[smart_count].action = "?";
      if (smart_array[smart_count].any_trigger_required) {
        if (strContains(single_smart_string, "|")) {
          smart_array[smart_count].action = single_smart_string.substring(single_smart_string.indexOf("|") + 1, single_smart_string.indexOf("&"));
        }
        single_smart_string = single_smart_string.substring(single_smart_string.indexOf("&") + 1);
      } else {
        if (single_smart_string.indexOf("|") != single_smart_string.lastIndexOf("|")) {
          smart_array[smart_count].action = single_smart_string.substring(single_smart_string.indexOf("|") + 1, single_smart_string.lastIndexOf("|"));
        }
        single_smart_string = single_smart_string.substring(single_smart_string.lastIndexOf("|") + 1);
      }

      smart_array[smart_count].twilight_must_be_ = "?";
      if (strContains(single_smart_string, "r2(")) {
        substring = single_smart_string.substring(single_smart_string.indexOf("r2("), single_smart_string.indexOf(")", single_smart_string.indexOf("r2(")) + 1);
        smart_array[smart_count].twilight_must_be_ = substring.substring(substring.indexOf("r2(") + 3, substring.indexOf(")", substring.indexOf("r2(")));
        single_smart_string.replace(substring, "");
      }

      smart_array[smart_count].must_be_ = "?";
      if (strContains(single_smart_string, "r(")) {
        smart_array[smart_count].must_be_ = single_smart_string.substring(single_smart_string.indexOf("r(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("r(")));
      }

      smart_array[smart_count].at_time = -1;
      if (strContains(single_smart_string, "_")) {
        smart_array[smart_count].at_time = isStringDigit(single_smart_string.substring(0, single_smart_string.indexOf("_")), "-1").toInt();
      }

      smart_array[smart_count].start_time = -1;
      smart_array[smart_count].end_time = -1;
      if (strContains(single_smart_string, "h(")) {
        smart_array[smart_count].start_time = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("h(") + 2, single_smart_string.indexOf(";", single_smart_string.indexOf("h("))), "-1").toInt();
        smart_array[smart_count].end_time = isStringDigit(single_smart_string.substring(single_smart_string.indexOf(";", single_smart_string.indexOf("h(")) + 1, single_smart_string.indexOf(")", single_smart_string.indexOf("h("))), "-1").toInt();
      }

      smart_array[smart_count].at_sunset = strContains(single_smart_string, "n");
      smart_array[smart_count].sunset_offset = 0;
      smart_array[smart_count].has_lowering_at_sunset_offset = false;
      if (strContains(single_smart_string, "n(")) {
        smart_array[smart_count].sunset_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("n(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("n("))), "0").toInt();
      }

      smart_array[smart_count].at_sunrise = strContains(single_smart_string, "d");
      smart_array[smart_count].sunrise_offset = 0;
      if (strContains(single_smart_string, "d(")) {
        smart_array[smart_count].sunrise_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("d(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("d("))), "0").toInt();
      }

      smart_array[smart_count].at_dusk = -1;
      smart_array[smart_count].local_dusk_time = -1;
      smart_array[smart_count].dusk_offset = 0;
      smart_array[smart_count].dusk_day = 0;
      if (strContains(single_smart_string, "<")) {
        smart_array[smart_count].at_dusk = 0;
        if (strContains(single_smart_string, "<(")) {
          smart_array[smart_count].at_dusk = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("<(") + 2, single_smart_string.indexOf(";", single_smart_string.indexOf("<("))), "0").toInt();
          smart_array[smart_count].dusk_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf(";", single_smart_string.indexOf("<(")) + 1, single_smart_string.indexOf(")", single_smart_string.indexOf("<("))), "0").toInt();
        }
      }

      smart_array[smart_count].at_dawn = -1;
      smart_array[smart_count].local_dawn_time = -1;
      smart_array[smart_count].dawn_offset = 0;
      smart_array[smart_count].dawn_day = 0;
      if (strContains(single_smart_string, ">")) {
        smart_array[smart_count].at_dawn = 0;
        if (strContains(single_smart_string, ">(")) {
          smart_array[smart_count].at_dawn = isStringDigit(single_smart_string.substring(single_smart_string.indexOf(">(") + 2, single_smart_string.indexOf(";", single_smart_string.indexOf(">("))), "0").toInt();
          smart_array[smart_count].dawn_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf(";", single_smart_string.indexOf(">(")) + 1, single_smart_string.indexOf(")", single_smart_string.indexOf(">("))), "0").toInt();
        }
      }

      if (strContains(single_smart_string, "z")) {
        smart_array[smart_count].at_dusk = 0;
        smart_array[smart_count].local_dusk_time = -1;
        smart_array[smart_count].dusk_day = -1;
        smart_array[smart_count].at_dawn = 0;
        smart_array[smart_count].local_dawn_time = -1;
        smart_array[smart_count].dawn_day = -1;
        if (strContains(single_smart_string, "z(")) {
          smart_array[smart_count].dusk_offset = single_smart_string.substring(single_smart_string.indexOf("z(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("z("))).toInt();
          smart_array[smart_count].dawn_offset = smart_array[smart_count].dusk_offset;
        }
      }

      #ifdef light_switch
        smart_array[smart_count].at_switch = "?";
        smart_array[smart_count].switch_offset = 0;
        smart_array[smart_count].switch_offset_countdown = -1;
        if (strContains(single_smart_string, "l(")) {
          if (strContains(single_smart_string.substring(single_smart_string.indexOf("l("), single_smart_string.indexOf(")", single_smart_string.indexOf("l("))), ";")) {
            smart_array[smart_count].at_switch = single_smart_string.substring(single_smart_string.indexOf("l(") + 2, single_smart_string.indexOf(";", single_smart_string.indexOf("l(")));
            smart_array[smart_count].switch_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf(";", single_smart_string.indexOf("l(")) + 1, single_smart_string.indexOf(")", single_smart_string.indexOf("l("))), "0").toInt();
          } else {
            smart_array[smart_count].at_switch = single_smart_string.substring(single_smart_string.indexOf("l(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("l(")));
          }
        }
      #endif

      #ifdef blinds
        smart_array[smart_count].at_blinds = "?";
        smart_array[smart_count].blinds_offset = 0;
        smart_array[smart_count].blinds_offset_countdown = -1;
        if (strContains(single_smart_string, "b(")) {
        int semicolon = 0;
        for (char b: smart_string) b == ';' ? semicolon++ : false;
          if (semicolon == 1 || semicolon == 3) {
            smart_array[smart_count].at_blinds = single_smart_string.substring(single_smart_string.indexOf("b(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("b("))).substring(0, single_smart_string.substring(single_smart_string.indexOf("b(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("b("))).lastIndexOf(";"));
            smart_array[smart_count].blinds_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("b(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("b("))).substring(single_smart_string.substring(single_smart_string.indexOf("b(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("b("))).lastIndexOf(";") + 1), "0").toInt();
          } else {
            smart_array[smart_count].at_blinds = single_smart_string.substring(single_smart_string.indexOf("b(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("b(")));
          }
        }
      #endif

      #ifdef thermostat
        smart_array[smart_count].at_thermostat = "?";
        smart_array[smart_count].thermostat_offset = 0;
        smart_array[smart_count].thermostat_offset_countdown = 0;
        if (strContains(single_smart_string, "t(")) {
          if (strContains(single_smart_string.substring(single_smart_string.indexOf("t("), single_smart_string.indexOf(")", single_smart_string.indexOf("t("))), ";")) {
            smart_array[smart_count].at_thermostat = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("t(") + 2, single_smart_string.indexOf(";", single_smart_string.indexOf("t("))), "?");
            smart_array[smart_count].thermostat_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf(";", single_smart_string.indexOf("t(")) + 1, single_smart_string.indexOf(")", single_smart_string.indexOf("t("))), "0").toInt();
          } else {
            smart_array[smart_count].at_thermostat = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("t(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("t("))), "?");
          }
        }
      #endif

      #ifdef chain
        smart_array[smart_count].at_chain = "?";
        smart_array[smart_count].chain_offset = 0;
        smart_array[smart_count].chain_offset_countdown = -1;
        if (strContains(single_smart_string, "c(")) {
          if (strContains(single_smart_string.substring(single_smart_string.indexOf("c("), single_smart_string.indexOf(")", single_smart_string.indexOf("c("))), ";")) {
            smart_array[smart_count].at_chain = single_smart_string.substring(single_smart_string.indexOf("c(") + 2, single_smart_string.indexOf(";", single_smart_string.indexOf("c(")));
            smart_array[smart_count].chain_offset = isStringDigit(single_smart_string.substring(single_smart_string.indexOf(";", single_smart_string.indexOf("c(")) + 1, single_smart_string.indexOf(")", single_smart_string.indexOf("c("))), "0").toInt();
          } else {
            smart_array[smart_count].at_chain = single_smart_string.substring(single_smart_string.indexOf("c(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("c(")));
          }
        }
      #endif

      smart_array[smart_count].lead_u_time = 0;
      if (strContains(single_smart_string, "e(")) {
          smart_array[smart_count].lead_u_time = isStringDigit(single_smart_string.substring(single_smart_string.indexOf("e(") + 2, single_smart_string.indexOf(")", single_smart_string.indexOf("e("))), "0").toInt();
      }

      smart_count++;
    }
  }
  readSmart();
}

int verifiedTime(int time) {
  if (time > 1439) {
    return 1439 - time;
  }
  return time;
}

void smartAction(int trigger, bool twilight_change) { // -1 none ; 0 light_changed ; 1 switch_1 ; 2 switch_2 ; 5 stepper_movement ; 6 temperature_changed
  if (!RTCisrunning()) {
    return;
  }

  int current_time = -1;
  DateTime now = rtc.now();
  current_time = (now.hour() * 60) + now.minute();

  if (current_time == -1) {
    return;
  }

  int i = -1;
  bool result = false;
  bool local_result;
  bool some_activation;
  bool at_time_result;
  bool start_time_result;
  bool end_time_result;
  bool at_sunset_result;
  bool at_sunrise_result;
  bool at_dusk_result;
  bool at_dawn_result;
  #ifdef light_switch
    bool at_switch_result;
    int new_light[] = {-1, -1};
  #endif
  #ifdef blinds
    bool at_blinds_result;
    int new_destination[] = {-1, -1, -1};
  #endif
  #ifdef thermostat
    bool at_thermostat_result;
    int new_heating = -1;
    int new_heating_temperature = -1;
  #endif
  #ifdef chain
    bool at_chain_result;
    int new_destination = -1;
  #endif
  String action;
  String log_text = "";
  String local_log = "";
  while (++i < smart_count) {
    if (smart_array[i].enabled && strContains(smart_array[i].days, days_of_the_week[now.dayOfTheWeek()])) {
      local_result = false;
      some_activation = false;
      at_time_result = false;
      start_time_result = false;
      end_time_result = false;
      at_sunset_result = false;
      at_sunrise_result = false;
      at_dusk_result = false;
      at_dawn_result = false;
      #ifdef light_switch
        at_switch_result = false;
      #endif
      #ifdef blinds
        at_blinds_result = false;
      #endif
      #ifdef thermostat
        at_thermostat_result = false;
      #endif
      #ifdef chain
        at_chain_result = false;
      #endif
      action = "?";
      local_log = "";

      if (smart_array[i].at_time > -1) {
        at_time_result = smart_array[i].at_time == current_time && smart_array[i].lead_u_time + 60 < now.unixtime();
        some_activation |= at_time_result;
        if (!at_time_result && smart_array[i].any_trigger_required) {
          at_time_result = smart_array[i].at_time < current_time;
        }
        local_result |= at_time_result;
      }

      if (smart_array[i].start_time > -1) {
        start_time_result = smart_array[i].start_time < current_time;
        local_result |= start_time_result;
      }

      if (smart_array[i].end_time > -1) {
        end_time_result = smart_array[i].end_time > current_time;
        local_result |= end_time_result;
      }

      if (smart_array[i].at_sunset && next_sunset > -1) {
        at_sunset_result = verifiedTime(next_sunset + smart_array[i].sunset_offset) == current_time && smart_array[i].lead_u_time + 60 < now.unixtime();
        some_activation |= at_sunset_result;
        if (!at_sunset_result && smart_array[i].any_trigger_required) {
          at_sunset_result = (next_sunset + smart_array[i].sunset_offset) < current_time;
        }
        local_result |= at_sunset_result;
      }

      if (smart_array[i].at_sunrise && next_sunrise > -1) {
        at_sunrise_result = verifiedTime(next_sunrise + smart_array[i].sunrise_offset) == current_time && smart_array[i].lead_u_time + 60 < now.unixtime();
        some_activation |= at_sunrise_result;
        if (!at_sunrise_result && smart_array[i].any_trigger_required) {
          at_sunrise_result = (next_sunrise + smart_array[i].sunrise_offset) < current_time;
        }
        local_result |= at_sunrise_result;
      }

      if (smart_array[i].at_dusk > -1) {
        if (smart_array[i].dusk_day > -1 && smart_array[i].dusk_day != now.day() && (smart_array[i].at_dusk == 0 ? !sensor_twilight : smart_array[i].at_dusk < light_sensor)) {
          smart_array[i].dusk_day = 0;
        }
        at_dusk_result = trigger == 0 && (smart_array[i].at_dusk == 0 ? (twilight_change ? sensor_twilight : false) : smart_array[i].at_dusk > light_sensor);
        at_dusk_result &= smart_array[i].dusk_day == -1 || smart_array[i].dusk_day == 0;
        if (at_dusk_result && (smart_array[i].dusk_day == -1 || smart_array[i].dusk_day == 0)) {
          smart_array[i].local_dusk_time = current_time;
          if (smart_array[i].dusk_day == 0) {
            smart_array[i].dusk_day = now.day();
          }
        }
        if (smart_array[i].dusk_offset > 0 && smart_array[i].local_dusk_time > -1) {
          at_dusk_result = verifiedTime(smart_array[i].local_dusk_time + smart_array[i].dusk_offset) == current_time && smart_array[i].lead_u_time + 60 < now.unixtime();
        }
        some_activation |= at_dusk_result;
        if (!at_dusk_result && smart_array[i].any_trigger_required) {
          at_dusk_result = smart_array[i].at_dusk == 0 ? sensor_twilight : smart_array[i].at_dusk > light_sensor;
          if (smart_array[i].dusk_offset > 0 && smart_array[i].local_dusk_time > -1) {
            at_dusk_result &= (smart_array[i].local_dusk_time + smart_array[i].dusk_offset) < current_time;
          }
        }
        local_result |= at_dusk_result;
      }

      if (smart_array[i].at_dawn > -1) {
        if (smart_array[i].dawn_day > -1 && smart_array[i].dawn_day != now.day() && (smart_array[i].at_dawn == 0 ? sensor_twilight : smart_array[i].at_dawn > light_sensor)) {
          smart_array[i].dawn_day = 0;
        }
        at_dawn_result = trigger == 0 && (smart_array[i].at_dawn == 0 ? (twilight_change ? !sensor_twilight : false) : smart_array[i].at_dawn < light_sensor);
        at_dawn_result &= smart_array[i].dawn_day == -1 || smart_array[i].dawn_day == 0;
        at_dawn_result &= !(smart_array[i].has_lowering_at_sunset_offset || calendar_twilight);
        if (at_dawn_result && (smart_array[i].dawn_day == -1 || smart_array[i].dawn_day == 0)) {
          smart_array[i].local_dawn_time = current_time;
          if (smart_array[i].dawn_day == 0) {
            smart_array[i].dawn_day = now.day();
          }
        }
        if (smart_array[i].dawn_offset > 0 && smart_array[i].local_dawn_time > -1) {
          at_dawn_result &= verifiedTime(smart_array[i].local_dawn_time + smart_array[i].dawn_offset) == current_time && smart_array[i].lead_u_time + 60 < now.unixtime();
        }
        some_activation |= at_dawn_result;
        if (!at_dawn_result && smart_array[i].any_trigger_required) {
          at_dawn_result = smart_array[i].at_dawn == 0 ? !sensor_twilight : smart_array[i].at_dawn < light_sensor;
          at_dawn_result &= !(smart_array[i].has_lowering_at_sunset_offset || calendar_twilight);
          if (smart_array[i].dawn_offset > 0 && smart_array[i].local_dawn_time > -1) {
            at_dawn_result &= (smart_array[i].local_dawn_time + smart_array[i].dawn_offset) < current_time;
          }
        }
        local_result |= at_dawn_result;
      }

      if (smart_array[i].has_lowering_at_sunset_offset && calendar_twilight) {
        smart_array[i].has_lowering_at_sunset_offset = false;
      }

      #ifdef light_switch
        if (smart_array[i].at_switch != "?") {
          at_switch_result = (trigger == 1 && strContains(smart_array[i].at_switch, 1)) || (trigger == 2 && strContains(smart_array[i].at_switch, 2)) || smart_array[i].switch_offset_countdown == 0;
          if (strContains(smart_array[i].at_switch, 1)) {
            if (strContains(smart_array[i].at_switch, -1)) {
              at_switch_result &= !light[0];
            } else {
              at_switch_result &= light[0];
            }
          }
          if (strContains(smart_array[i].at_switch, 2)) {
            if (strContains(smart_array[i].at_switch, -2)) {
              at_switch_result &= !light[1];
            } else {
              at_switch_result &= light[1];
            }
          }
          if (at_switch_result && smart_array[i].switch_offset > 0 && smart_array[i].switch_offset_countdown == -1) {
            at_switch_result = false;
            smart_array[i].switch_offset_countdown = smart_array[i].switch_offset * 60;
          }
          some_activation |= at_switch_result;
          if (smart_array[i].switch_offset_countdown > -1) {
            smart_array[i].switch_offset_countdown--;
          }
          local_result |= at_switch_result;
        }

        if (smart_array[i].must_be_ != "?") {
          if (strContains(smart_array[i].must_be_, 1)) {
            if (strContains(smart_array[i].must_be_, -1)) {
              local_result &= !light[0];
            } else {
              local_result &= light[0];
            }
          }
          if (strContains(smart_array[i].must_be_, 2)) {
            if (strContains(smart_array[i].must_be_, -2)) {
              local_result &= !light[1];
            } else {
              local_result &= light[1];
            }
          }
        }
      #endif

      #ifdef blinds
        if (smart_array[i].at_blinds != "?") {
          at_blinds_result = trigger == 5 || smart_array[i].blinds_offset_countdown == 0;
          if (strContains(smart_array[i].at_blinds, ";")) {
            at_blinds_result &= getActual(true) == smart_array[i].at_blinds;
          } else {
            at_blinds_result &= getActual(true) == (steps[0] > 0 ? smart_array[i].at_blinds : "0") + ";" + (steps[1] > 0 ? smart_array[i].at_blinds : "0") + ";" + (steps[2] > 0 ? smart_array[i].at_blinds : "0");
          }
          if (at_blinds_result && smart_array[i].blinds_offset > 0 && smart_array[i].blinds_offset_countdown == -1) {
            at_blinds_result = false;
            smart_array[i].blinds_offset_countdown = smart_array[i].blinds_offset * 60;
          }
          some_activation |= at_blinds_result;
          if (smart_array[i].blinds_offset_countdown > -1) {
            smart_array[i].blinds_offset_countdown--;
          }
          local_result |= at_blinds_result;
        }

        if (smart_array[i].must_be_ != "?") {
          if (strContains(smart_array[i].must_be_, "<") || strContains(smart_array[i].must_be_, ">")) {
            if (strContains(smart_array[i].must_be_, ";")) {
              if (strContains(smart_array[i].must_be_.substring(0, smart_array[i].must_be_.indexOf(";")), "<")) {
                local_result &= destination[0] <= actual[0];
                local_result &= getValue(0) < smart_array[i].must_be_.substring(1, smart_array[i].must_be_.indexOf(";")).toInt();
              } else {
                if (strContains(smart_array[i].must_be_.substring(0, smart_array[i].must_be_.indexOf(";")), ">")) {
                  local_result &= destination[0] >= actual[0];
                  local_result &= getValue(0) > smart_array[i].must_be_.substring(1, smart_array[i].must_be_.indexOf(";")).toInt();
                } else {
                  local_result &= destination[0] == actual[0];
                  local_result &= getValue(0) == smart_array[i].must_be_.substring(0, smart_array[i].must_be_.indexOf(";")).toInt();
                }
              }
              if (strContains(smart_array[i].must_be_.substring(smart_array[i].must_be_.indexOf(";") + 1, smart_array[i].must_be_.lastIndexOf(";")), "<")) {
                local_result &= destination[1] <= actual[1];
                local_result &= getValue(1) < smart_array[i].must_be_.substring(smart_array[i].must_be_.indexOf(";") + 2, smart_array[i].at_blinds.lastIndexOf(";")).toInt();
              } else {
                if (strContains(smart_array[i].must_be_.substring(smart_array[i].must_be_.indexOf(";") + 1, smart_array[i].must_be_.lastIndexOf(";")), ">")) {
                  local_result &= destination[1] >= actual[1];
                  local_result &= getValue(1) > smart_array[i].must_be_.substring(smart_array[i].must_be_.indexOf(";") + 2, smart_array[i].at_blinds.lastIndexOf(";")).toInt();
                } else {
                  local_result &= destination[1] == actual[1];
                  local_result &= getValue(1) == smart_array[i].must_be_.substring(smart_array[i].must_be_.indexOf(";") + 1, smart_array[i].at_blinds.lastIndexOf(";")).toInt();
                }
              }
              if (strContains(smart_array[i].must_be_.substring(smart_array[i].must_be_.lastIndexOf(";")), "<")) {
                local_result &= destination[2] <= actual[2];
                local_result &= getValue(2) < smart_array[i].must_be_.substring(smart_array[i].must_be_.lastIndexOf(";") + 2).toInt();
              } else {
                if (strContains(smart_array[i].must_be_.substring(smart_array[i].must_be_.lastIndexOf(";")), ">")) {
                  local_result &= destination[2] >= actual[2];
                  local_result &= getValue(2) > smart_array[i].must_be_.substring(smart_array[i].must_be_.lastIndexOf(";") + 2).toInt();
                } else {
                  local_result &= destination[2] == actual[2];
                  local_result &= getValue(2) == smart_array[i].must_be_.substring(smart_array[i].must_be_.lastIndexOf(";") + 1).toInt();
                }
              }
            } else {
              if (strContains(smart_array[i].must_be_, "<")) {
                for (int j = 0; j < 3; j++) {
                  local_result &= steps[j] == 0 || (destination[j] <= actual[j] && getValue(j) < smart_array[i].must_be_.substring(1).toInt());
                }
              } else {
                for (int j = 0; j < 3; j++) {
                  local_result &= steps[j] == 0 || (destination[j] >= actual[j] && getValue(j) > smart_array[i].must_be_.substring(1).toInt());
                }
              }
            }
          } else {
            local_result &= destination[0] == actual[0] && destination[1] == actual[1] && destination[2] == actual[2];
            if (strContains(smart_array[i].must_be_, ";")) {
              local_result &= getValue() == smart_array[i].must_be_;
            } else {
              for (int j = 0; j < 3; j++) {
                local_result &= steps[j] == 0 || getValue(j) == smart_array[i].must_be_.toInt();
              }
            }
          }
        }
      #endif

      #ifdef thermostat
        if (smart_array[i].at_thermostat != "?") {
          at_thermostat_result = trigger == 6 || smart_array[i].thermostat_offset_countdown == 0;
          at_thermostat_result &= temperature == smart_array[i].at_thermostat.toFloat();
          if (at_thermostat_result && smart_array[i].thermostat_offset > 0 && smart_array[i].thermostat_offset_countdown == -1) {
            at_thermostat_result = false;
            smart_array[i].thermostat_offset_countdown = smart_array[i].thermostat_offset * 60;
          }
          some_activation |= at_thermostat_result;
          if (smart_array[i].thermostat_offset_countdown > -1) {
            smart_array[i].thermostat_offset_countdown--;
          }
          local_result |= at_thermostat_result;
        }

        if (smart_array[i].must_be_ != "?") {
          if (strContains(smart_array[i].must_be_, ".")) {
            if (strContains(smart_array[i].must_be_, "<") || strContains(smart_array[i].must_be_, ">")) {
              if (strContains(smart_array[i].must_be_, "<")) {
                local_result &= temperature < smart_array[i].must_be_.substring(1).toFloat();
              } else {
                local_result &= temperature > smart_array[i].must_be_.substring(1).toFloat();
              }
            } else {
              local_result &= temperature == smart_array[i].must_be_.toFloat();
            }
          } else {
            local_result &= heating == strContains(smart_array[i].must_be_, "1");
          }
        }
      #endif

      #ifdef chain
        if (smart_array[i].at_chain != "?") {
          at_chain_result = trigger == 5 || smart_array[i].chain_offset_countdown == 0;
          at_chain_result &= getActual() == smart_array[i].at_chain;
          if (at_chain_result && smart_array[i].chain_offset > 0 && smart_array[i].chain_offset_countdown == -1) {
            at_chain_result = false;
            smart_array[i].chain_offset_countdown = smart_array[i].chain_offset * 60;
          }
          some_activation |= at_chain_result;
          if (smart_array[i].chain_offset_countdown > -1) {
            smart_array[i].chain_offset_countdown--;
          }
          local_result |= at_chain_result;
        }

        if (smart_array[i].must_be_ != "?") {
          if (strContains(smart_array[i].must_be_, "<") || strContains(smart_array[i].must_be_, ">")) {
            if (strContains(smart_array[i].must_be_, "<")) {
              local_result &= steps == 0 || (destination <= actual && getValue() < smart_array[i].must_be_.substring(1));
            } else {
              local_result &= steps == 0 || (destination >= actual && getValue() > smart_array[i].must_be_.substring(1));
            }
          } else {
            local_result &= destination == actual;
            local_result &= getValue() == smart_array[i].must_be_;
          }
        }
      #endif

      if (smart_array[i].twilight_must_be_ != "?") {
        if (next_sunset > -1 && next_sunrise > -1) {
          if (strContains(smart_array[i].twilight_must_be_, "n")) {
            local_result &= calendar_twilight;
          }
          if (strContains(smart_array[i].twilight_must_be_, "d")) {
            local_result &= !calendar_twilight;
          }
        }
        if (strContains(smart_array[i].twilight_must_be_, "<")) {
          local_result &= sensor_twilight;
        }
        if (strContains(smart_array[i].twilight_must_be_, ">")) {
          local_result &= !sensor_twilight;
        }
      }

      if (smart_array[i].any_trigger_required) {
        local_result &= some_activation;
      }
      local_result &= !smart_array[i].any_trigger_required
        || ((smart_array[i].at_time == -1 || (smart_array[i].at_time > -1 && at_time_result))
        && (smart_array[i].start_time == -1 || (smart_array[i].start_time > -1 && start_time_result))
        && (smart_array[i].end_time == -1 || (smart_array[i].end_time > -1 && end_time_result))
        && (!smart_array[i].at_sunset || (smart_array[i].at_sunset && at_sunset_result))
        && (!smart_array[i].at_sunrise || (smart_array[i].at_sunrise && at_sunrise_result))
        && (smart_array[i].at_dusk == -1 || (smart_array[i].at_dusk > -1 && at_dusk_result))
        && (smart_array[i].at_dawn == -1 || (smart_array[i].at_dawn > -1 && at_dawn_result)));
      #ifdef light_switch
        local_result &= !smart_array[i].any_trigger_required || (smart_array[i].at_switch == "?" || (smart_array[i].at_switch != "?" && at_switch_result));
      #endif
      #ifdef blinds
        local_result &= !smart_array[i].any_trigger_required || (smart_array[i].at_blinds == "?" || (smart_array[i].at_blinds != "?" && at_blinds_result));
      #endif
      #ifdef thermostat
        local_result &= !smart_array[i].any_trigger_required || (smart_array[i].at_thermostat == "?" || (smart_array[i].at_thermostat != "?" && at_thermostat_result));
      #endif
      #ifdef chain
        local_result &= !smart_array[i].any_trigger_required || (smart_array[i].at_chain == "?" || (smart_array[i].at_chain != "?" && at_chain_result));
      #endif

      if (local_result) {
        if (at_sunset_result) {
          action = smart_array[i].action == "?" || strContains(smart_array[i].action, ".") ? "100" : smart_array[i].action;
          if (local_log.length() > 2) {
            local_log += " & ";
          }
          local_log += "sunset";
          if (smart_array[i].sunset_offset != 0) {
            if (smart_array[i].sunset_offset > 0) {
              local_log += "+";
            }
            local_log += String(smart_array[i].sunset_offset);
          }
        }
        if (at_sunrise_result) {
          action = smart_array[i].action == "?" || strContains(smart_array[i].action, ".") ? "0" : smart_array[i].action;
          if (local_log.length() > 2) {
            local_log += " & ";
          }
          local_log += "sunrise";
          if (smart_array[i].sunrise_offset != 0) {
            if (smart_array[i].sunrise_offset > 0) {
              local_log += "+";
            }
            local_log += String(smart_array[i].sunrise_offset);
          }
        }
        if (at_dusk_result) {
          action = smart_array[i].action == "?" || strContains(smart_array[i].action, ".") ? "100" : smart_array[i].action;
          if (local_log.length() > 2) {
            local_log += " & ";
          }
          local_log += "dusk";
          if (smart_array[i].dusk_offset > 0) {
            local_log += "+" + String(smart_array[i].dusk_offset);
          }
        }
        if (at_dawn_result) {
          action = smart_array[i].action == "?" || strContains(smart_array[i].action, ".") ? "0" : smart_array[i].action;
          if (local_log.length() > 2) {
            local_log += " & ";
          }
          local_log += "dawn";
          if (smart_array[i].dawn_offset > 0) {
            local_log += "+" + String(smart_array[i].dawn_offset);
          }
        }
        if (at_time_result) {
          action = smart_array[i].action == "?" || strContains(smart_array[i].action, ".") ? "100" : smart_array[i].action;
          if (local_log.length() > 2) {
            local_log += " & ";
          }
          local_log += "time";
        }
        #ifdef light_switch
          if (at_switch_result) {
            action = smart_array[i].action;
            if (local_log.length() > 2) {
              local_log += " & ";
            }
            local_log += "switch";
            if (smart_array[i].switch_offset > 0) {
              local_log += "+" + String(smart_array[i].switch_offset);
            }
            local_log += " " + smart_array[i].at_switch;
          }
        #endif
        #ifdef blinds
          if (at_blinds_result) {
            action = smart_array[i].action;
            if (local_log.length() > 2) {
              local_log += " & ";
            }
            local_log += "blinds";
            if (smart_array[i].blinds_offset > 0) {
              local_log += "+" + String(smart_array[i].blinds_offset);
            }
            local_log += " " + smart_array[i].at_blinds;
          }
        #endif
        #ifdef thermostat
          if (at_thermostat_result) {
            action = smart_array[i].action;
            if (local_log.length() > 2) {
              local_log += " & ";
            }
            local_log += "thermostat";
            if (smart_array[i].thermostat_offset > 0) {
              local_log += "+" + String(smart_array[i].thermostat_offset);
            }
            local_log += " " + smart_array[i].at_thermostat + "°C";
          }
        #endif
        #ifdef chain
          if (at_chain_result) {
            action = smart_array[i].action;
            if (local_log.length() > 2) {
              local_log += " & ";
            }
            local_log += "chain";
            if (smart_array[i].chain_offset > 0) {
              local_log += "+" + String(smart_array[i].chain_offset);
            }
            local_log += " " + smart_array[i].at_chain;
          }
        #endif
        if (start_time_result && end_time_result) {
          local_log += " between_hours";
          action = smart_array[i].action;
        } else {
          if (start_time_result) {
            local_log += " after time";
            action = smart_array[i].action;
          }
          if (end_time_result) {
            local_log += " before time";
            action = smart_array[i].action;
          }
        }
        if (smart_array[i].must_be_ != "?") {
          local_log += ", must_be_";
          local_log += smart_array[i].must_be_;
        }
        if (trigger > -1) {
          local_log += " (trigger: " + String(trigger) + ")";
        }

        if (action != "?") {
          local_log = (smart_array[i].any_trigger_required ? " after " : " at ") + local_log;
          if (strContains(action, ".") && strContains(action, ";") && action.indexOf(".") < action.indexOf(";")) {
            putOfflineData(action.substring(0, action.indexOf(";")), "{\"val\":\"" + action.substring(action.indexOf(";") + 1) + "\"}");
            log_text = "Action " + action + local_log;
          } else {
            #ifdef light_switch
              if (strContains(smart_array[i].what, 1) || smart_array[i].what == "?") {
                if (strContains(action, -1) || action == "0") {
                  new_light[0] = 0;
                } else {
                  if (strContains(action, 1)) {
                    new_light[0] = 1;
                  }
                }
              }
              if (strContains(smart_array[i].what, 2) || smart_array[i].what == "?") {
                if (strContains(action, -2) || action == "0") {
                  new_light[1] = 0;
                } else {
                  if (strContains(action, 2) || action == "1" || action == "100") {
                    new_light[1] = 1;
                  }
                }
              }
              if (((new_light[0] > -1 && (light[0] ? 1 : 0) != new_light[0])
              || (new_light[1] > -1 && (light[1] ? 1 : 0) != new_light[1])) && !smart_lock) {
                if (smart_array[i].what != "?") {
                  log_text = smart_array[i].what + " to ";
                }
                log_text += (smart_array[i].action != "?" ? action : (strContains(action, 1) ? "On" : "Off")) + local_log;
                result |= true;
                smart_array[i].lead_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
                if (strContains(smart_array[i].smart_string, "e(")) {
                  smart_array[i].smart_string.replace(
                    smart_array[i].smart_string.substring(smart_array[i].smart_string.indexOf("e(") + 2, smart_array[i].smart_string.indexOf(")", smart_array[i].smart_string.indexOf("e("))),
                    String(smart_array[i].lead_u_time)
                  );
                } else {
                  smart_array[i].smart_string += "e(" + String(smart_array[i].lead_u_time) + ")";
                }
              }
            #endif
            #ifdef blinds
              if (strContains(action, ";")) {
                new_destination[0] = toSteps(action.substring(0, action.indexOf(";")).toInt(), steps[0]);
                new_destination[1] = toSteps(action.substring(action.indexOf(";") + 1, action.lastIndexOf(";")).toInt(), steps[1]);
                new_destination[2] = toSteps(action.substring(action.lastIndexOf(";") + 1).toInt(), steps[2]);
              } else {
                if ((strContains(smart_array[i].what, 1) || smart_array[i].what == "?") && steps[0] > 0) {
                    new_destination[0] = toSteps(action.toInt(), steps[0]);
                }
                if ((strContains(smart_array[i].what, 2) || smart_array[i].what == "?") && steps[1] > 0) {
                  new_destination[1] = toSteps(action.toInt(), steps[1]);
                }
                if ((strContains(smart_array[i].what, 3) || smart_array[i].what == "?") && steps[2] > 0) {
                  new_destination[2] = toSteps(action.toInt(), steps[2]);
                }
              }
              if (((new_destination[0] > -1 && destination[0] != new_destination[0])
              || (new_destination[1] > -1 && destination[1] != new_destination[1])
              || (new_destination[2] > -1 && destination[2] != new_destination[2])) && !smart_lock) {
                if (smart_array[i].action != "?") {
                  if (strContains(action, ";")) {
                    log_text = action + local_log;
                  } else {
                    if (smart_array[i].what != "?") {
                      log_text = smart_array[i].what + " ";
                    }
                    log_text += action + "%" + local_log;
                  }
                } else {
                  if (smart_array[i].what != "?") {
                    log_text = smart_array[i].what + " ";
                  }
                  log_text += (action == "100" ? "Lowering" : "Lifting") + local_log;
                }
                result |= true;
                if (at_sunset_result && !calendar_twilight) {
                  smart_array[i].has_lowering_at_sunset_offset = true;
                }
                smart_array[i].lead_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
                if (strContains(smart_array[i].smart_string, "e(")) {
                  smart_array[i].smart_string.replace(
                    smart_array[i].smart_string.substring(smart_array[i].smart_string.indexOf("e(") + 2, smart_array[i].smart_string.indexOf(")", smart_array[i].smart_string.indexOf("e("))),
                    String(smart_array[i].lead_u_time)
                  );
                } else {
                  smart_array[i].smart_string += "e(" + String(smart_array[i].lead_u_time) + ")";
                }
              }
            #endif
            #ifdef thermostat
              if (strContains(action, ".")) {
                new_heating = 1;
                new_heating_temperature = action.toFloat();
              } else {
                new_heating = strContains(action, 1) ? 1 : 0;
                new_heating_temperature = 0;
              }
              if (new_heating != heating && !smart_lock) {
                log_text = (strContains(action, ".") ? ("Up to " + action + "°C") : String("Heating ") + (strContains(action, "1") ? "on" : "off")) + local_log;
                result |= true;
                smart_heating = i;
                smart_array[i].lead_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
                if (strContains(smart_array[i].smart_string, "e(")) {
                  smart_array[i].smart_string.replace(
                    smart_array[i].smart_string.substring(smart_array[i].smart_string.indexOf("e(") + 2, smart_array[i].smart_string.indexOf(")", smart_array[i].smart_string.indexOf("e("))),
                    String(smart_array[i].lead_u_time)
                  );
                } else {
                  smart_array[i].smart_string += "e(" + String(smart_array[i].lead_u_time) + ")";
                }
              }
            #endif
            #ifdef chain
              new_destination = toSteps(action.toInt(), steps);
              if (new_destination > -1 && destination != new_destination && !smart_lock) {
                if (smart_array[i].action != "?") {
                  log_text = action + "%" + local_log;
                } else {
                  log_text = (action == "100" ? "Opening" : "Closing") + local_log;
                }
                result |= true;
                if (at_sunset_result && !calendar_twilight) {
                  smart_array[i].has_lowering_at_sunset_offset = true;
                }
                smart_array[i].lead_u_time = now.unixtime() - offset - (dst ? 3600 : 0);
                if (strContains(smart_array[i].smart_string, "e(")) {
                  smart_array[i].smart_string.replace(
                    smart_array[i].smart_string.substring(smart_array[i].smart_string.indexOf("e(") + 2, smart_array[i].smart_string.indexOf(")", smart_array[i].smart_string.indexOf("e("))),
                    String(smart_array[i].lead_u_time)
                  );
                } else {
                  smart_array[i].smart_string += "e(" + String(smart_array[i].lead_u_time) + ")";
                }
              }
            #endif
          }
        }
      }
    }
  }

  if (result && !smart_lock) {
    #ifdef light_switch
      if ((new_light[0] > -1 && (light[0] ? 1 : 0) != new_light[0])
      || (new_light[1] > -1 && (light[1] ? 1 : 0) != new_light[1])) {
        if (new_light[0] > -1) {
          light[0] = strContains(new_light[0], 1);
        }
        if (new_light[1] > -1) {
          light[1] = strContains(new_light[1], 1);
        }
        note(log_text);
        setLights("smart");
        writeObjectToFile("smart", getSmartJson(true));
      }
    #endif
    #ifdef blinds
      if ((new_destination[0] > -1 && destination[0] != new_destination[0])
      || (new_destination[1] > -1 && destination[1] != new_destination[1])
      || (new_destination[2] > -1 && destination[2] != new_destination[2])) {
        for (int j = 0; j < 3; j++) {
          if (new_destination[j] > -1 && steps[j] > 0) {
            destination[j] = new_destination[j];
          }
        }
        note(log_text);
        prepareRotation("smart");
        writeObjectToFile("smart", getSmartJson(true));
      }
    #endif
    #ifdef thermostat
      if (heating_time == 0 && downtime == 0 && (vacation == 0 || (RTCisrunning() && vacation < now.unixtime()))) {
        if ((new_heating > -1 && new_heating != heating) || new_heating_temperature != heating_temperature) {
          if (new_heating > -1 && new_heating != heating) {
            heating = new_heating;
          }
          if (new_heating_temperature > -1) {
            heating_temperature = new_heating_temperature;
          }
          note(log_text);
          setHeating(heating, "smart");
          writeObjectToFile("smart", getSmartJson(true));
        }
      }
      if (!heating) {
        smart_heating = -1;
      }
    #endif
    #ifdef chain
      if (new_destination > -1 && destination != new_destination) {
        if (new_destination > -1 && steps > 0) {
          destination = new_destination;
        }
        note(log_text);
        prepareRotation("smart");
        writeObjectToFile("smart", getSmartJson(true));
      }
    #endif
  }

  #ifdef thermostat
    if (!heating && temperature < minimum_temperature) {
      heating = true;
      heating_temperature = minimum_temperature;
      setHeating(heating, "minimum");
    }
  #endif
}


void connectingToWifi(bool use_wps) {
  if (ssid.length() == 0 && password.length() == 0) {
    initiatingWPS();
    return;
  }

  String log_text = "Connecting to Wi-Fi";
  Serial.print("\n" + log_text);


  WiFi.mode(WIFI_STA);

  if (ssid.length() > 0 && password.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    WiFi.begin();
  }

  int timeout = 0;
  while (timeout++ < 10 && WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    delay(250);
  }

  bool result = WiFi.status() == WL_CONNECTED;

  if (result) {
    log_text = "Connected to " + WiFi.SSID();
    log_text += " : " + WiFi.localIP().toString();
    if (password.length() == 0) {
      password = WiFi.psk();
        saveSettings(false);
    }
  } else {
    log_text += " timed out";
  }
  note(log_text);

  if (result) {
    startServices();
    WiFi.setAutoReconnect(true);
    auto_reconnect = true;
  } else {
    if (use_wps) {
      initiatingWPS();
    }
  }
}

void initiatingWPS() {
  String log_text = "Initiating WPS";
  Serial.print("\n" + log_text);


  WiFi.mode(WIFI_STA);

  WiFi.begin("idom", "");
  int timeout = 0;
  while (timeout++ < 10 && WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    delay(250);
  }

  bool result = WiFi.beginWPSConfig();
  result &= String(WiFi.SSID()).length() > 0;

  if (result) {
    ssid = WiFi.SSID();
    password = WiFi.psk();

    log_text += " finished. ";
    log_text += "Connected to " + WiFi.SSID();
    log_text += " : " + WiFi.localIP().toString();
  } else {
    log_text += " timed out";
  }
  note(log_text);

  if (result) {
    saveSettings();
    startServices();
    WiFi.setAutoReconnect(true);
    auto_reconnect = true;
  }
}


void activationTheLog() {
  if (keep_log) {
    server.send(200, "text/plain", "Done");
    return;
  }

  File file = LittleFS.open("/log.txt", "a");
  if (file) {
    file.println();
    file.close();
  }
  last_accessed_log = 0;
  saveSettings(false);
  keep_log = true;

  server.send(200, "text/plain", "The log has been activated");
}

void deactivationTheLog() {
  if (!keep_log) {
    server.send(200, "text/plain", "Done");
    return;
  }

  if (LittleFS.exists("/log.txt")) {
    LittleFS.remove("/log.txt");
  }
  last_accessed_log = 0;
  saveSettings(false);
  keep_log = false;

  server.send(200, "text/plain", "The log has been deactivated");
}

void requestForLogs() {
  File file = LittleFS.open("/log.txt", "r");
  if (!file) {
    server.send(404, "text/plain", "No log file");
    return;
  }

  server.setContentLength(file.size() + String("Log file\nHTTP/1.1 200 OK").length());
  server.send(200, "text/plain", "Log file\n");
  while (file.available()) {
    server.sendContent(String(char(file.read())));
  }
  file.close();

  last_accessed_log = 0;
  saveSettings(false);

  server.send(200, "text/plain", "Done");
}

void clearTheLog() {
  File file = LittleFS.open("/log.txt", "w");
  if (!file) {
    server.send(404, "text/plain", "Failed!");
    return;
  }

  file.println();
  file.close();

  server.send(200, "text/plain", "The log file was cleared");
}


void getSunriseSunset(DateTime now) {
  if (geo_location.length() < 2) {
    return;
  }

  sun.setCurrentDate(now.year(), now.month(), now.day());

  next_sunset = sun.calcSunset() + (offset > 0 ? offset / 60 : 0) + (dst ? 60 : 0);
  next_sunrise = sun.calcSunrise() + (offset > 0 ? offset / 60 : 0) + (dst ? 60 : 0);
  last_sun_check = now.day();
  note("Sunrise: " + String(next_sunrise) + " / Sunset: " + String(next_sunset));
  if (calendar_twilight != !(next_sunrise < (now.hour() * 60) + now.minute() && (now.hour() * 60) + now.minute() < next_sunset)) {
    calendar_twilight = !calendar_twilight;
    saveSettings();
  }
}

int findMDNSDevices() {
  int n = MDNS.queryService("idom", "tcp");

  if (n > 0) {
    delete [] devices_array;
    devices_array = new Device[n];
    devices_count = n;

    for (int i = 0; i < n; ++i) {
      devices_array[i].ip = String(MDNS.IP(i)[0]) + '.' + String(MDNS.IP(i)[1]) + '.' + String(MDNS.IP(i)[2]) + '.' + String(MDNS.IP(i)[3]);
    }
  }

  return n;
}

void receivedOfflineData() {
  if (server.hasArg("plain")) {
    server.send(200, "text/plain", "Data has received");
    readData(server.arg("plain"), true);
    return;
  }

  server.send(200, "text/plain", "Body not received");
}

void putOfflineData(String url, String data) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (wifiClient.available() == 0) {
    wifiClient.stop();
  }

  httpClient.begin(wifiClient, "http://" + url + "/set");
  httpClient.addHeader("Content-Type", "text/plain");
  int http_code = httpClient.PUT(data);

  if (http_code == HTTP_CODE_OK) {
    note("Data transfer to:\n " + url + ": " + data);
  } else {
    note("Data transfer to:\n " + url + " - error "  + http_code);
  }

  httpClient.end();
}

void putMultiOfflineData(String data) {
  putMultiOfflineData(data, true);
}

void putMultiOfflineData(String data, bool log) {
  if (WiFi.status() != WL_CONNECTED || data.length() < 2) {
    return;
  }

  int count = findMDNSDevices();
  if (count == 0) {
    return;
  }

  int http_code;
  String log_text = "";

  for (int i = 0; i < count; i++) {
    if (wifiClient.available() == 0) {
      wifiClient.stop();
    }

    httpClient.begin(wifiClient, "http://" + devices_array[i].ip + "/set");
    httpClient.addHeader("Content-Type", "text/plain");
    http_code = httpClient.PUT(data);

    if (log) {
      if (http_code == HTTP_CODE_OK) {
        log_text += "\n " + devices_array[i].ip;
      } else {
        log_text += "\n " + devices_array[i].ip + " - error "  + http_code;
      }
    }

    httpClient.end();
  }

  if (log) {
    note(data + " transfer to " + String(count) + ":" + log_text);
  }
}

void getOfflineData() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  int count = findMDNSDevices();
  if (count == 0) {
    return;
  }

  int http_code;
  String data;
  String log_text = "";

  for (int i = 0; i < count; i++) {
    if (wifiClient.available() == 0) {
      wifiClient.stop();
    }

    httpClient.begin(wifiClient, "http://" + devices_array[i].ip + "/basicdata");
    httpClient.addHeader("Content-Type", "text/plain");
    http_code = httpClient.POST("");

    if (http_code == HTTP_CODE_OK) {
      if (httpClient.getSize() > 15) {
        data = httpClient.getString();
        log_text +=  "\n " + devices_array[i].ip + ": ";
        if (strContains(data, "ip")) {
          log_text += "{*," + data.substring(data.indexOf("\"offset"));
        } else {
          log_text += data;
        }
        readData(data, true);
      }
    } else {
      log_text += "\n " + devices_array[i].ip + ": error " + http_code;
    }

    httpClient.end();
  }

  note("Received data..." + log_text);
}

void setupOTA() {
  ArduinoOTA.setHostname(host_name);

  ArduinoOTA.onEnd([]() {
    note("Software update over Wi-Fi");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String log_text = "OTA ";
    if (error == OTA_AUTH_ERROR) {
      log_text += "Auth";
    } else if (error == OTA_BEGIN_ERROR) {
      log_text += "Begin";
    } else if (error == OTA_CONNECT_ERROR) {
      log_text += "Connect";
    } else if (error == OTA_RECEIVE_ERROR) {
      log_text += "Receive";
    } else if (error == OTA_END_ERROR) {
      log_text += "End";
    }
    note(log_text + String(" failed!"));
  });

  ArduinoOTA.begin();
}

void getSmartDetail() {
  String result;
  serializeJson(getSmartJson(false), result);
  server.send(200, "text/plain", result);
}

void getRawSmartDetail() {
  String result;
  serializeJson(getSmartJson(true), result);
  server.send(200, "text/plain", result);
}
