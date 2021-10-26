#include <Arduino.h>

const char device[7] = "blinds";
const char smart_prefix = 'b';
const int version = 20;

const int light_sensor_pin = A0;

const int bipolar_enable_pin[] = {D6, D7, D8}; // stepper1, stepper2, stepper3
const int bipolar_direction_pin = D5;
const int bipolar_step_pin = D3;

int boundary = 200;
int steps1 = 0;
int steps2 = 0;
int steps3 = 0;
bool reversed = false;
bool separately = false;
bool inverted_sequence = false;
bool tandem = false;
int fixit = 0;

struct Smart {
  bool enabled;
  String days;
  String wing;
  int target;
  int time;
  int lifting_time;
  bool at_night;
  bool at_night_and_time;
  bool at_day;
  bool at_day_and_time;
  bool react_to_cloudiness;
  uint32_t access;
};

int destination1 = 0;
int destination2 = 0;
int destination3 = 0;
int actual1 = 0;
int actual2 = 0;
int actual3 = 0;

bool measurement = false;
int wings = 123;

int light = -1;
int twilight_counter = 0;
int daybreak_counter = 0;
uint32_t sunset = 0;
uint32_t sunrise = 0;
bool block_twilight_counter = false;
bool twilight = false;
bool twilight_sensor = false;
bool cloudiness = false;
bool lock = false;

void setStepperOff();
String toPercentages(int value, int steps);
int toSteps(int value, int steps);
bool readSettings(bool backup);
void saveSettings();
void saveSettings(bool log);
void resume();
void saveTheState();
void sayHelloToTheServer();
void introductionToServer();
void startServices();
String getBlindsDetail();
String getValue();
String getBlindsPosition();
String getSensorDetail();
void handshake();
void requestForState();
void exchangeOfBasicData();
void setMin();
void setMax();
void setAsMax();
void makeMeasurement();
void cancelMeasurement();
void endMeasurement();
void initiateTheLightSensor();
void deactivateTheLightSensor();
bool hasTheLightChanged();
void readData(String payload, bool per_wifi);
void setSmart();
bool automaticSettings();
bool automaticSettings(bool light_changed);
void prepareRotation(String orderer);
void calibration(int set, bool bypass);
void measurementRotation();
void rotation();
