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
#include "FS.h"

#include <Preferences.h>
#define RW_MODE false
#define RO_MODE true

#include <WiFi.h>
#include <WiFiClient.h>

#include "wifi_credentials.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <Timezone.h>

// microSD Card Reader connections
#define SD_CS          5
#define SPI_MOSI      35 
#define SPI_MISO      37
#define SPI_SCK       36
 
// I2S Connections
#define I2S_DOUT      12
#define I2S_BCLK      11
#define I2S_LRC       13
 
Preferences prefs;

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

// US Pacific Time Zone
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};    // Daylight time = UTC - 7 hours
TimeChangeRule usPST = {"PST", First,  Sun, Nov, 2, -480};    // Standard time = UTC - 8 hours
Timezone usPacific(usPDT, usPST);

// change this to match your current timezone
Timezone *myTimezone = &usEastern;

RTClib rtcLib;
DS3231 rtc;

Adafruit_MAX17048 maxlipo;

// Create Audio object
Audio audio;
 
#define SS_SWITCH        24
#define SS_NEOPIX        6

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);

enum class EncoderState {
  TIME,
  ADJUSTING,
  COUNTING
};

EncoderState encoderState = EncoderState::TIME;
int32_t encoderCount;
unsigned long encoderLastMoved;

bool encoderButtonState = false;

unsigned long timeRemaining;
unsigned long secondStartTime;

unsigned int localPort = 2390;      // local port to listen for UDP packets

#define SDA_PIN 3
#define SCL_PIN 4

#define I2C_POWER   0x36
#define I2C_ENCODER 0x37
#define I2C_RTC     0x68
#define I2C_DISPLAY 0x70

static void buttonHandler(uint8_t btnId, uint8_t btnState) 
{
  if (btnState == BTN_PRESSED) {
    Serial.println("Pushed button");
    switch (encoderState) {
    case EncoderState::ADJUSTING:
      encoderState = EncoderState::COUNTING;
      secondStartTime = millis();
      timeRemaining = encoderCount * 60000; // in milliseconds
      break;
    case EncoderState::COUNTING:
      encoderState = EncoderState::TIME;
      encoderCount = 0;
      ss.setEncoderPosition(encoderCount);
      break;
    }
  } 
  else {
    // btnState == BTN_OPEN.
    Serial.println("Released button");
  }
}

Button myButton(0, buttonHandler);

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

bool showPM = false;

Adafruit_7segment display = Adafruit_7segment();

//BLE server name
#define bleServerName "JClock"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define TAG_SERVICE_UUID "9517ee93-7909-49e9-aa40-1bd9b825e0d3"
#define TAG_IMAGE_CHARACTERISTIC_UUID "00767412-ac3b-4e86-8941-1fb7f70c87f1"
#define TAG_COLOR_CHARACTERISTIC_UUID "d0dca24b-399b-4b07-baa3-0db90835d742"

bool bleConnected = false;
BLEServer *pServer;
BLECharacteristic *pImageCharacteristic;
BLECharacteristic *pColorCharacteristic;

//Setup callbacks onConnect and onDisconnect
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    Serial.println("Connected");
    bleConnected = true;
  };
  void onDisconnect(BLEServer* pServer) {
    Serial.println("Disconnected");
    bleConnected = false;
    startAdvertising();
  }
};

class MyImageCallbackHandler : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    uint8_t *data = pCharacteristic->getData();
    size_t length = pCharacteristic->getLength();

    Serial.printf("Image:");
    for (size_t i = 0; i < length; ++i) {
      Serial.printf(" %02x", data[i]);
    }
    Serial.println();
  }
};

class MyColorCallbackHandler : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    uint8_t *data = pCharacteristic->getData();
    size_t length = pCharacteristic->getLength();

    Serial.printf("Color:");
    for (size_t i = 0; i < length; ++i) {
      Serial.printf(" %02x", data[i]);
    }
    Serial.println();
  }
};

unsigned long batterySampleTime;

