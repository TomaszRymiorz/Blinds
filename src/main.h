#include <Arduino.h>

const String device = "blinds";

const int stepper_pin[] = {D5, D6, D7, D8};
const int light_sensor_pin = A0;

int dayBoundary = 60;

struct Smart {
  String days;
  bool loweringAtNight;
  bool liftingAtDay;
  int loweringTime;
  int liftingTime;
  bool enabled;
  uint32_t access;
};

int coverage = 0;
float destination = 0;
int actual = 0;
int step = 0;

int steps = 0;
bool reversed = false;

bool measurement = false;
int measure = 0;

uint32_t twilightLoopTime = 0;
uint32_t sunset = 0;
uint32_t sunrise = 0;

int light = 0;

void setupStepperPins(bool mode);
void readSettings();
void resume();
void saveTheState();
void saveTheSettings();
void sayHelloToTheServer() ;
void startRestServer();
void handshake();
void requestForState();
void reverseDirection();
void resetCoverage();
void makeMeasurement();
bool lightHasChanged();
void readData(String payload, bool perWiFi);
void setSmart();
void checkSmart(bool lightHasChanged);
void setCoverage(int set, bool calibrate);
void rotation();
