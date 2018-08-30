#include <Arduino.h>

const String device = "blinds"; //switch

const int sd_pin = D8;
const int twilight_pin = A0;
const int stepper_pin = D0;
const int multiplexer_pin[] = {D5, D6, D7};

struct Smart {
  String days;
  bool loweringAtNight;
  bool liftingAtDay;
  uint32_t blackout;
  int loweringTime;
  int liftingTime;
  uint32_t access;
};

int coverage = 0;
float destination = 0;
int actual = 0;

int steps = 0;
bool reversed = false;
const int lag = 4;

bool measurement = false;
int measure = 0;
int calibration = 0;

uint32_t twilightLoopTime = 0;
uint32_t sunset = 0;
uint32_t sunrise = 0;

void setupStepperPins();
void uprisingsCounter();
void readSteps();
void readCoverage();
void readSunset();
void readSunrise();
void writeSteps();
void writeCoverage();
void startRestServer();
void handshake();
void requestForState();
void receivedTheData();
void reverseDirection();
void resetCoverage();
void makeMeasurement();
bool daylightHasChanged();
int readData(String payload);
void setSmart();
void checkSmart();
void setCoverage(int set, bool calibrate);
void cover(int lag);
void uncover(int lag);
void selectMuxPin(byte pin);
