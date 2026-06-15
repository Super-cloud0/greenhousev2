#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <DHT.h>
#include <HTTPClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

#define MQTT_SERVER   "io.adafruit.com"
#define MQTT_PORT     1883
#define MQTT_USERNAME "YOUR_MQTT_USERNAME"
#define MQTT_PASSWORD "YOUR_MQTT_SECRET_KEY"
#define CLIENT_ID     "ESP32_SmartGarden_PRO"
#define DATA_TOPIC    "brobropups/feeds/smartgarden_data"
#define CONTROL_TOPIC "brobropups/feeds/smartgarden_control"
#define LOG_TOPIC     "brobropups/feeds/smartgarden_log"

#define DHTPIN          22
#define DHTTYPE         DHT11
#define SOIL_PIN        34
#define WATER_SENSOR_PIN 27
#define PUMP_RELAY_PIN  25
#define LIGHT_RELAY_PIN 26
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

const int DRY_VAL = 4095;
const int WET_VAL = 1918;

TFT_eSPI      tft = TFT_eSPI();
AsyncWebServer server(80);
WiFiClient    espClient;
PubSubClient  mqtt(espClient);
DHT           dht(DHTPIN, DHTTYPE);

float temp = 0.0, hum = 0.0;
int   soilPct = 0;
bool  pumpOn = false;
bool  waterOk = true;
int   autoWaterThreshold = 30;

int   currentTab = 0;
int   prevTab = 0;

String logResponseText = "Press MSG to send command";
#define MAX_LOG_HISTORY 20
String logHistory[MAX_LOG_HISTORY];
int logHistoryCount = 0;
int logScrollOffset = 0;
String inputBuffer = "";
String camIP = "192.168.1.100";
bool  camConnected = false;
bool  flashOn = false;

unsigned long lastSensorRead = 0;
const int SENSOR_DELAY = 2500;
unsigned long lastCamCheck = 0;

float prevTemp = -999, prevHum = -999;
int   prevSoil = -999;
bool  prevPump = false, prevWater = true;

bool kbRussian = false;
const char* KB_EN_R1 = "QWERTYUIOP";
const char* KB_EN_R2 = "ASDFGHJKL";
const char* KB_EN_R3 = "ZXCVBNM";
const char* TRANSLIT_MAP = "QWERTYUIOPASDFGHJKLZXCVBNM";

void checkCamConnection() {
  if (WiFi.status() != WL_CONNECTED) { camConnected = false; return; }
  HTTPClient http;
  http.begin("http://" + camIP + "/status");
  http.setTimeout(1500);
  int code = http.GET();
  camConnected = (code > 0);
  http.end();
}

void drawTabsMenu() {
  tft.fillRect(0, 205, 320, 35, tft.color565(30,30,30));
  tft.drawLine(0, 205, 320, 205, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);

  tft.fillRoundRect(10,  210, 95, 25, 4, currentTab==0 ? TFT_GREEN  : tft.color565(60,60,60));
  tft.setTextColor(currentTab==0 ? TFT_BLACK : TFT_WHITE);
  tft.drawString("HOME", 57, 222, 2);

  tft.fillRoundRect(112, 210, 95, 25, 4, currentTab==1 ? TFT_CYAN   : tft.color565(60,60,60));
  tft.setTextColor(currentTab==1 ? TFT_BLACK : TFT_WHITE);
  tft.drawString("CAMERA", 159, 222, 2);

  tft.fillRoundRect(215, 210, 95, 25, 4, currentTab==2 ? TFT_ORANGE : tft.color565(60,60,60));
  tft.setTextColor(currentTab==2 ? TFT_BLACK : TFT_WHITE);
  tft.drawString("CONSOLE", 262, 222, 2);
}

