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

#include <Timezone.h>

#include "wifi_credentials.h"

// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};    // Daylight time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};     // Standard time = UTC - 5 hours
Timezone usEastern(usEDT, usEST);

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
//#define NTP_INTERVAL  (10 * 60 * 1000)
#define NTP_INTERVAL  (10 * 1000)

unsigned long lastNtpRequestTime;

Adafruit_7segment display = Adafruit_7segment();

void setup()
{
  Serial.begin(115200);
  delay(500);
  
  Serial.printf("jclock\n");

  Wire.begin(SDA_PIN, SCL_PIN);

  display.begin(0x70);

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
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

  time_t localTime = usEastern.toLocal((time_t)utcTime);

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
