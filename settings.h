#pragma once

#include <Timezone.h>

extern String ssid;
extern String passwd;
extern Timezone *timezone;
extern bool showPM;

void settingsInit();

bool getStringSetting(const char *tag, char *value, size_t valueSize);
void setStringSetting(const char *tag, const char *value);

bool getBooleanSetting(const char *tag, bool &value);
void setBooleanSetting(const char *tag, bool value);