void updateDashboardData() {
  if (currentTab != 0) return;
  tft.setTextDatum(MC_DATUM);
  if (temp != prevTemp) {
    tft.fillRect(10, 50, 145, 52, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW);
    tft.drawFloat(temp, 1, 80, 75, 6);
    prevTemp = temp;
  }
  if (hum != prevHum) {
    tft.fillRect(163, 50, 145, 52, TFT_BLACK);
    tft.setTextColor(TFT_CYAN);
    tft.drawFloat(hum, 1, 235, 75, 6);
    prevHum = hum;
  }
  if (soilPct != prevSoil) {
    tft.fillRect(6, 135, 196, 52, TFT_BLACK);
    tft.setTextColor(soilPct < autoWaterThreshold ? TFT_RED : TFT_GREEN);
    char s[10]; sprintf(s, "%d %%", soilPct);
    tft.drawString(s, 105, 160, 6);
    prevSoil = soilPct;
  }
  if (pumpOn != prevPump || waterOk != prevWater) {
    tft.fillRect(211, 135, 98, 58, TFT_BLACK);
    tft.setTextColor(pumpOn ? TFT_GREEN : tft.color565(150,150,150));
    tft.drawString(pumpOn ? "PUMP: ON" : "PUMP:OFF", 260, 152, 2);
    tft.setTextColor(waterOk ? TFT_CYAN : TFT_RED);
    tft.drawString(waterOk ? "H2O: OK" : "H2O: LOW", 260, 174, 2);
    prevPump = pumpOn; prevWater = waterOk;
  }
}

void drawKeyboard() {
  tft.fillRect(0, 24, 320, 181, TFT_BLACK);
  tft.fillRect(0, 24, 320, 28, tft.color565(20,20,40));
  tft.drawRect(0, 24, 320, 28, tft.color565(80,80,120));
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
  String display = inputBuffer.length() > 22 ? inputBuffer.substring(inputBuffer.length()-22) : inputBuffer;
  tft.drawString(display + "_", 4, 30, 2);
  
  tft.fillRoundRect(258, 26, 58, 22, 3, kbRussian ? tft.color565(0,100,200) : tft.color565(80,80,80));
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(kbRussian ? "RU" : "EN", 287, 37, 2);

  for (int i = 0; i < 10; i++) {
    int bx = 1 + i * 32;
    tft.fillRoundRect(bx, 55, 29, 22, 3, tft.color565(50,50,70));
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawChar(KB_EN_R1[i], bx+14, 66, 2);
  }
  for (int i = 0; i < 9; i++) {
    int bx = 17 + i * 32;
    tft.fillRoundRect(bx, 82, 29, 22, 3, tft.color565(50,50,70));
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawChar(KB_EN_R2[i], bx+14, 93, 2);
  }
  for (int i = 0; i < 7; i++) {
    int bx = 33 + i * 36;
    tft.fillRoundRect(bx, 109, 32, 22, 3, tft.color565(50,50,70));
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawChar(KB_EN_R3[i], bx+16, 120, 2);
  }

  tft.fillRoundRect(3,   136, 100, 22, 3, tft.color565(40,40,60));
  tft.setTextColor(tft.color565(180,180,180));
  tft.drawString("SPACE", 53, 147, 2);

  tft.fillRoundRect(108, 136, 60, 22, 3, tft.color565(120,40,40));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("DEL", 138, 147, 2);

  tft.fillRoundRect(173, 136, 60, 22, 3, tft.color565(60,60,60));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("BACK", 203, 147, 2);

  tft.fillRoundRect(238, 136, 79, 22, 3, tft.color565(0,140,0));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("SEND", 277, 147, 2);

  for (int i = 0; i < 10; i++) {
    int bx = 3 + i * 31;
    tft.fillRoundRect(bx, 163, 28, 18, 3, tft.color565(40,60,40));
    tft.setTextColor(tft.color565(150,220,150));
    tft.drawChar('0' + ((i+1)%10), bx+14, 172, 2);
  }

  tft.fillRoundRect(313, 163, 15, 18, 3, tft.color565(40,60,40));
  tft.setTextColor(tft.color565(150,220,150));
  tft.drawString(".", 320, 172, 2);
}

