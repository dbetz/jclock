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
#include <DS3231.h>

#include <WiFi.h>
#include <WiFiClient.h>

#include "wifi_credentials.h"
#include "dfplayer.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <Timezone.h>

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

unsigned int localPort = 2390;      // local port to listen for UDP packets

#define SDA_PIN 20
#define SCL_PIN 21

/* Don't hardwire the IP address or we won't get the benefits of the pool.
 *  Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress localIP;
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

// query the NTP server every 10 minutes
#define NTP_INTERVAL  (10 * 60 * 1000)
//#define NTP_INTERVAL  (10 * 1000)

unsigned long lastNtpRequestTime;

Adafruit_7segment display = Adafruit_7segment();

//BLE server name
#define bleServerName "JClock"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define TAG_SERVICE_UUID "9517ee93-7909-49e9-aa40-1bd9b825e0d3"
#define TAG_IMAGE_CHARACTERISTIC_UUID "00767412-ac3b-4e86-8941-1fb7f70c87f1"
#define TAG_COLOR_CHARACTERISTIC_UUID "d0dca24b-399b-4b07-baa3-0db90835d742"

bool bleConnected = false;
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

void setup()
{
  Serial.begin(115200);
  delay(500);
                
  mp3_initialize();

  Serial.printf("jclock\n");

  Wire.begin(SDA_PIN, SCL_PIN);

  display.begin(0x70);

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  // Create the BLE Device
  BLEDevice::init(bleServerName);

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
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
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(TAG_SERVICE_UUID);
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");

  mp3_playFirst();
}

bool wifiConnected = false;
bool getNTPserver = false;
bool requestTime = false;
bool parseTime = false;

void loop()
{
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
        lastNtpRequestTime = millis();
        Serial.printf("packet received, length=%d\n", len);
        // We've received a packet, read the data from it
        udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
        parseNTPpacket();
      }
    }

    else {
      if (millis() - lastNtpRequestTime >= NTP_INTERVAL) {
        Serial.println("Sending a new NTP request");
        getNTPserver = true;
      }
    }
  }

  DateTime now = rtcLib.now();
  uint8_t hourNow = now.hour();
  uint8_t minuteNow = now.minute();
  uint8_t secondNow = now.second();
  bool isPM = (hourNow >= 12);

  // convert 24 hour time to 12 hour AM/PM time
  if (hourNow == 0) {
    hourNow = 12;
  } 
  else if (hourNow > 12) {
    hourNow -= 12;
  }

  display.writeDigitNum(0, hourNow / 10);
  display.writeDigitNum(1, hourNow % 10);
  display.drawColon((secondNow % 2) == 0);
  display.writeDigitNum(3, minuteNow / 10);
  display.writeDigitNum(4, minuteNow % 10, isPM);
  display.writeDisplay();

  mp3_idle();
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
