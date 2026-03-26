#ifndef SUBGHZ_RAW_SEND_H
#define SUBGHZ_RAW_SEND_H

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


void runSubGHzSendFile() {
    if (!sdReady) {
        u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"SD Error"); u8g2.sendBuffer(); delay(1000); return;
    }


    while(digitalRead(BTN_OK) == LOW) { delay(10); } 
    delay(200); 


    String files[30]; 
    int fileCount = 0;
    
    File dir = SD.open("/subghz");
    if(!dir) { SD.mkdir("/subghz"); dir = SD.open("/subghz"); }

    File entry = dir.openNextFile();
    while (entry && fileCount < 30) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            if (name.startsWith("/subghz/")) name.replace("/subghz/", ""); 
            files[fileCount] = name;
            fileCount++;
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    if (fileCount == 0) {
        u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"No .sub files"); u8g2.sendBuffer(); delay(1000); return;
    }

    int selected = 0;
    while(true) {
        u8g2.clearBuffer(); drawStars(); 
        u8g2.drawStr(0, 10, "Select File:");
        for(int i=0; i<4; i++) {
            int idx = selected + i;
            if(idx < fileCount) {
                u8g2.drawStr(10, 25 + i*10, files[idx].c_str());
                if(i==0) u8g2.drawStr(0, 25, ">");
            }
        }
        u8g2.sendBuffer();

        if(digitalRead(BTN_DOWN) == LOW) { selected = (selected+1)%fileCount; delay(150); }
        if(digitalRead(BTN_BACK) == LOW) return;
        if(digitalRead(BTN_OK) == LOW) break; 
        delay(10);
    }

    String fullPath = "/subghz/" + files[selected];
    File file = SD.open(fullPath);
    if(!file) return;

    String protocol = "RAW"; 
    unsigned long code = 0;
    float freq = 433.92;
    int bits = 0;
    int pulseLen = 320;
    bool isRawFile = false;


    while(file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if(line.startsWith("Frequency:")) {
            freq = line.substring(10).toFloat() / 1000000.0;
        }
        else if(line.startsWith("Protocol:")) {
            protocol = line.substring(9);
            protocol.trim();
            if(protocol == "RAW") isRawFile = true;
        }
        else if(line.startsWith("Key:")) {
            isRawFile = false; 
            String keyStr = line.substring(4);
            keyStr.trim();
            String hex = "";
            for(unsigned int i=0; i<keyStr.length(); i++) if(keyStr[i] != ' ') hex += keyStr[i];
            if(hex.length() > 8) hex = hex.substring(hex.length() - 8); 
            code = strtoul(hex.c_str(), NULL, 16);
        }
        else if(line.startsWith("Bit:")) bits = line.substring(4).toInt();
        else if(line.startsWith("TE:")) pulseLen = line.substring(3).toInt();
        

        if(line.startsWith("RAW_Data:")) { 
            isRawFile = true; 
            break; 
        }
    }
    

    ELECHOUSE_cc1101.setMHZ(freq);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.SetTx();
    
    u8g2.clearBuffer(); drawStars();
    u8g2.drawStr(0, 20, "Sending...");
    u8g2.drawStr(0, 35, protocol.c_str());
    char buf[32]; snprintf(buf, sizeof(buf), "%.2f MHz", freq);
    u8g2.drawStr(0, 50, buf);
    u8g2.sendBuffer();


    
    if (!isRawFile) {

        mySwitch.enableTransmit(CC1101_GDO0);
        mySwitch.setPulseLength(pulseLen > 0 ? pulseLen : 320);
        
        if(protocol.equalsIgnoreCase("CAME")) mySwitch.setProtocol(1);
        else if(protocol.equalsIgnoreCase("Nice FLO")) mySwitch.setProtocol(2);
        else mySwitch.setProtocol(1);

        for(int i=0; i<10; i++) {
            mySwitch.send(code, bits > 0 ? bits : 12);
            delay(10);
        }
        mySwitch.disableTransmit();
    } 
    else {

        file.close();
        file = SD.open(fullPath); 
        
        bool dataFound = false;
        while(file.available()) {
            String line = file.readStringUntil(' ');
            if(line.indexOf("RAW_Data:") >= 0) {
                dataFound = true;
                break;
            }
        }

        if(dataFound) {
            pinMode(CC1101_GDO0, OUTPUT);
            digitalWrite(CC1101_GDO0, LOW);
            

            while(file.available()) {
                if(digitalRead(BTN_BACK) == LOW) break; 

                String token = file.readStringUntil(' ');
                token.trim();
                if(token.length() > 0) {
                    long duration = token.toInt();
                    if(duration != 0) {
                        if(duration > 0) {
                            digitalWrite(CC1101_GDO0, HIGH);
                            delayMicroseconds(duration);
                        } else {
                            digitalWrite(CC1101_GDO0, LOW);
                            delayMicroseconds(abs(duration));
                        }
                    }
                }
            }
            digitalWrite(CC1101_GDO0, LOW);
        }
    }

    file.close();
    ELECHOUSE_cc1101.SetRx(); 
    delay(500);
}

#endif