void drawCameraTab() {
  tft.setTextDatum(MC_DATUM);

  tft.fillRect(5, 30, 310, 20, TFT_BLACK);
  if (camConnected) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("CAM: ONLINE  " + camIP, 160, 40, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("CAM: OFFLINE - tap IP to set", 160, 40, 2);
  }

  tft.fillRoundRect(5, 55, 150, 30, 6, tft.color565(40,40,80));
  tft.drawRect(5, 55, 150, 30, tft.color565(80,80,150));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("IP: " + camIP, 80, 70, 2);

  tft.fillRoundRect(165, 55, 150, 30, 6, flashOn ? tft.color565(180,180,0) : tft.color565(60,60,30));
  tft.setTextColor(flashOn ? TFT_BLACK : TFT_WHITE);
  tft.drawString(flashOn ? "FLASH: ON" : "FLASH: OFF", 240, 70, 2);

  tft.fillRoundRect(5, 95, 310, 40, 6, camConnected ? tft.color565(0,80,0) : tft.color565(40,40,40));
  tft.setTextColor(camConnected ? TFT_GREEN : tft.color565(100,100,100));
  tft.drawString(camConnected ? "VIEW STREAM (open in browser)" : "STREAM unavailable", 160, 115, 2);

  if (camConnected) {
    tft.setTextColor(tft.color565(100,200,100));
    tft.setTextDatum(TL_DATUM);
    tft.drawString("http://" + camIP + ":81/stream", 10, 145, 2);
    tft.setTextDatum(MC_DATUM);
  }

  tft.fillRoundRect(80, 165, 160, 30, 6, tft.color565(30,60,80));
  tft.setTextColor(TFT_CYAN);
  tft.drawString("CHECK CONNECTION", 160, 180, 2);
}

void addToLog(String prefix, String msg) {
  String full = prefix + msg;
  while (full.length() > 0) {
    if (logHistoryCount < MAX_LOG_HISTORY) {
      logHistory[logHistoryCount++] = full.substring(0, min((int)full.length(), 36));
      full = full.length() > 36 ? full.substring(36) : "";
    } else {
      for (int i = 0; i < MAX_LOG_HISTORY - 1; i++) logHistory[i] = logHistory[i+1];
      logHistory[MAX_LOG_HISTORY-1] = full.substring(0, min((int)full.length(), 36));
      full = full.length() > 36 ? full.substring(36) : "";
    }
  }
  logScrollOffset = 0;
}

void drawConsoleTab() {
  tft.drawRect(5, 30, 310, 110, TFT_DARKGREY);
  tft.fillRect(6, 31, 308, 108, tft.color565(10,15,20));

  int linesVisible = 7;
  int startIdx = max(0, logHistoryCount - linesVisible - logScrollOffset);
  int endIdx = min(logHistoryCount, startIdx + linesVisible);

  tft.setTextDatum(TL_DATUM);
  for (int i = startIdx; i < endIdx; i++) {
    int lineY = 35 + (i - startIdx) * 14;
    if (logHistory[i].startsWith("TX:")) tft.setTextColor(TFT_YELLOW);
    else if (logHistory[i].startsWith("RX:")) tft.setTextColor(TFT_GREEN);
    else tft.setTextColor(tft.color565(150,150,150));
    tft.drawString(logHistory[i], 8, lineY, 2);
  }

  if (logHistoryCount > linesVisible) {
    tft.setTextColor(tft.color565(80,80,80));
    tft.setTextDatum(TR_DATUM);
    if (logScrollOffset > 0) tft.drawString("^", 318, 33, 2);
    if (logScrollOffset < logHistoryCount - linesVisible) tft.drawString("v", 318, 120, 2);
  }

  tft.setTextDatum(MC_DATUM);
  tft.fillRoundRect(5,   147, 75, 28, 6, TFT_ORANGE);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("STATUS", 42, 161, 2);

  tft.fillRoundRect(85, 147, 80, 28, 6, tft.color565(0,120,180));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("MSG", 125, 161, 2);

  tft.fillRoundRect(170, 147, 35, 28, 6, tft.color565(120,30,30));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("CLR", 187, 161, 2);

  tft.fillRoundRect(210, 147, 45, 28, 6, tft.color565(50,50,80));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("^", 232, 161, 2);

  tft.fillRoundRect(260, 147, 45, 28, 6, tft.color565(50,50,80));
  tft.drawString("v", 282, 161, 2);
}

