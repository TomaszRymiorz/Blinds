#include "core.h"

const String baseURL = "";

String networkedDevices;
bool prime;

uint32_t updateTime = 0;
bool sendingError = false;
bool blockGetOnlineData = false;

//This functions is only available with a ready-made iDom device.

void activationOnlineMode();
void deactivationOnlineMode();
void putOnlineData(String variant, String values);
void putOnlineData(String variant, String values, bool logs);
void getOnlineData();
void getOnlineData(bool criterion);
void readMultiOnlineData(String payload);

void activationOnlineMode() {}
void deactivationOnlineMode() {}
void putOnlineData(String variant, String values) {}
void putOnlineData(String variant, String values, bool logs) {}
void getOnlineData() {}
void getOnlineData(bool criterion) {}
void readMultiOnlineData(String payload) {}
