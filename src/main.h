#include <Arduino.h>

#define physical_clock

const char device[7] = "blinds";
const char smart_prefix = 'b';
const int version = 27;

const int light_sensor_pin = A0;

const int bipolar_enable_pin[] = {D6, D7, D8}; // stepper1, stepper2, stepper3
const int bipolar_direction_pin = D5;
const int bipolar_step_pin = D3;

int default_boundary = 200;
int boundary = default_boundary;
int steps1 = 0;
int steps2 = 0;
int steps3 = 0;
bool reversed = false;
bool separately = false;
bool inverted_sequence = false;
bool tandem = false;
int fixit1 = 0;
int fixit2 = 0;
int fixit3 = 0;
int cycles1 = 0;
int cycles2 = 0;
int cycles3 = 0;

struct Smart {
  bool enabled;
  String days;
  String wing;
  int action;
  int at_time;
  int end_time;
  bool at_sunset;
  bool at_sunrise;
  bool at_dusk;
  bool at_dawn;
  bool any_required;
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
uint32_t overstep = 0;
bool block_twilight_counter = false;
bool twilight = false;
bool twilight_sensor = false;
bool lock = false;

String toPercentages(int value, int steps);
int toSteps(int value, int steps);
bool readSettings(bool backup);
void saveSettings();
void saveSettings(bool log);
void resume();
void saveTheState();
String getBlindsDetail();
String getValue();
String getPosition();
String getSensorDetail(bool basic);
String getCycles();
String getFixit();
void sayHelloToTheServer();
void introductionToServer();
void startServices();
void handshake();
void requestForState();
void exchangeOfBasicData();
void readData(String payload, bool per_wifi);
bool hasTheLightChanged();
bool automaticSettings();
bool automaticSettings(bool light_changed);
void setSmart();
void setMin();
void setMax();
void setAsMax();
void initiateTheLightSensor();
void deactivateTheLightSensor();
void makeMeasurement();
void cancelMeasurement();
void endMeasurement();
void setStepperOff();
void prepareRotation(String orderer);
void calibration(int set, bool positioning, bool fixit);
void measurementRotation();
void rotation();
