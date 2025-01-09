#include <Preferences.h>
#define RW_MODE false
#define RO_MODE true

#include "settings.h"

Preferences prefs;

String ssid;
String passwd;
String timezoneName;
Timezone *timezone;
bool showPM;

// US Eastern Time Zone
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};    // Daylight time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First,  Sun, Nov, 2, -300};    // Standard time = UTC - 5 hours
Timezone usEastern(usEDT, usEST);

// US Central Time Zone
TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};    // Daylight time = UTC - 5 hours
TimeChangeRule usCST = {"CST", First,  Sun, Nov, 2, -360};    // Standard time = UTC - 6 hours
Timezone usCentral(usCDT, usCST);

// US Mountain Time Zone
TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};    // Daylight time = UTC - 6 hours
TimeChangeRule usMST = {"MST", First,  Sun, Nov, 2, -420};    // Standard time = UTC - 7 hours
Timezone usMountain(usMDT, usMST);

// Arizona is US Mountain Time Zone but does not use DST
Timezone usAZ(usMST);

// US Pacific Time Zone
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};    // Daylight time = UTC - 7 hours
TimeChangeRule usPST = {"PST", First,  Sun, Nov, 2, -480};    // Standard time = UTC - 8 hours
Timezone usPacific(usPDT, usPST);

// Finland Time Zone
TimeChangeRule finlandDT = {"FDT", Last, Sun, Mar, 3, 180};   // Daylight time = UTC + 3 hours
TimeChangeRule finlandST = {"FST", Last,  Sun, Oct, 4, 120};  // Standard time = UTC + 2 hours
Timezone finland(finlandDT, finlandST);

Timezone *findTimezone(const char *name);

void settingsInit()
{
  prefs.begin("Settings", RW_MODE);

  //prefs.clear();

  ssid = prefs.getString("SSID", String());
  passwd = prefs.getString("Password", String());

  String timezoneStr = prefs.getString("Timezone", String("US-Eastern"));
  setTimezoneSetting(timezoneStr);

  String showPMstr = prefs.getString("ShowPM", String("false"));
  showPM = strcmp(showPMstr.c_str(), "true") == 0;

  prefs.end();
}

void setTimezoneSetting(String timezoneStr)
{
  Timezone *newTimezone = findTimezone(timezoneStr.c_str());
  if (newTimezone == NULL) {
    Serial.printf("No timezone '%s'\n", timezoneStr.c_str());
  }
  else {
    Serial.printf("Timezone is '%s'\n", timezoneStr.c_str());
    timezoneName = timezoneStr;
    timezone = newTimezone;
  }
}

bool getStringSetting(const char *tag, char *value, size_t valueSize)
{
    bool result = false;
    prefs.begin("Settings", RO_MODE);
    if (prefs.isKey(tag)) {
      String str = prefs.getString(tag);
      const char *valueStr = str.c_str();
      size_t valueLength = strlen(valueStr);
      if (valueLength >= valueSize) {
        valueLength = valueSize - 1;
      }
      strncpy(value, valueStr, valueLength);
      value[valueLength] = '\0';
      result = true;
    }
    prefs.end();
    return result;
}

void setStringSetting(const char *tag, const char *value)
{
  prefs.begin("Settings", RW_MODE);
  prefs.putString(tag, value);
  prefs.end();
}

bool getBooleanSetting(const char *tag, bool &value)
{
    bool result = false;
    prefs.begin("Settings", RO_MODE);
    if (prefs.isKey(tag)) {
      value = prefs.getBool(tag);
      result = true;
    }
    prefs.end();
    return result;
}

void setBooleanSetting(const char *tag, bool value)
{
  prefs.begin("Settings", RW_MODE);
  prefs.putBool(tag, value);
  prefs.end();
}

struct NamedTimezone {
  const char *name;
  Timezone *timezone;
};

NamedTimezone timezones[] = {
  { "US-Eastern",   &usEastern  },
  { "US-Central",   &usCentral  },
  { "US-Mountain",  &usMountain },
  { "US-Arizona",   &usAZ       },
  { "US-Pacific",   &usPacific  },
  { "Finland",      &finland    }
};
int timezoneCount = sizeof(timezones) / sizeof(timezones[0]);

Timezone *findTimezone(const char *name)
{
  for (int i = 0; i < timezoneCount; ++i) {
    if (strcmp(name, timezones[i].name) == 0) {
      return timezones[i].timezone;
    }
  }
  return NULL;
}
