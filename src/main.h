#include <Arduino.h>

#define physical_clock
#define blinds

const char device[7] = "blinds";
const char smart_prefix = 'b';
const uint8_t version = 30;

const int light_sensor_pin = A0;

const int bipolar_enable_pin[] = {D6, D7, D8}; // stepper1, stepper2, stepper3
const int bipolar_direction_pin = D5;
const int bipolar_step_pin = D3;

const int default_boundary = 200;
int boundary = default_boundary;
bool reversed = false;
bool separately = false;
bool inverted_sequence = false;
bool tandem = false;
int fixit[] = {0, 0, 0};
int cycles[] = {0, 0, 0};
int day_night[] = {0, 0, 0};

int steps[] = {0, 0, 0};
int destination[] = {0, 0, 0};
int actual[] = {0, 0, 0};

bool measurement = false;
int wings = 123;

bool has_a_sensor = false;
uint32_t dusk_u_time = 0;
uint32_t dawn_u_time = 0;
uint32_t overstep_u_time = 0;
int daybreak_counter = 0;
int twilight_counter = 0;
bool block_twilight_counter = false;

String toPercentages(int value, int steps);
int toSteps(int value, int steps);
bool readSettings(bool backup);
void saveSettings();
void saveSettings(bool log);
void resume();
void saveTheState();
String getFixit();
String getFixit(String separator);
String getCycles();
String getCycles(String separator);
String getDayNight();
String getDayNight(String separator);
String getSteps();
String getSteps(String separator);
String getValue();
String getValue(String separator);
int getValue(int number);
String getActual();
String getActual(bool complete);
String getSensorDetail(bool basic);
void startServices();
void handshake();
void requestForState();
void exchangeOfBasicData();
void readData(const String& payload, bool per_wifi);
void automation();
int hasTheLightChanged();
void smartAction();
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
void calibration(int set, bool positioning);
void measurementRotation();
void rotation();
