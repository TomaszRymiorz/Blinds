#include "core.h"

const String base_url = "";

String networked_devices = "";
bool prime = false;

uint32_t update_time = 0;
bool sending_error = false;
bool block_get_online_data = false;

//This functions is only available with a ready-made iDom device.

void activationOnlineMode();
void deactivationOnlineMode();
void confirmationOfPriority();
void putOnlineData(String variant, String values);
void putOnlineData(String variant, String values, bool logs);
void getOnlineData();
void readMultiOnlineData(String payload);

void activationOnlineMode() {}
void deactivationOnlineMode() {}
void confirmationOfPriority() {}
void putOnlineData(String variant, String values) {}
void putOnlineData(String variant, String values, bool logs) {}
void getOnlineData() {}
void readMultiOnlineData(String payload) {}
