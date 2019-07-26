#include <Arduino.h>

const char device[7] = "blinds";
const bool bipolar = true;

const int light_sensor_pin = A0;

const int bipolar_enable_pin = D3;
const int bipolar_direction_pin = D6;
const int bipolar_step_pin[] = {D5, D7, D8}; // stepper1, stepper2, stepper3

const int unipolar_pin[] = {D5, D6, D7, D8};

int boundary = 300;
int steps1 = 0;
int steps2 = 0;
int steps3 = 0;
bool reversed = false;

struct Smart {
  String wing;
  String days;
  bool loweringAtNight;
  bool liftingAtDay;
  int loweringTime;
  int liftingTime;
  bool enabled;
  uint32_t access;
};

int destination1 = 0;
int destination2 = 0;
int destination3 = 0;
int actual1 = 0;
int actual2 = 0;
int actual3 = 0;
int step = 0;

bool measurement = false;
int calibration = 0;
int wings = 123;

int light = -1;
uint32_t twilightCounter = 0;
uint32_t sunset = 0;
uint32_t sunrise = 0;

void setStepperPins(bool setMode);
String toPercentages(int value, int steps);
int toSteps(int value, int steps);
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
bool hasTheLightChanged();
void readData(String payload, bool perWiFi);
void setSmart();
void checkSmart(bool lightChanged);
void prepareCalibration(int set);
void prepareRotation();
void rotation();
void bipolarRotation();
void unipolarRotation();
