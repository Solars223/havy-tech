#ifndef SUBGHZ_EXTRA_H
#define SUBGHZ_EXTRA_H

#include <Arduino.h>
#include <SD.h>
#include <RCSwitch.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "pinout.h"

extern RCSwitch mySwitch;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern void drawStars();
extern bool cc1101Ready;
extern bool sdReady;



struct BruteProtocol {
  const int* zero;
  uint8_t zeroLen;
  const int* one;
  uint8_t oneLen;
  const int* pilot;
  uint8_t pilotLen;
  const int* stop;
  uint8_t stopLen;
};


const int cameZero[] = {-320, 640};
const int cameOne[] = {-640, 320};
const int camePilot[] = {-11520, 320}; 
const BruteProtocol protoCame = {cameZero, 2, cameOne, 2, camePilot, 2, nullptr, 0};


const int niceZero[] = {700, -1400};   
const int niceOne[] = {1400, -700};    
const int nicePilot[] = {700, -25200}; 
const BruteProtocol protoNice = {niceZero, 2, niceOne, 2, nicePilot, 2, nullptr, 0};


const int chamberZero[] = {-870, 430};
const int chamberOne[] = {-430, 870};
const int chamberStop[] = {-3000, 1000}; 
const BruteProtocol protoChamber = {chamberZero, 2, chamberOne, 2, nullptr, 0, chamberStop, 2};


const int holtekZero[] = {-870, 430};
const int holtekOne[] = {-430, 870};
const int holtekPilot[] = {-15480, 430};
const BruteProtocol protoHoltek = {holtekZero, 2, holtekOne, 2, holtekPilot, 2, nullptr, 0};




void bruteSendSequence(const int* seq, size_t len) {
  if (!seq || len == 0) return;
  for (size_t i = 0; i < len; i++) {
    int duration = seq[i];

    bool levelHigh = duration > 0;
    unsigned int delayVal = (duration > 0) ? duration : -duration;
    
    digitalWrite(CC1101_GDO0, levelHigh ? HIGH : LOW);
    delayMicroseconds(delayVal);
  }
}

void bruteSendCode(const BruteProtocol* p, uint16_t code, int bits) {
    
    if (p->pilot) bruteSendSequence(p->pilot, p->pilotLen);

    
    for (int bit = bits - 1; bit >= 0; --bit) {
        bool set = (code >> bit) & 0x1;
        bruteSendSequence(set ? p->one : p->zero, set ? p->oneLen : p->zeroLen);
    }

    
    if (p->stop) bruteSendSequence(p->stop, p->stopLen);
    
    
    digitalWrite(CC1101_GDO0, LOW);
}


void drawBruteProgress(const char* name, int current, int total) {
    static unsigned long lastDraw = 0;
    if (millis() - lastDraw < 100) return; 
    lastDraw = millis();

    u8g2.clearBuffer();
    drawStars();
    u8g2.drawFrame(0, 0, 128, 64);
    
    u8g2.drawStr(6, 12, name);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", current, total);
    u8g2.drawStr(6, 30, buf);

    int w = map(current, 0, total, 0, 116);
    u8g2.drawFrame(6, 40, 116, 8);
    u8g2.drawBox(6, 40, w, 8);

    u8g2.drawStr(6, 62, "BACK to stop");
    u8g2.sendBuffer();
}



void runSubGHzAnalyzer() {
    const float freqs[] = {315.00, 433.92, 868.00, 915.00};
    int freqIdx = 1; 
    bool needsUpdate = true;

    ELECHOUSE_cc1101.setModulation(2); 
    ELECHOUSE_cc1101.SetRx();

    while (digitalRead(BTN_BACK) == HIGH) {

        if (digitalRead(BTN_DOWN) == LOW) {
            freqIdx++;
            if(freqIdx > 3) freqIdx = 0;
            needsUpdate = true;
            delay(200);
        }
        if (digitalRead(BTN_UP) == LOW) {
            freqIdx--;
            if(freqIdx < 0) freqIdx = 3;
            needsUpdate = true;
            delay(200);
        }

        if (needsUpdate) {
            ELECHOUSE_cc1101.setMHZ(freqs[freqIdx]);
            ELECHOUSE_cc1101.SetRx();
            needsUpdate = false;
            delay(10);
        }

        int rssi = ELECHOUSE_cc1101.getRssi();
        bool found = (rssi >= -75);

        u8g2.clearBuffer();
        drawStars();
        

        u8g2.setFont(u8g2_font_6x12_tf);
        char freqStr[20];
        snprintf(freqStr, sizeof(freqStr), "%.2fMHz", freqs[freqIdx]);
        int wFreq = u8g2.getStrWidth(freqStr);
        u8g2.drawStr(128 - wFreq, 10, freqStr);


        if (found) {
            u8g2.setFont(u8g2_font_9x15_tf);
            const char* msg = "FOUND";
            int wMsg = u8g2.getStrWidth(msg);
            u8g2.drawStr((128 - wMsg) / 2, 35, msg);
        } else {
             u8g2.drawFrame(50, 25, 28, 12); 
             if((millis()/500)%2==0) u8g2.drawBox(52, 27, 24, 8);
        }


        u8g2.setFont(u8g2_font_6x12_tf);
        char rssiStr[32];
        snprintf(rssiStr, sizeof(rssiStr), "RSSI %d", rssi);
        int wRssi = u8g2.getStrWidth(rssiStr);
        u8g2.drawStr((128 - wRssi) / 2, 52, rssiStr);


        int barWidth = map(rssi, -100, -75, 0, 128);
        if(barWidth < 0) barWidth = 0;
        if(barWidth > 128) barWidth = 128;

        u8g2.drawFrame(0, 58, 128, 6);
        u8g2.drawBox(0, 58, barWidth, 6);

        u8g2.sendBuffer();
        delay(50);
    }
}



