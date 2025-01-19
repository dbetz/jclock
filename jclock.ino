/********************
 * Jonathan's Clock Program
 * Based on:
 *  The AdaFruit LED Backpack sample program
 *  and
 *  www.geekstips.com
 *  Arduino Time Sync from NTP Server using ESP8266 WiFi module 
 *  Arduino code example
 ********************/

/*

Medidation bell sounds:

https://pixabay.com/sound-effects/search/meditation-bell/

Daylight Savings Time

begins at 2:00 a.m. on the second Sunday of March (at 2 a.m. the local time time skips ahead to 3 a.m. so there is one less hour in that day)
ends at 2:00 a.m. on the first Sunday of November (at 2 a.m. the local time becomes 1 a.m. and that hour is repeated, so there is an extra hour in that day)

*/

#include <Wire.h>
#include <TimeLib.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include "Adafruit_seesaw.h"
#include <seesaw_neopixel.h>
#include "Adafruit_MAX1704X.h"
#include <debounce.h>
#include <DS3231.h>
#include "Audio.h"
#include "SD.h"
#include "LittleFS.h"

#include <Preferences.h>
#define RW_MODE false
#define RO_MODE true

#include <WiFi.h>
#include <WiFiClient.h>

#include "settings.h"
#include "configServer.h"

// Libraries:
// ESP32-audioI2S

// Board: Adafruit Feather ESP32-S3
// Flash Mode: QIO 80MHz
// Flash Size: 4MG (32Mb)
// Partition Scheme: Huge APP (3MB No OTA, 1MB SPIFFS)

/* wiring: (module pin - ESP32 pin)

i2s Audio Module
================
VIN - 3v3
GND - GND
SD -
GAIN - GND
DIN - 12
BCLK - 11
LRC - 13

SD Module
=========
GND - GND
MISO - MI
CLK - SCK
MOSI - MO
CS - 5
3v3 - 3v3

Config Mode Button
GND - 6

*/

// microSD Card Reader connections
#define SD_CS          5
#define SPI_MOSI      35 
#define SPI_MISO      37
#define SPI_SCK       36
 
// I2S Connections
#define I2S_DOUT      12
#define I2S_BCLK      11
#define I2S_LRC       13
 
RTClib rtcLib;
DS3231 rtc;

Adafruit_MAX17048 maxlipo;

// Create Audio object
Audio audio;
 
#define SS_SWITCH       24
#define SS_NEOPIX       6

#define BUTTON_A        14
#define BUTTON_B        15
//#define BUTTON_C        17
//#define BUTTON_D        18

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);

struct Parameter {
  int index;
  const char *name;
  int lowLimit;
  int highLimit;
  int *pValue;
};

// start and end pauses in seconds
#define START_PAUSE_IN_SECS 5
#define END_PAUSE_IN_SECS   5

int meditationInterval = 20;
int pomodoroWorkInterval = 25;
int pomodoroWorkPeriodCount = 5;
int pomodoroShortRestInterval = 5;
int pomodoroLongRestInterval = 15;

Parameter parameters[] = {
{ 1, "mTime",       1, 99, &meditationInterval        },
{ 2, "pWork",       1, 99, &pomodoroWorkInterval      },
{ 3, "pCount",      1, 99, &pomodoroWorkPeriodCount   },
{ 4, "pShortRest",  1, 99, &pomodoroShortRestInterval },
{ 5, "pLongRest",   1, 99, &pomodoroLongRestInterval  }
};
int parameterCount = sizeof(parameters) / sizeof(parameters[0]);

#define MEDITATION_INDEX 1

Parameter *selectedParameter = &parameters[0];
bool immediate = false;

void selectParameter(int index)
{
  selectedParameter = &parameters[index - 1];
}

void setParameterValue(Parameter *parameter, int value)
{
  setIntSetting(parameter->name, value);
  *parameter->pValue = value;
}

int getParameterValue(Parameter *parameter)
{
  return *parameter->pValue;
}

enum class ClockState {
  TIME,
  SELECTING,
  ADJUSTING,
  START_PAUSE,
  COUNTING,
  POMODORO_WORK,
  POMODORO_SHORT_REST,
  POMODORO_LONG_REST,
  END_PAUSE
};

ClockState clockState = ClockState::TIME;
ClockState nextClockState;
int nextTimeInterval;

int32_t encoderCount;
unsigned long encoderLastMoved;

