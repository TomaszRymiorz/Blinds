#include <Arduino.h>

const String device = "blinds";
const bool bipolar = true;
const int wings = bipolar ? 2 : 1;

const int light_sensor_pin = A0;

const int bipolar_enable_pin = D3;
const int bipolar_direction_pin = D6;
const int bipolar_pin[] = {D5, D7}; // stepper1, stepper2

const int unipolar_pin[] = {D5, D6, D7, D8};

int boundary = 300;
int steps = 0;
bool reversed = false;

struct Smart {
  String days;
  bool loweringAtNight;
  bool liftingAtDay;
  int loweringTime;
  int liftingTime;
  bool enabled;
  uint32_t access;
};

int destination = 0;
int actual = 0;
int step = 0;

bool measurement = false;
int calibration = 0;
int calibration_wings = 0;

int light = -1;
uint32_t twilightCounter = 0;
uint32_t sunset = 0;
uint32_t sunrise = 0;

void setStepperPins(bool setMode);
String toPercentages(int value);
int toSteps(int value);
bool readSettings(bool backup);
void saveSettings();
void resume();
void saveTheState();
void sayHelloToTheServer() ;
void startServices();
void handshake();
void requestForState();
void requestForBasicData();
void reverseDirection();
void setMin();
void setMax();
void makeMeasurement();
void cancelMeasurement();
void endMeasurement();
bool lightHasChanged();
void readData(String payload, bool perWiFi);
void setSmart();
void checkSmart(bool lightChanged);
void setCoverage(int set, bool calibrate);
void rotation();
void bipolarRotation();
void unipolarRotation();
