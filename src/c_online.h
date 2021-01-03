#include "core.h"

const String base_url = "";

uint32_t update_time = 0;
bool sending_error = false;
bool block_get_online_data = false;
bool block_put_online_data = false;

//This functions is only available with a ready-made iDom device.

void activationOnlineMode();
void deactivationOnlineMode();
void putOnlineData(String variant, String data);
void putOnlineData(String variant, String data, bool logs, bool flawless);
void getOnlineData();
void readMultiOnlineData(String payload);


void activationOnlineMode() {}
void deactivationOnlineMode() {}
void putOnlineData(String variant, String data) {}
void putOnlineData(String variant, String data, bool logs, bool flawless) {}
void getOnlineData() {}
void readMultiOnlineData(String payload) {}