void runBruteCAME() {
    ELECHOUSE_cc1101.setMHZ(433.92);
    ELECHOUSE_cc1101.setModulation(2); 
    ELECHOUSE_cc1101.setSyncMode(0);   
    ELECHOUSE_cc1101.SetTx();
    
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);

    for (int i = 0; i < 4096; i++) { 
        if (digitalRead(BTN_BACK) == LOW) break;
        drawBruteProgress("CAME 12-bit", i, 4096);
        
        for(int r=0; r<4; r++) { 
            bruteSendCode(&protoCame, i, 12);
            delay(8);
        }
    }
    digitalWrite(CC1101_GDO0, LOW);
    ELECHOUSE_cc1101.SetRx();
}

void runBruteNice() {
    ELECHOUSE_cc1101.setMHZ(433.92);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.SetTx();
    
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);

    for (int i = 0; i < 4096; i++) {
        if (digitalRead(BTN_BACK) == LOW) break;
        drawBruteProgress("Nice 12-bit", i, 4096);
        
        for(int r=0; r<4; r++) { 
            bruteSendCode(&protoNice, i, 12);
            delay(10);
        }
    }
    digitalWrite(CC1101_GDO0, LOW);
    ELECHOUSE_cc1101.SetRx();
}

void runBruteChamberlain() {
    ELECHOUSE_cc1101.setMHZ(390.00); 
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.SetTx();
    
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);

    for (int i = 0; i < 512; i++) {
        if (digitalRead(BTN_BACK) == LOW) break;
        drawBruteProgress("Chamberlain 9b", i, 512);
        
        for(int r=0; r<4; r++) {
            bruteSendCode(&protoChamber, i, 9);
            delay(10);
        }
    }
    digitalWrite(CC1101_GDO0, LOW);
    ELECHOUSE_cc1101.SetRx();
}

void runBruteHoltek() {
    ELECHOUSE_cc1101.setMHZ(433.92);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.SetTx();
    
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);
    
    for (unsigned long i = 0; i < 1000; i++) {
        if (digitalRead(BTN_BACK) == LOW) break;
        drawBruteProgress("Holtek (Demo)", i, 1000);
        
        unsigned long code = 0xB00000 | i; 
        for(int r=0; r<3; r++) {
             bruteSendCode(&protoHoltek, code, 24);
             delay(10);
        }
    }
    digitalWrite(CC1101_GDO0, LOW);
    ELECHOUSE_cc1101.SetRx();
}

void runSubGHzBruteMenu() {
      if (!sdReady) {
        u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"SD Error"); u8g2.sendBuffer(); delay(1000); return;
    }


    while(digitalRead(BTN_OK) == LOW) { delay(10); } 
    delay(200); 


    String files[30];

    const char* items[] = {"CAME 12bit", "Nice 12bit", "Chamberlain", "Holtek (Demo)"};
    int selected = 0;
    
    while(true) {
         u8g2.clearBuffer(); drawStars();
         u8g2.drawFrame(0,0,128,64);
         u8g2.drawStr(40, 10, "BRUTE");
         
         for(int i=0; i<4; i++) {
             char buf[32];
             snprintf(buf, sizeof(buf), "%s %s", (i==selected?">":" "), items[i]);
             u8g2.drawStr(10, 25 + i*10, buf);
         }
         u8g2.sendBuffer();

         if(digitalRead(BTN_DOWN)==LOW) { selected = (selected+1)%4; delay(150); }
         if(digitalRead(BTN_UP)==LOW) { selected = (selected-1+4)%4; delay(150); }
         if(digitalRead(BTN_BACK)==LOW) return; 
         if(digitalRead(BTN_OK)==LOW) {
             delay(200);
             if(selected == 0) runBruteCAME();
             if(selected == 1) runBruteNice();
             if(selected == 2) runBruteChamberlain();
             if(selected == 3) runBruteHoltek();
         }
         delay(20);
    }
}

#endif