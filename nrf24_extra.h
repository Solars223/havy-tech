#ifndef NRF24_EXTRA_H
#define NRF24_EXTRA_H

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "pinout.h"


extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern void drawStars();


RF24 radio(NRF24_CE, NRF24_CSN);


const byte wifi_channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
const byte ble_channels[] = {2, 26, 80};
const byte bt_channels[] = {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80};
const byte usb_channels[] = {40, 50, 60};  
const byte video_channels[] = {70, 75, 80}; 
const byte rc_channels[] = {1, 3, 5, 7};    
const byte all_channels_start = 0;
const byte all_channels_end = 125;


#define SPECTRUM_CHANNELS 128
uint8_t spectrumValues[SPECTRUM_CHANNELS];

void runNRF24Spectrum() {
  if (!radio.begin()) {
    u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10, 30, "NRF24 Fail"); u8g2.sendBuffer(); delay(1000); return;
  }
  
  radio.setAutoAck(false);
  radio.startListening();
  radio.stopListening();
  radio.setDataRate(RF24_2MBPS);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.setPALevel(RF24_PA_MAX);
  
  memset(spectrumValues, 0, sizeof(spectrumValues));

  while (digitalRead(BTN_BACK) == HIGH) {
    
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
      radio.setChannel(i);
      radio.startListening();
      delayMicroseconds(40); 
      bool carrier = radio.testCarrier();
      radio.stopListening();
      
     
      if (carrier) {
        if (spectrumValues[i] < 60) spectrumValues[i] += 12; 
      } else {
        if (spectrumValues[i] > 0) spectrumValues[i] -= 2;   
      }
    }

   
    u8g2.clearBuffer();
    
    
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(0, 6, "2.40");
    u8g2.drawStr(60, 6, "2.45");
    u8g2.drawStr(110, 6, "2.52");
    
    
    for (int i = 0; i < 128; i++) {
      if (spectrumValues[i] > 0) {
        
        int h = spectrumValues[i];
        if (h > 55) h = 55;
        u8g2.drawLine(i, 63, i, 63 - h);
      }
    }
    
    
    u8g2.drawFrame(0, 7, 128, 57);

    u8g2.sendBuffer();
  }
  
  
  radio.powerDown();
}


void executeNRFJamming(const byte* channels, int count, const char* name) {
  radio.stopListening();
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_2MBPS);
  radio.setRetries(0, 0);
  radio.setPayloadSize(32);
  
  u8g2.clearBuffer(); drawStars();
  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.drawStr(10, 25, "JAMMING!");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(10, 45, name);
  u8g2.drawStr(10, 60, "BACK to STOP");
  u8g2.sendBuffer();

  int chIdx = 0;
  

  while(digitalRead(BTN_BACK) == HIGH) {

    radio.setChannel(channels[chIdx]);
    

    radio.startConstCarrier(RF24_PA_MAX, 0);
    delayMicroseconds(200); 
    radio.stopConstCarrier();
    

    chIdx++;
    if (chIdx >= count) chIdx = 0;
  }
  
  radio.powerDown();
}


void executeNRFJammingAll() {
  radio.stopListening();
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_2MBPS);
  
  u8g2.clearBuffer(); drawStars();
  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.drawStr(10, 25, "JAMMING ALL");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(10, 60, "BACK to STOP");
  u8g2.sendBuffer();

  int ch = 0;
  while(digitalRead(BTN_BACK) == HIGH) {
    radio.setChannel(ch);
    radio.startConstCarrier(RF24_PA_MAX, 0);
    delayMicroseconds(100); 
    radio.stopConstCarrier();
    
    ch++;
    if (ch > 125) ch = 0;
  }
  radio.powerDown();
}


void runNRF24JammerMenu() {
  if (!radio.begin()) {
    u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10, 30, "NRF24 Fail"); u8g2.sendBuffer(); delay(1000); return;
  }

  const char* items[] = {"WiFi", "BLE", "Bluetooth", "USB Mouse", "Video/Cams", "RC Toys", "ALL 2.4GHz"};
  const int count = 7;
  int selected = 0;
  

  while(true) {
    u8g2.clearBuffer(); drawStars();
    u8g2.drawFrame(0, 0, 128, 64);
    
    u8g2.drawStr(30, 12, "Target:");
    

    int yStart = 25;
    u8g2.drawStr(10, yStart, ">");
    u8g2.drawStr(20, yStart, items[selected]);
    
    u8g2.drawStr(20, 58, "OK to START");
    u8g2.sendBuffer();

    if(digitalRead(BTN_DOWN) == LOW) { selected = (selected + 1) % count; delay(150); }
    if(digitalRead(BTN_UP) == LOW) { selected = (selected - 1 + count) % count; delay(150); }
    if(digitalRead(BTN_BACK) == LOW) return;
    
    if(digitalRead(BTN_OK) == LOW) {
        delay(200);
        if(selected == 0) executeNRFJamming(wifi_channels, sizeof(wifi_channels), "Target: WiFi");
        if(selected == 1) executeNRFJamming(ble_channels, sizeof(ble_channels), "Target: BLE");
        if(selected == 2) executeNRFJamming(bt_channels, sizeof(bt_channels), "Target: BT");
        if(selected == 3) executeNRFJamming(usb_channels, sizeof(usb_channels), "Target: USB");
        if(selected == 4) executeNRFJamming(video_channels, sizeof(video_channels), "Target: Video");
        if(selected == 5) executeNRFJamming(rc_channels, sizeof(rc_channels), "Target: RC");
        if(selected == 6) executeNRFJammingAll();
        delay(200);
    }
    delay(30);
  }
}

#endif