void setup()
{
  Serial.begin(115200);
  delay(1000);
                
  Serial.printf("jclock\n");

  if (maxlipo.begin()) {
    Serial.print(F("Found MAX17048"));
    Serial.print(F(" with Chip ID: 0x")); 
    Serial.println(maxlipo.getChipID(), HEX);
  }
  else {
    Serial.println("Can't find MAX17048");
  }

  prefs.begin("Settings", RW_MODE);

  prefs.clear();

  bool initialized = prefs.isKey("Initialized");
  if (!initialized) {
    Serial.println("Initializing settings");
    prefs.putBool("Initialized", true);
    prefs.putBool("ShowPM", false);
  }

  if (prefs.isKey("ShowPM")) {
    showPM = prefs.getBool("ShowPM");
  }
  
  // Start microSD Card
  if (!SD.begin(SD_CS)) {
    Serial.println("Error accessing microSD card!");
    while(true); 
  }
  
  // Setup I2S 
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  
  // Set Volume
  audio.setVolume(10);
  
  Wire.begin(SDA_PIN, SCL_PIN);

  display.begin(I2C_DISPLAY);

  if (!ss.begin(I2C_ENCODER) || !sspixel.begin(I2C_ENCODER)) {
    Serial.println("Couldn't find seesaw on default address");
    while(1) delay(10);
  }
  Serial.println("seesaw started");

  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991){
    Serial.print("Wrong firmware loaded? ");
    Serial.println(version);
    while(1) delay(10);
  }
  Serial.println("Found Product 4991");

  // set not so bright!
  sspixel.setBrightness(20);
  sspixel.show();
  
  // use a pin for the built in encoder switch
  ss.pinMode(SS_SWITCH, INPUT_PULLUP);

  // set starting position
  resetEncoder();

  // go back to the clock display if the encoder hasn't been moved in 10 seconds
  encoderLastMoved = millis() - 10000;

  Serial.println("Turning on interrupts");
  delay(10);
  ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
  ss.enableEncoderInterrupt();

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  // Create the BLE Device
  BLEDevice::init(bleServerName);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *bmeService = pServer->createService(TAG_SERVICE_UUID);

  pImageCharacteristic = bmeService->createCharacteristic(TAG_IMAGE_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pImageCharacteristic->setCallbacks(new MyImageCallbackHandler);

  pColorCharacteristic = bmeService->createCharacteristic(TAG_COLOR_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pColorCharacteristic->setCallbacks(new MyColorCallbackHandler);

/*
  // Create BLE Characteristics and Create a BLE Descriptor

  // Temperature
  bmeService->addCharacteristic(&bmeTemperatureCelsiusCharacteristics);
  bmeTemperatureCelsiusDescriptor.setValue("BME temperature Celsius");
  bmeTemperatureCelsiusCharacteristics.addDescriptor(&bmeTemperatureCelsiusDescriptor);

  // Humidity
  bmeService->addCharacteristic(&bmeHumidityCharacteristics);
  bmeHumidityDescriptor.setValue("BME humidity");
  bmeHumidityCharacteristics.addDescriptor(new BLE2902());
*/

  // Start the service
  bmeService->start();

  // Start advertising
  startAdvertising();

  batterySampleTime = millis();
}

bool wifiConnected = false;
bool getNTPserver = false;
bool requestTime = false;
bool parseTime = false;

void loop()
{
  unsigned long currentTime = millis();

  audio.loop();    

  if ((currentTime - batterySampleTime) >= 1000) {
    batterySampleTime = currentTime;
    Serial.print(F("Batt Voltage: ")); Serial.print(maxlipo.cellVoltage(), 3); Serial.println(" V");
    Serial.print(F("Batt Percent: ")); Serial.print(maxlipo.cellPercent(), 1); Serial.println(" %");
    Serial.println();
  }

  // check to see if wifi is connected
  if (!wifiConnected) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      getNTPserver = true;
    
      Serial.println("WiFi connected");
      
      localIP = WiFi.localIP();
      Serial.printf("IP address: %s\n", localIP.toString().c_str());
    
      Serial.printf("Starting UDP on port %d\n", localPort);
      udp.begin(localPort);
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
      Serial.println("sending NTP packet...");
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
        Serial.println("Sending a new NTP request");
        getNTPserver = true;
      }
    }
  }

  switch (encoderState) {
    case EncoderState::TIME:
      displayTime();
      break;
    case EncoderState::ADJUSTING:
      displayCounter();
      break;
    case EncoderState::COUNTING:
      displayTimeRemaining();
      if (timeRemaining <= 0) {
        audio.connecttoFS(SD,"/0001.mp3");
        encoderState = EncoderState::TIME;
        resetEncoder();
      }
      break;
    default:
      encoderState = EncoderState::TIME;
      resetEncoder();
      break;
  }

  // go back to displaying the time if the encoder hasn't been turned in 10 seconds
  if (encoderState == EncoderState::ADJUSTING && (currentTime - encoderLastMoved) >= 10000) {
    encoderState = EncoderState::TIME;
    resetEncoder();
  }

  if (encoderState == EncoderState::COUNTING) {
    if ((currentTime - secondStartTime) >= 1000) {
      secondStartTime += 1000;
      timeRemaining -= 1000;
    }
  }

  myButton.update(!ss.digitalRead(SS_SWITCH));

  int32_t newCount = ss.getEncoderPosition();
  if (newCount != encoderCount && newCount >= 0 && newCount <= 999) {
    encoderCount = newCount;
    encoderState = EncoderState::ADJUSTING;
    encoderLastMoved = currentTime;
  }
}

void startAdvertising()
{
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(TAG_SERVICE_UUID);
  pServer->getAdvertising()->start();
  Serial.println("Awaiting a client connection...");
}

void resetEncoder()
{
  encoderCount = 0;
  ss.setEncoderPosition(encoderCount);
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

void displayCounter()
{
  if (encoderCount < 1000) {
    display.writeDigitAscii(0, ' ');
  }
  else {
    display.writeDigitNum(0,  (encoderCount / 1000) % 10);
  }
  if (encoderCount < 100) {
    display.writeDigitAscii(1, ' ');
  }
  else {
    display.writeDigitNum(1, (encoderCount / 100) % 10);
  }
  display.drawColon(false);
  if (encoderCount < 10) {
    display.writeDigitAscii(3, ' ');
  }
  else {
    display.writeDigitNum(3, (encoderCount / 10) % 10);
  }
  display.writeDigitNum(4,  encoderCount % 10);
  display.writeDisplay();
}

void displayTimeRemaining()
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
  Serial.print("Seconds since Jan 1 1900 = " );
  Serial.println(utcTime);

  // now convert NTP time into everyday time:
  Serial.print("Unix time = ");
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = utcTime - seventyYears;
  // print Unix time:
  Serial.println(epoch);

  time_t localTime = myTimezone->toLocal((time_t)utcTime);

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
  Serial.print("The time is ");
  Serial.println(timeBuf);
}

uint32_t Wheel(uint8_t WheelPos) 
{
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return sspixel.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return sspixel.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return sspixel.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