unsigned long timeRemaining;
unsigned long secondStartTime;
int shortIntervalsRemaining;

unsigned int localPort = 2390;      // local port to listen for UDP packets

#define SDA_PIN 3
#define SCL_PIN 4

#define I2C_POWER   0x36
#define I2C_ENCODER 0x37
#define I2C_RTC     0x68
#define I2C_DISPLAY 0x70

uint8_t encoderBtnState = BTN_OPEN;
bool encoderBtnChanged = false;

static void buttonHandler(uint8_t btnId, uint8_t btnState) 
{
  switch (btnId) {
    case 0: // encoder button
      encoderBtnState = btnState;
      encoderBtnChanged = true;
      break;
    case 1: // button A
      startMeditationTimer();
      break;
    case 2: // button B
      startPomodoroTimer();
      break;
    case 3: // button C
      break;
    case 4: // button D
      break;
  }
}

Button encoderButton(0, buttonHandler);
Button buttonA(1, buttonHandler);
Button buttonB(2, buttonHandler);
//Button buttonC(3, buttonHandler);
//Button buttonD(4, buttonHandler);

/* Don't hardwire the IP address or we won't get the benefits of the pool.
 *  Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress localIP;
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

uint8_t packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

// query the NTP server every 10 minutes
#define NTP_INTERVAL  (10 * 60 * 1000)
//#define NTP_INTERVAL  (10 * 1000)

unsigned long lastNtpRequestTime;

Adafruit_7segment display = Adafruit_7segment();

unsigned long batterySampleTime;
bool flashFilesystemMounted = false;
bool sdFilesystemMounted = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);
                
  Serial.printf("jclock\n");

  settingsInit();

  getIntSetting("mTime",      meditationInterval);
  getIntSetting("pWork",      pomodoroWorkInterval);
  getIntSetting("pCount",     pomodoroWorkPeriodCount);
  getIntSetting("pShortRest", pomodoroShortRestInterval);
  getIntSetting("pLongRest",  pomodoroLongRestInterval);

  Serial.printf("meditationInterval:        %d\n", meditationInterval);
  Serial.printf("pomodoroWorkInterval:      %d\n", pomodoroWorkInterval);
  Serial.printf("pomodoroWorkPeriodCount:   %d\n", pomodoroWorkPeriodCount);
  Serial.printf("pomodoroShortRestInterval: %d\n", pomodoroShortRestInterval);
  Serial.printf("pomodoroLongRestInterval:  %d\n", pomodoroLongRestInterval);

  if (maxlipo.begin()) {
    Serial.printf("Found MAX17048 with Chip ID: 0x%02x\n", maxlipo.getChipID()); 
  }
  else {
    Serial.printf("Can't find MAX17048\n");
  }

  // start the flash filesystem
  if (LittleFS.begin()) {
    flashFilesystemMounted = true;
  }
  else {
    Serial.printf("An Error has occurred while mounting LittleFS\n");
  }

  // Start microSD Card
  if (SD.begin(SD_CS)) {
    sdFilesystemMounted = true;
  }
  else {
    Serial.printf("Error accessing microSD card!\n");
  }
  
  // Setup I2S 
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  
  // Set Volume
  audio.setVolume(10);
  
  Wire.begin(SDA_PIN, SCL_PIN);

  display.begin(I2C_DISPLAY);

  if (!ss.begin(I2C_ENCODER) || !sspixel.begin(I2C_ENCODER)) {
    Serial.printf("Couldn't find seesaw on default address\n");
    while(1) delay(10);
  }
  Serial.printf("seesaw started\n");

  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991){
    Serial.printf("Wrong firmware loaded? %u\n", version);
    while(1) delay(10);
  }
  Serial.printf("Found Product 4991\n");

  // set not so bright!
  sspixel.setBrightness(20);
  sspixel.show();
  
  // use a pin for the built in encoder switch
  ss.pinMode(SS_SWITCH, INPUT_PULLUP);

  // button pins
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  //pinMode(BUTTON_C, INPUT_PULLUP);
  //pinMode(BUTTON_D, INPUT_PULLUP);

  // go back to the clock display if the encoder hasn't been moved in 10 seconds
  encoderLastMoved = millis() - 10000;

  Serial.printf("Turning on interrupts\n");
  delay(10);
  ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
  ss.enableEncoderInterrupt();

  // We start by connecting to a WiFi network and starting the configuration web server
  configServerStart(ssid, passwd);
  batterySampleTime = millis();
}

bool udpStarted = false;
bool getNTPserver = false;
bool requestTime = false;
bool parseTime = false;

void loop()
{
  unsigned long currentTime = millis();

  audio.loop();    

  if ((currentTime - batterySampleTime) >= 10 * 60 * 1000) {
    batterySampleTime = currentTime;
    Serial.printf("Batt Voltage: %g V\n", maxlipo.cellVoltage());
    Serial.printf("Batt Percent: %g %%\n\n", maxlipo.cellPercent());
  }

  // check to see if wifi is connected
  if (!udpStarted) {
    if (wifiConnected) {
      udpStarted = true;
      getNTPserver = true;
      Serial.printf("Starting UDP on port %d\n", localPort);
      udp.begin(localPort);
      //audio.connecttohost("https://nhpr.streamguys1.com/nhpr");
    }
  }

  else {

    if (getNTPserver) {
      getNTPserver = false;
      requestTime = true;
      //get a random server from the pool
      WiFi.hostByName(ntpServerName, timeServerIP);
      Serial.printf("NTP server IP address: %s\n", timeServerIP.toString().c_str());
    }

    else if (requestTime) {
      requestTime = false;
      parseTime = true;
      Serial.printf("sending NTP packet...\n");
      sendNTPpacket(timeServerIP); // send an NTP packet to a time server
    }
    
    else if (parseTime) {
      int len = udp.parsePacket();
      if (len != 0) {
        parseTime = false;
        lastNtpRequestTime = currentTime;
        Serial.printf("packet received, length=%d\n", len);
        // We've received a packet, read the data from it
        udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
        parseNTPpacket();
      }
    }

    else {
      if (currentTime - lastNtpRequestTime >= NTP_INTERVAL) {
        Serial.printf("Sending a new NTP request\n");
        getNTPserver = true;
      }
    }
  }

  // check for web server requests
  configServerLoop();

  updateDisplay(currentTime);
  handleEncoder(currentTime);
}

void displayTime()
{
  DateTime now = rtcLib.now();
  uint8_t hourNow = now.hour();
  uint8_t minuteNow = now.minute();
  uint8_t secondNow = now.second();
  bool isPM = showPM && (hourNow >= 12);

  // convert 24 hour time to 12 hour AM/PM time
  if (hourNow == 0) {
    hourNow = 12;
  } 
  else if (hourNow > 12) {
    hourNow -= 12;
  }

  if (hourNow < 10) {
    display.writeDigitAscii(0, ' ');
  }
  else {
    display.writeDigitNum(0, hourNow / 10);
  }
  display.writeDigitNum(1, hourNow % 10);
  display.drawColon((secondNow % 2) == 0);
  display.writeDigitNum(3, minuteNow / 10);
  display.writeDigitNum(4, minuteNow % 10, isPM);
  display.writeDisplay();
}

void displaySelection(unsigned long currentTime)
{
  int parameterValue = getParameterValue(selectedParameter);
  unsigned long currentHalfSecond = currentTime / 500;
  if (currentHalfSecond & 1) {
    display.writeDigitNum(0, selectedParameter->index);
  }
  else {
    display.writeDigitAscii(0, ' ');
  }
  display.writeDigitAscii(1, '-');
  display.drawColon(false);
  if (parameterValue < 10) {
    display.writeDigitAscii(3, ' ');
  }
  else {
    display.writeDigitNum(3, (parameterValue / 10) % 10);
  }
  display.writeDigitNum(4,  parameterValue % 10);
  display.writeDisplay();
}

void displaySelectionValue(unsigned long currentTime)
{
  unsigned long currentHalfSecond = currentTime / 500;
  display.writeDigitNum(0, selectedParameter->index);
  display.writeDigitAscii(1, '-');
  display.drawColon(false);
  if (currentHalfSecond & 1) {
    if (encoderCount < 10) {
      display.writeDigitAscii(3, ' ');
    }
    else {
      display.writeDigitNum(3, (encoderCount / 10) % 10);
    }
    display.writeDigitNum(4,  encoderCount % 10);
  }
  else {
      display.writeDigitAscii(3, ' ');
      display.writeDigitAscii(4, ' ');
  }
  display.writeDisplay();
}

void blankDisplay()
{
  display.writeDigitAscii(0, ' ');
  display.writeDigitAscii(1, ' ');
  display.drawColon(false);
  display.writeDigitAscii(3, ' ');
  display.writeDigitAscii(4, ' ');
  display.writeDisplay();
}

void displayTimeRemaining(unsigned long currentTime)
{
  switch (clockState) {
  case ClockState::START_PAUSE:
  case ClockState::END_PAUSE:
    blankDisplay();
    break;
  case ClockState::COUNTING:
  case ClockState::POMODORO_WORK:
  case ClockState::POMODORO_SHORT_REST:
  case ClockState::POMODORO_LONG_REST:
    displayCounter();
    break;
  }

  if (timeRemaining <= 0) {
    switch (clockState) {
    case ClockState::START_PAUSE:
      clockState = nextClockState;
      startTimeInterval(currentTime, nextTimeInterval);
      playSound("bell.mp3");
      break;
    case ClockState::COUNTING:
      clockState = ClockState::END_PAUSE;
      startTimeInterval(currentTime, END_PAUSE_IN_SECS);
      playSound("bell.mp3");
      break;
    case ClockState::POMODORO_WORK:
      if (shortIntervalsRemaining > 0) {
        clockState = ClockState::POMODORO_SHORT_REST;
        startTimeInterval(currentTime, pomodoroShortRestInterval * 60);
        --shortIntervalsRemaining;
        playSound("bell.mp3");
      }
      else {
        clockState = ClockState::POMODORO_LONG_REST;
        startTimeInterval(currentTime, pomodoroLongRestInterval * 60);
        playSound("bell2.mp3");
      }
      break;
    case ClockState::POMODORO_SHORT_REST:
      clockState = ClockState::POMODORO_WORK;
      startTimeInterval(currentTime, pomodoroWorkInterval * 60);
      playSound("bell.mp3");
      break;
    case ClockState::POMODORO_LONG_REST:
      clockState = ClockState::TIME;
      playSound("bell.mp3");
      break;
    case ClockState::END_PAUSE:
      clockState = ClockState::TIME;
      break;
    }
  }
}

void displayCounter()
{
  int seconds = timeRemaining / 1000;
  int minutes = seconds / 60;
  seconds %= 60;
  if (minutes < 10) {
    display.writeDigitAscii(0, ' ');
  }
  else {
    display.writeDigitNum(0,  (minutes / 10) % 10);
  }
  if (minutes == 0) {
    display.writeDigitAscii(1, ' ');
    display.drawColon(false);
  }
  else {
    display.writeDigitNum(1, minutes % 10);
    display.drawColon(true);
  }
  if (minutes == 0 && seconds < 10) {
    display.writeDigitAscii(3, ' ');
  }
  else {
    display.writeDigitNum(3,  (seconds / 10) % 10);
  }
  display.writeDigitNum(4,  seconds % 10);
  display.writeDisplay();
}

void resetEncoder(int count)
{
  encoderCount = count;
  ss.setEncoderPosition(encoderCount);
}

void updateDisplay(unsigned long currentTime)
{
  switch (clockState) {
    case ClockState::TIME:
      displayTime();
      break;
    case ClockState::SELECTING:
      displaySelection(currentTime);
      break;
    case ClockState::ADJUSTING:
      displaySelectionValue(currentTime);
      break;
    case ClockState::START_PAUSE:
    case ClockState::COUNTING:
    case ClockState::POMODORO_WORK:
    case ClockState::POMODORO_SHORT_REST:
    case ClockState::POMODORO_LONG_REST:
    case ClockState::END_PAUSE:
      displayTimeRemaining(currentTime);
      break;
    default:
      clockState = ClockState::TIME;
      break;
  }

  // go back to displaying the time if the encoder hasn't been turned in 10 seconds
  if ((clockState == ClockState::SELECTING || clockState == ClockState::ADJUSTING) && (currentTime - encoderLastMoved) >= 10000) {
    clockState = ClockState::TIME;
  }

  switch (clockState) {
  case ClockState::START_PAUSE:
  case ClockState::COUNTING:
  case ClockState::POMODORO_WORK:
  case ClockState::POMODORO_SHORT_REST:
  case ClockState::POMODORO_LONG_REST:
  case ClockState::END_PAUSE:
    if ((currentTime - secondStartTime) >= 1000) {
      secondStartTime += 1000;
      timeRemaining -= 1000;
    }
    break;
  }
}

void handleEncoder(unsigned long currentTime)
{
  encoderButton.update(ss.digitalRead(SS_SWITCH));
  buttonA.update(digitalRead(BUTTON_A));
  buttonB.update(digitalRead(BUTTON_B));
  //buttonC.update(digitalRead(BUTTON_C));
  //buttonD.update(digitalRead(BUTTON_D));

  if (encoderBtnChanged) {
    encoderBtnChanged = false;
    if (encoderBtnState == BTN_PRESSED) {
      Serial.printf("Pushed button\n");
      switch (clockState) {
      case ClockState::TIME:
        clockState = ClockState::SELECTING;
        selectParameter(MEDITATION_INDEX);
        resetEncoder(MEDITATION_INDEX);
        break;
      case ClockState::SELECTING:
        clockState = ClockState::ADJUSTING;
        resetEncoder(getParameterValue(selectedParameter));
        immediate = false;
        break;
      case ClockState::ADJUSTING:
        setParameterValue(selectedParameter, encoderCount);
        if (immediate) {
          startMeditationTimer();
        }
        else {
          clockState = ClockState::SELECTING;
          encoderCount = selectedParameter->index;
        }
        break;
      case ClockState::COUNTING:
      case ClockState::POMODORO_WORK:
      case ClockState::POMODORO_SHORT_REST:
      case ClockState::POMODORO_LONG_REST:
        clockState = ClockState::TIME;
        break;
      }
    } 
    else {
      // encoderBtnState == BTN_OPEN.
      Serial.printf("Released button\n");
    }
  }

  int32_t newCount = ss.getEncoderPosition();
  if (newCount != encoderCount) {
    if (clockState == ClockState::TIME) {
      clockState = ClockState::ADJUSTING;
      selectParameter(MEDITATION_INDEX);
      encoderCount = MEDITATION_INDEX;
      immediate = true;
    }
    switch (clockState) {
    case ClockState::SELECTING:
      if (newCount >= 1 && newCount <= parameterCount) {
        encoderCount = newCount;
        selectParameter(encoderCount);
      }
      break;
    case ClockState::ADJUSTING:
      if (newCount >= selectedParameter->lowLimit && newCount <= selectedParameter->highLimit) {
        encoderCount = newCount;
      }
      break;
    }
    ss.setEncoderPosition(encoderCount);
    encoderLastMoved = currentTime;
  }
}

void startMeditationTimer()
{
  clockState = ClockState::START_PAUSE;
  nextClockState = ClockState::COUNTING;
  nextTimeInterval = meditationInterval * 60;
  startTimeInterval(millis(), START_PAUSE_IN_SECS);
}

void startPomodoroTimer()
{
  clockState = ClockState::POMODORO_WORK;
  startTimeInterval(millis(), pomodoroWorkInterval * 60);
  shortIntervalsRemaining = pomodoroWorkPeriodCount - 1;
}

void startTimeInterval(unsigned long currentTime, int intervalInSeconds)
{
  secondStartTime = currentTime;
  timeRemaining = intervalInSeconds * 1000; // in milliseconds
}

void playSound(const char *name)
{
  if (flashFilesystemMounted) {
    audio.connecttoFS(LittleFS, name);
  }
  else if (sdFilesystemMounted) {
    audio.connecttoFS(SD, name);
  }
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, sizeof(packetBuffer));

  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, sizeof(packetBuffer));
  udp.endPacket();
}

void parseNTPpacket()
{
  //the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, esxtract the two words:

  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long utcTime = highWord << 16 | lowWord;
  Serial.printf("Seconds since Jan 1 1900 = %lu\n", utcTime);

  // now convert NTP time into everyday time:
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = utcTime - seventyYears;
  // print Unix time:
  Serial.printf("Unix time = %lu\n", epoch);

  time_t localTime = timezone->toLocal((time_t)utcTime);

  rtc.setEpoch(localTime, true);

  int hourNow = hour(localTime);
  int minuteNow = minute(localTime);
  int secondNow = second(localTime);

  char timeBuf[9]; // hh:mm:ss
  sprintf(&timeBuf[0], "%02d", hourNow); // print the hour (86400 equals secs per day)
  timeBuf[2] = ':';
  sprintf(&timeBuf[3], "%02d", minuteNow); // print the minute (3600 equals secs per minute)
  timeBuf[5] = ':';
  sprintf(&timeBuf[6], "%02d", secondNow); // print the second
  
  // print the hour, minute and second:
  Serial.printf("The time is %s\n", timeBuf);
}