void drawIPInput() {
  tft.fillRect(0, 0, 320, 240, TFT_BLACK);
  tft.fillRect(0, 0, 320, 20, tft.color565(20,20,50));
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(" ENTER CAMERA IP", 5, 4, 2);

  tft.fillRect(5, 25, 310, 25, tft.color565(20,20,40));
  tft.drawRect(5, 25, 310, 25, tft.color565(80,80,150));
  tft.setTextColor(TFT_WHITE);
  tft.drawString(inputBuffer + "_", 10, 31, 2);

  for (int i = 1; i <= 9; i++) {
    int col = (i-1) % 3;
    int row = (i-1) / 3;
    int bx = 20 + col * 90;
    int by = 58 + row * 42;
    tft.fillRoundRect(bx, by, 70, 32, 5, tft.color565(50,50,80));
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(String(i), bx+35, by+16, 4);
  }
  tft.fillRoundRect(110, 184, 70, 32, 5, tft.color565(50,50,80));
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("0", 145, 200, 4);
  
  tft.fillRoundRect(20, 184, 70, 32, 5, tft.color565(50,50,80));
  tft.drawString(".", 55, 200, 4);
  
  tft.fillRoundRect(200, 184, 70, 32, 5, tft.color565(120,40,40));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("DEL", 235, 200, 2);
  
  tft.fillRoundRect(20, 222, 120, 14, 4, tft.color565(0,120,0));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("OK", 80, 229, 2);
  
  tft.fillRoundRect(160, 222, 140, 14, 4, tft.color565(100,40,40));
  tft.drawString("CANCEL", 230, 229, 2);
}

