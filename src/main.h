#include <Arduino.h>

const String device = "blinds";

const int stepper_pin[] = {D5, D6, D7, D8};
const int light_sensor_pin = A0;

int boundary = 60;
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

int light = -1;
uint32_t twilightLoopTime = 0;
uint32_t sunset = 0;
uint32_t sunrise = 0;

void setStepperPins(bool setMode);
String toPercentages(int value);
int toSteps(int value);
void readSettings();
void saveSettings();
void resume();
void saveTheState();
void sayHelloToTheServer() ;
void startServices();
void handshake();
void requestForState();
void requestForBasicData();
void reverseDirection();
void resetCoverage();
void makeMeasurement();
bool lightHasChanged();
void readData(String payload, bool perWiFi);
void setSmart();
void checkSmart(bool lightHasChanged);
void setCoverage(int set, bool calibrate);
void rotation();
