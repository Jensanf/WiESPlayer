#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebSrv.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <EEPROM.h>

//Defines
#define EEPROM_SIZE 512
#define GLOABAL_SSID_ADDR 0
#define GLOABAL_PASSWORD_ADDR 100
#define MAX_ATTEMPTS 20            // How many repeates do WIFi connection

//Constants
const char* AP_SSID = "ESP32";
const char* AP_PASSWORD = "12345678";
const char* HOSTNAME = "esp32";
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'>"
                          "<input type='file' name='update'><input type='submit' value='Update'></form>";
//Server
AsyncWebServer server(80);

// Function prototypes
void createWiFi_AP();
void configOTA();
bool connectToGlobalWiFi(const char* ssid, const char* password);
String readStringEEPROM(int addr);
void writeStringEEPROM(int addr, String data);

void setup() {
  EEPROM.begin(EEPROM_SIZE);
  if(!connectToGlobalWiFi(readStringEEPROM(GLOABAL_SSID_ADDR).c_str(), readStringEEPROM(GLOABAL_PASSWORD_ADDR).c_str())) {
    createWiFi_AP();
  }
  MDNS.begin(HOSTNAME);
  configOTA();
  configWiFiLoginData(GLOABAL_SSID_ADDR, GLOABAL_PASSWORD_ADDR); 
  MDNS.addService("http", "tcp", 80);
  
}

void loop() {
  delay(2); 
}

bool connectToGlobalWiFi(const char* ssid, const char* password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attemptCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    attemptCount++;
    if(attemptCount > MAX_ATTEMPTS) return false; 
  }
  return true;
}

void createWiFi_AP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
}

void configOTA() {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", serverIndex);
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    int code = Update.hasError() ? 500 : 200;
    request->send(code, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(100);
    ESP.restart();
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
        Update.printError(Serial);
      }
    }
    if (!Update.hasError()) {
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
    }
    if (final) {
      if (!Update.end(true)) {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
}

void configWiFiLoginData (int ssid_addr, int pass_addr) {
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    String s = "<html><body>";
    s += "<form method='post' action='/wifi'>";
    s += "SSID:<br><input type='text' name='ssid'><br>";
    s += "Password:<br><input type='text' name='password'><br>";
    s += "<input type='submit' value='Submit'>";
    s += "</form></body></html>";
    request->send(200, "text/html", s);
  });
  server.on("/wifi", HTTP_POST, [=](AsyncWebServerRequest *request){
    String ssid;
    String password;
    if(request->hasParam("ssid", true)) {
      ssid = request->getParam("ssid", true)->value();
      writeStringEEPROM(ssid_addr, ssid);
    }
    if(request->hasParam("password", true)){
      password = request->getParam("password", true)->value();
      writeStringEEPROM(pass_addr, password);
    }
    ESP.restart();
  });
}

/* Function to write in EEPROM string data */ 
void writeStringEEPROM(int addr, String data) {
  int len = data.length();
  EEPROM.write(addr, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + 1 + i, data[i]);
  }
  EEPROM.commit();
}
/* Function to read string from EEPROM by address */ 
String readStringEEPROM(int addr) {
  int len = EEPROM.read(addr);
  String data = "";
  for (int i = 0; i < len; i++) {
    data = data + (char)EEPROM.read(addr + 1 + i);
  }
  return data;
}