void drawScreen() {
  if (currentTab == 3) { drawKeyboard(); return; }
  if (currentTab == 4) { drawIPInput(); return; }

  tft.fillRect(0, 0, 320, 24, tft.color565(20,20,35));
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  String titles[] = {" DASHBOARD", " CAMERA CTRL", " SYSTEM CONSOLE"};
  tft.drawString(titles[currentTab], 5, 4, 2);

  tft.fillRect(0, 24, 320, 181, TFT_BLACK);
  drawTabsMenu();
  tft.setTextDatum(MC_DATUM);

  if (currentTab == 0) {
    tft.drawRoundRect(5,   30, 150, 80, 4, tft.color565(50,50,50));
    tft.fillRect(5,   30, 150, 18, tft.color565(150,40,40));
    tft.setTextColor(TFT_WHITE); tft.drawString("TEMP C", 80, 39, 2);

    tft.drawRoundRect(160, 30, 150, 80, 4, tft.color565(50,50,50));
    tft.fillRect(160, 30, 150, 18, tft.color565(40,100,200));
    tft.drawString("HUMIDITY %", 235, 39, 2);

    tft.drawRoundRect(5,  115, 200, 80, 4, tft.color565(50,50,50));
    tft.fillRect(5,  115, 200, 18, tft.color565(40,150,60));
    tft.drawString("SOIL MOISTURE", 105, 124, 2);

    tft.drawRoundRect(210, 115, 100, 80, 4, tft.color565(50,50,50));
    tft.fillRect(210, 115, 100, 18, tft.color565(80,80,80));
    tft.drawString("PUMP & H2O", 260, 124, 2);

    prevTemp = prevHum = -999; prevSoil = -999;
    prevPump = !pumpOn; prevWater = !waterOk;
    updateDashboardData();

  } else if (currentTab == 1) {
    drawCameraTab();
  } else if (currentTab == 2) {
    drawConsoleTab();
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  if (String(topic) == CONTROL_TOPIC) {
    if (msg == "PUMP_ON"  && !pumpOn) { pumpOn = true;  digitalWrite(PUMP_RELAY_PIN, RELAY_ON);  }
    if (msg == "PUMP_OFF" &&  pumpOn) { pumpOn = false; digitalWrite(PUMP_RELAY_PIN, RELAY_OFF); }
    if (msg == "CLEAR_CHAT") {
      logHistoryCount = 0; logScrollOffset = 0;
      if (currentTab == 2) drawConsoleTab();
    }
  } else if (String(topic) == LOG_TOPIC) {
    logResponseText = msg;
    addToLog("RX: ", msg);
    if (currentTab == 2) drawConsoleTab();
  }
}

void reconnect() {
  if (!mqtt.connected()) {
    if (mqtt.connect(CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
      mqtt.subscribe(CONTROL_TOPIC);
      mqtt.subscribe(LOG_TOPIC);
    }
  }
}

void toggleFlash() {
  flashOn = !flashOn;
  HTTPClient http;
  String url = "http://" + camIP + "/control?var=led_intensity&val=" + (flashOn ? "255" : "0");
  http.begin(url);
  http.setTimeout(2000);
  http.GET();
  http.end();
  if (currentTab == 1) drawCameraTab();
}

void handleKeyboardTouch(int x, int y) {
  if (x >= 258 && x <= 316 && y >= 26 && y <= 48) {
    kbRussian = !kbRussian;
    drawKeyboard();
    delay(200);
    return;
  }
  if (y >= 55 && y <= 77) {
    int idx = (x - 1) / 32;
    if (idx >= 0 && idx < 10) { inputBuffer += KB_EN_R1[idx]; drawKeyboard(); delay(150); return; }
  }
  if (y >= 82 && y <= 104) {
    int idx = (x - 17) / 32;
    if (idx >= 0 && idx < 9) { inputBuffer += KB_EN_R2[idx]; drawKeyboard(); delay(150); return; }
  }
  if (y >= 109 && y <= 131) {
    int idx = (x - 33) / 36;
    if (idx >= 0 && idx < 7) { inputBuffer += KB_EN_R3[idx]; drawKeyboard(); delay(150); return; }
  }
  if (y >= 136 && y <= 158) {
    if (x < 103) { inputBuffer += " "; drawKeyboard(); delay(150); return; }
    if (x >= 108 && x < 168) {
      if (inputBuffer.length() > 0) inputBuffer.remove(inputBuffer.length()-1);
      drawKeyboard(); delay(150); return;
    }
    if (x >= 173 && x < 233) {
      inputBuffer = "";
      currentTab = prevTab;
      drawScreen();
      delay(200);
      return;
    }
    if (x >= 238) {
      if (inputBuffer.length() > 0) {
        if (prevTab == 2) {
          addToLog("TX: ", inputBuffer);
          drawConsoleTab();
          String chatMsg = "CMD:" + inputBuffer;
          if (mqtt.connected()) mqtt.publish(CONTROL_TOPIC, chatMsg.c_str());
        } else if (prevTab == 4) {
          camIP = inputBuffer;
        }
        inputBuffer = "";
      }
      currentTab = prevTab == 4 ? 1 : prevTab;
      drawScreen();
      delay(200);
      return;
    }
  }
  if (y >= 163 && y <= 181) {
    int idx = (x - 3) / 31;
    if (idx >= 0 && idx < 10) {
      inputBuffer += String((idx + 1) % 10);
      drawKeyboard(); delay(150);
    }
  }
}

void handleIPInputTouch(int x, int y) {
  if (y >= 58 && y <= 216) {
    for (int i = 1; i <= 9; i++) {
      int col = (i-1) % 3;
      int row = (i-1) / 3;
      int bx = 20 + col * 90;
      int by = 58 + row * 42;
      if (x >= bx && x <= bx+70 && y >= by && y <= by+32) {
        inputBuffer += String(i);
        drawIPInput(); delay(150); return;
      }
    }
  }
  if (x >= 110 && x <= 180 && y >= 184 && y <= 216) { inputBuffer += "0"; drawIPInput(); delay(150); return; }
  if (x >= 20  && x <=  90 && y >= 184 && y <= 216) { inputBuffer += "."; drawIPInput(); delay(150); return; }
  if (x >= 200 && x <= 270 && y >= 184 && y <= 216) {
    if (inputBuffer.length() > 0) inputBuffer.remove(inputBuffer.length()-1);
    drawIPInput(); delay(150); return;
  }
  if (y >= 222 && x < 160) {
    if (inputBuffer.length() > 0) camIP = inputBuffer;
    inputBuffer = "";
    currentTab = 1;
    checkCamConnection();
    drawScreen();
    delay(200);
    return;
  }
  if (y >= 222 && x >= 160) {
    inputBuffer = "";
    currentTab = 1;
    drawScreen();
    delay(200);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PUMP_RELAY_PIN));
  REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << LIGHT_RELAY_PIN));
  pinMode(WATER_SENSOR_PIN, INPUT);
  pinMode(LIGHT_RELAY_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN,  RELAY_OFF);
  digitalWrite(LIGHT_RELAY_PIN, RELAY_OFF);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  uint16_t calData[5] = { 406, 3482, 430, 3186, 7 };
  tft.setTouch(calData);

  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Smart Garden v2", 160, 90, 4);
  tft.setTextColor(tft.color565(150,150,150));
  tft.drawString("Connecting WiFi...", 160, 120, 2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) { delay(500); tries++; }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi OK: " + WiFi.localIP().toString());
    tft.setTextColor(TFT_GREEN);
    tft.drawString("WiFi OK!", 160, 138, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("WiFi FAILED", 160, 138, 2);
  }
  delay(600);

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  dht.begin();

  drawScreen();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 5000) { lastReconnect = millis(); reconnect(); }
  }
  if (mqtt.connected()) mqtt.loop();

  if (millis() - lastCamCheck > 15000) {
    lastCamCheck = millis();
    bool wasConnected = camConnected;
    checkCamConnection();
    if (camConnected != wasConnected && currentTab == 1) drawCameraTab();
  }

  if (millis() - lastSensorRead >= SENSOR_DELAY) {
    lastSensorRead = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) { temp = t; hum = h; }

    int rawSoil = analogRead(SOIL_PIN);
    soilPct = constrain(map(rawSoil, DRY_VAL, WET_VAL, 0, 100), 0, 100);
    waterOk = (digitalRead(WATER_SENSOR_PIN) == LOW);

    if (soilPct < autoWaterThreshold && waterOk && !pumpOn) {
      pumpOn = true; digitalWrite(PUMP_RELAY_PIN, RELAY_ON);
    } else if ((soilPct >= autoWaterThreshold || !waterOk) && pumpOn) {
      pumpOn = false; digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
    }

    String payload = String(soilPct) + "," + String(temp,1) + "," +
                     String(hum,0)   + "," + String(pumpOn ? 1 : 0);
    if (mqtt.connected()) mqtt.publish(DATA_TOPIC, payload.c_str());

    if (currentTab == 0) updateDashboardData();
  }

  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    if (currentTab == 3) { handleKeyboardTouch(x, y); return; }
    if (currentTab == 4) { handleIPInputTouch(x, y); return; }

    if (y > 200) {
      if      (x < 110  && currentTab != 0) { currentTab = 0; drawScreen(); }
      else if (x < 215  && currentTab != 1) { currentTab = 1; drawScreen(); }
      else if (x >= 215 && currentTab != 2) { currentTab = 2; drawScreen(); }
      delay(300);
      return;
    }

    if (currentTab == 1) {
      if (x >= 5 && x <= 155 && y >= 55 && y <= 85) {
        inputBuffer = camIP;
        prevTab = 4;
        currentTab = 4;
        drawIPInput();
        delay(200);
      }
      if (x >= 165 && x <= 315 && y >= 55 && y <= 85) {
        toggleFlash();
        delay(200);
      }
      if (y >= 165 && y <= 195) {
        checkCamConnection();
        drawCameraTab();
        delay(200);
      }
    }

    if (currentTab == 2) {
      if (x >= 170 && x <= 205 && y >= 147 && y <= 175) {
        logHistoryCount = 0; logScrollOffset = 0;
        drawConsoleTab(); delay(150);
      }
      if (x >= 210 && x <= 255 && y >= 147 && y <= 175) {
        if (logScrollOffset < max(0, logHistoryCount - 7)) logScrollOffset++;
        drawConsoleTab(); delay(150);
      }
      if (x >= 260 && y >= 147 && y <= 175) {
        if (logScrollOffset > 0) logScrollOffset--;
        drawConsoleTab(); delay(150);
      }
      if (x >= 5 && x <= 80 && y >= 147 && y <= 175) {
        addToLog("TX: ", "REQ_STATUS");
        drawConsoleTab();
        if (mqtt.connected()) mqtt.publish(CONTROL_TOPIC, "GET_STATUS");
        delay(300);
      }
      if (x >= 85 && x <= 165 && y >= 147 && y <= 175) {
        inputBuffer = "";
        prevTab = 2;
        currentTab = 3;
        drawKeyboard();
        delay(200);
      }
    }
  }
}
