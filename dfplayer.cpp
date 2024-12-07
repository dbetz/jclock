#include <Arduino.h>
#include "dfplayer.h"

#define RXD1 18
#define TXD1 17

#define Start_Byte 0x7E
#define Version_Byte 0xFF
#define Command_Length 0x06
#define End_Byte 0xEF
#define NoAcknowledge 0x00 // do not need to return the response
#define Acknowledge 0x01 // need answering

#define NextTrack 0x01
#define PrevTrack 0x02
#define SpecifyTrack 0x03
#define IncreaseVolume 0x04
#define DecreaseVolume 0x05
#define SpecifyVolume 0x06
#define SpecifySource 0x09
#define SpecifyFolder 0x0f
#define RepeatPlay 0x11
#define SendInitializationParameters 0x3f
#define QueryTotalNumberOfCardFiles 0x47

#define SourceU 0
#define SourceTF 1
#define SourceAux 2
#define SourceSleep 3
#define SourceFlash 4

static void execute_CMD(byte CMD, byte Par1, byte Par2);

#define MY_CODE

#ifndef MY_CODE
#include <DFPlayerMini_Fast.h>
DFPlayerMini_Fast myMP3;
#endif

void mp3_initialize()
{
  Serial1.begin(9600, SERIAL_8N1, RXD1, TXD1);
#ifdef MY_CODE
  delay(10000);
  Serial.println("Initializing");
  execute_CMD(SendInitializationParameters, 0, 2);
  delay(3000);
#else
  myMP3.begin(Serial1, true);
#endif
}

void mp3_playFirst()
{
#ifdef MY_CODE
  mp3_setVolume(30);
  delay(500);
  Serial.println("Playing first track");
  execute_CMD(SpecifySource, 0, SourceTF);
  delay(500);
  execute_CMD(RepeatPlay, 0, 1); 
  delay(500);
#else
  delay(1000);
  
  Serial.println("Setting volume to max");
  myMP3.volume(30);
  
  Serial.println("Looping track 1");
  myMP3.loop(1);
#endif
}

void mp3_setVolume(int volume)
{
  execute_CMD(SpecifyVolume, 0, volume); // Set the volume (0x00~0x30)
  delay(2000);
}

static void execute_CMD(byte CMD, byte Par1, byte Par2)
{
  // Calculate the checksum (2 bytes)
  //word checksum = -(Version_Byte + Command_Length + CMD + Acknowledge + Par1 + Par2);
  word checksum = 0xFFFF - (Version_Byte + Command_Length + CMD + NoAcknowledge + Par1 + Par2) + 1;

  // Build the command line
  byte Command_line[10] = {
    Start_Byte, 
    Version_Byte, 
    Command_Length, 
    CMD, 
    NoAcknowledge,
    Par1, 
    Par2, 
    highByte(checksum), 
    lowByte(checksum), 
    End_Byte
  };

  //Send the command line to the module
  Serial1.write(Command_line, sizeof(Command_line));
}

void mp3_idle()
{
  int available = Serial1.available();
  if (available > 0) {
    Serial.print("MP3:");
    while (available > 0) {
      int byte = Serial1.read();
      Serial.printf(" %02x", byte);
      --available;
    }
    Serial.println();
  }
}
