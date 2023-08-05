#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebSrv.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"

// Digital I/O used
#define SD_CS          5
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18
#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26

// Defines
#define EEPROM_SIZE 512
#define GLOABAL_SSID_ADDR 0
#define GLOABAL_PASSWORD_ADDR 100
#define MAX_ATTEMPTS 20            // How many repeates do WIFi connection
#define SPI_FREQ 10000000

#define MAX_TRACKS 1000
String tracks[MAX_TRACKS];
int trackCount = 0;
int currentTrack = 0;

// Constants
const char* AP_SSID = "ESP32";
const char* AP_PASSWORD = "12345678";
const char* HOSTNAME = "esp32";
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'>"
                          "<input type='file' name='update'><input type='submit' value='Update'></form>";

// Global variables
int currentVolume = 5; // Starting volume of audio player

// Server
AsyncWebServer server(80);

Audio audio;
File rootDirectory;
HTTPClient httpClient;

// Function prototypes
void createWiFi_AP(void);
void configOTA(void);
bool connectToGlobalWiFi(const char* ssid, const char* password);
String readStringEEPROM(int addr);
void writeStringEEPROM(int addr, String data);
bool isMusicFile(String fileName);

void setup() {
  pinMode(SD_CS, OUTPUT);      
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setFrequency(SPI_FREQ);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  
  while (!SD.begin(SD_CS));

  EEPROM.begin(EEPROM_SIZE);
  if(!connectToGlobalWiFi(readStringEEPROM(GLOABAL_SSID_ADDR).c_str(), 
                          readStringEEPROM(GLOABAL_PASSWORD_ADDR).c_str())) {
    createWiFi_AP();
  }
  MDNS.begin(HOSTNAME);
  
  configOTA();
  configWiFiLoginData(GLOABAL_SSID_ADDR, GLOABAL_PASSWORD_ADDR);
  server.begin();
  
  MDNS.addService("http", "tcp", 80);
  audio.setVolume(currentVolume); // 0...21

  rootDirectory = SD.open("/");
  File file = rootDirectory.openNextFile();
  while(file && trackCount < MAX_TRACKS) {
    if (isMusicFile(file.name())) {
      tracks[trackCount++] = String(file.name());
    }
    file = rootDirectory.openNextFile();
  }
  audio.connecttoSD(tracks[currentTrack].c_str());
}

void loop() {
  audio.loop();
}

void audio_eof_mp3(const char *info) {
  if (trackCount > 0 ) {
    currentTrack = (currentTrack + 1) % trackCount; // loop back to the first track after the last one
    audio.connecttoSD(tracks[currentTrack].c_str());  // start playing the next track
  }
}
// void audio_info(const char *info) {
//   Serial.print("info        "); 
//   Serial.println(info);
// }

bool isMusicFile(String fileName) {
  String extension = fileName.substring(fileName.lastIndexOf('.') + 1);
  extension.toLowerCase();
  return (extension == "mp3" || extension == "MP3");
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
    audio.pauseResume();  // Mute audio here before starting the update
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