#include "core.h"
#include <ESP8266httpUpdate.h>

const String base_url = "";

uint32_t update_time = 0;
bool sending_error = false;
bool block_put_online_data = false;
bool block_get_online_data = false;
int online_data_timeout = -1;

//This functions is only available with a ready-made iDom device.

void activationOnlineMode();
void deactivationOnlineMode();
void manualUpdate();
void checkForUpdate(bool force);
void getTime();
void putOnlineData(String data);
void putOnlineData(String data, bool bypass) ;
void putOnlineData(String variant, String data);
void putOnlineData(String data, bool logs, bool flawless);
void putOnlineData(String variant, String data, bool logs, bool flawless, bool bypass);
void getOnlineData();
void readOnlineData(String payload);


void activationOnlineMode() {}
void deactivationOnlineMode() {}
void manualUpdate() {}
void checkForUpdate(bool force) {}
void getTime() {}
void putOnlineData(String data) {}
void putOnlineData(String data, bool bypass) {}
void putOnlineData(String variant, String data) {}
void putOnlineData(String data, bool logs, bool flawless) {}
void putOnlineData(String variant, String data, bool logs, bool flawless, bool bypass) {}
void getOnlineData() {}
void readOnlineData(String payload) {}
