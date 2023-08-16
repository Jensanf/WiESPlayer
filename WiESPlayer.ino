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
#include "RTClib.h"

// Digital I/O used
#define SD_CS          5
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18
#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26

// Defines
#define EEPROM_SIZE 256
#define GLOABAL_SSID_ADDR 0
#define GLOABAL_PASSWORD_ADDR 100
#define VOLUME_ADDR 200
#define ALARM_HOUR_ADDR 201
#define ALARM_MIN_ADDR 202
#define ALARM_FLAG_ADDR 203
#define DAY_NIGHT_FLAG_ADDR 204 
#define START_NIGHT_HOUR_ADDR 205
#define START_NIGHT_MIN_ADDR 206
#define START_DAY_HOUR_ADDR 207
#define START_DAY_MIN_ADDR 208
#define NIGHT_VOLUME_ADDR 209
#define DAY_VOLUME_ADDR 210
#define ALARM_VOLUME_ADDR 211
#define ALARM_SONG_ADDR 220
#define MAX_ATTEMPTS 20            // How many repeates do WIFi connection
#define MAX_SONGNAME_LENGTH 30
#define SPI_FREQ 10000000

#define MAX_TRACKS 100
String tracks[MAX_TRACKS];
int trackCount = 0;
int currentTrack = 0;

// Constants
const char* AP_SSID = "ESP32";
const char* AP_PASSWORD = "12345678";
const char* HOSTNAME = "esp32";

// Global variables
int currentVolume = 5; // Starting volume of audio player
bool flagNextSong = false; 
bool flagPrevSong = false; 

// Player modes
bool flagModeAlarm = false;
bool flagAlarm = false; 
bool flagModeDayNight = false; 

// Alarm time
int alarmHour = 0;
int alarmMinute = 0;
int startNightHour = 0;
int startDayHour = 0; 
int startNightMinute = 0; 
int startDayMinute = 0; 
int nightVolume = 0; 
int dayVolume = 0; 
int alarmVolume = 0; 
String alarmSong = "";
// Server
AsyncWebServer server(80);

Audio audio;
File rootDirectory;
HTTPClient httpClient;
RTC_DS3231 rtc;
DateTime now;

// Function prototypes
void createWiFi_AP(void);
bool connectToGlobalWiFi(const char* ssid, const char* password);
String readStringEEPROM(int addr);
void writeStringEEPROM(int addr, String data);
bool isMusicFile(String fileName);
void configOTAPage(void);
void configGlobalWiFiPage(void);
void configMainPage(void);
void configPlaylistPage(void);
void configClockPage(void);
void deleteTrack(String tracksList[], int &trackCount, const String &trackName); 
String createOkResponse(const String& url);

void setup() {
  rtc.begin();
  pinMode(SD_CS, OUTPUT);      
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setFrequency(SPI_FREQ);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  
  while (!SD.begin(SD_CS));

  EEPROM.begin(EEPROM_SIZE);

  currentVolume = EEPROM.read(VOLUME_ADDR);
  alarmHour = EEPROM.read(ALARM_HOUR_ADDR);
  alarmMinute = EEPROM.read(ALARM_MIN_ADDR);
  flagModeAlarm = EEPROM.read(ALARM_FLAG_ADDR);
  flagModeDayNight = EEPROM.read(DAY_NIGHT_FLAG_ADDR);
  startNightHour = EEPROM.read(START_NIGHT_HOUR_ADDR);
  startDayHour = EEPROM.read(START_DAY_HOUR_ADDR); 
  startNightMinute = EEPROM.read(START_NIGHT_MIN_ADDR); 
  startDayMinute = EEPROM.read(START_DAY_MIN_ADDR); 
  nightVolume = EEPROM.read(NIGHT_VOLUME_ADDR); 
  dayVolume = EEPROM.read(DAY_VOLUME_ADDR);
  alarmVolume = EEPROM.read(ALARM_VOLUME_ADDR); 

  currentVolume = (currentVolume > 21 || currentVolume < 0) ? 5 : currentVolume;
  alarmHour = (alarmHour > 24 || alarmHour < 0) ? 0 : alarmHour;
  alarmMinute = (alarmMinute > 59 || alarmMinute < 0) ? 0 : alarmMinute;
  flagModeAlarm = (flagModeAlarm > 1 || flagModeAlarm < 0) ? 0 : flagModeAlarm;
  flagModeDayNight = (flagModeDayNight > 1 || flagModeDayNight < 0) ? 0 : flagModeDayNight;
  startNightHour = (startNightHour > 24 || startNightHour < 0) ? 0 : startNightHour;
  startDayHour =(startDayHour > 24 || startDayHour < 0) ? 0 : startDayHour;
  startNightMinute = (startNightMinute > 59 || startNightMinute < 0) ? 0 : startNightMinute;
  startDayMinute = (startDayMinute > 59 || startDayMinute < 0) ? 0 : startDayMinute; 
  nightVolume = (nightVolume > 21 || nightVolume < 0) ? 5 : nightVolume;
  dayVolume = (dayVolume > 21 || dayVolume < 0) ? 5 : dayVolume;
  alarmVolume = (alarmVolume > 21 || alarmVolume < 0) ? 5 : alarmVolume;
  if (EEPROM.read(ALARM_SONG_ADDR) <= MAX_SONGNAME_LENGTH ){
    alarmSong = readStringEEPROM(ALARM_SONG_ADDR);
  }

  if(!connectToGlobalWiFi(readStringEEPROM(GLOABAL_SSID_ADDR).c_str(), 
                          readStringEEPROM(GLOABAL_PASSWORD_ADDR).c_str())) {
    createWiFi_AP();
  }
  MDNS.begin(HOSTNAME);
  
  configOTAPage();
  configGlobalWiFiPage(GLOABAL_SSID_ADDR, GLOABAL_PASSWORD_ADDR);
  configMainPage(); 
  configPlaylistPage();
  configClockPage();
  server.begin();
  
  MDNS.addService("http", "tcp", 80);
  bool alarmSongCheck = false; 
  rootDirectory = SD.open("/");
  File file = rootDirectory.openNextFile();
  while(file && trackCount < MAX_TRACKS) {
    if (isMusicFile(file.name())) {
      tracks[trackCount++] = String(file.name());
      if (alarmSong == String(file.name())) {       // Check if the alarm song in current track list
        alarmSongCheck = true;
      } 
    }
    file = rootDirectory.openNextFile();
  }
  currentTrack = esp_random() % trackCount; // random(0, trackCount);
  audio.connecttoSD(tracks[currentTrack].c_str());
  if (!alarmSongCheck) {
    int i = 0; 
    while(i < trackCount) {
      if (tracks[i].length() <= MAX_SONGNAME_LENGTH) {
        alarmSong = tracks[i];
        writeStringEEPROM(ALARM_SONG_ADDR, alarmSong);
        break;
      } else {
        i++; 
      }
    } 
  }
  audio.setVolume(currentVolume); // 0...21
  if(flagModeDayNight) {
    if ((now.hour() > startDayHour || (now.hour() == startDayHour && now.minute() >= startDayMinute)) 
      && (now.hour() < startNightHour || (now.hour() == startNightHour && now.minute() < startNightMinute))) {
      currentVolume = dayVolume;
      audio.setVolume(currentVolume);
    } else {
      currentVolume = nightVolume;
      audio.setVolume(currentVolume);
    }
  }
}

void loop() {
  DateTime now = rtc.now();
  audio.loop();
  // Check if it's time to alarm
  if (flagModeAlarm && flagAlarm && now.hour() == alarmHour && now.minute() == alarmMinute) {
    currentVolume = alarmVolume; 
    audio.setVolume(currentVolume);
    currentTrack = findIdSong(tracks, trackCount, alarmSong); 
    audio.connecttoSD(tracks[currentTrack].c_str());
    flagAlarm = false;
  } else if (now.minute() != alarmMinute && flagModeAlarm && !flagAlarm){
    flagAlarm = true;  // Change flag of alarm to repeat it tomorrow
  }
  if (flagNextSong) {
    currentTrack = (currentTrack + 1) % trackCount;
    audio.connecttoSD(tracks[currentTrack].c_str());
    flagNextSong = false; 
  }
  if (flagPrevSong){
    currentTrack = (currentTrack - 1 + trackCount) % trackCount;
    audio.connecttoSD(tracks[currentTrack].c_str());
    flagPrevSong = false; 
  }
}

void audio_eof_mp3(const char *info) {
  if (trackCount > 0 ) {
    currentTrack = (currentTrack + 1) % trackCount; // loop back to the first track after the last one
    audio.connecttoSD(tracks[currentTrack].c_str());  // start playing the next track
  }
}

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

const char* update_page = "<a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>"
                          "<form method='POST' action='/update' enctype='multipart/form-data'>"
                          "<input type='file' name='update'><input type='submit' value='Update'></form>";

void configOTAPage() {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", update_page);
    audio.pauseResume();  // Mute audio here before starting the update
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    int code = Update.hasError() ? 500 : 200;
    request->send(code, "text/html", Update.hasError() ? "FAIL" : createOkResponse("/",3));
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

void configGlobalWiFiPage (int ssid_addr, int pass_addr) {
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    String s = "<html><body>";
    s += "<a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>";
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
const char* playlist_page PROGMEM = R"rawliteral(
<a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>
<form id="upload_form" action="/upload" method="POST" enctype="multipart/form-data">
    <input type="file" id="file" name="upload">
    <input type="submit" value="Upload">
</form>
<div id="progress">Progress: 0%</div>
<script>
document.getElementById("upload_form").addEventListener("submit", function(event){
    event.preventDefault();
    var fileInput = document.getElementById("file");
    var file = fileInput.files[0];
    var formData = new FormData();
    formData.append("upload", file);
    
    var xhr = new XMLHttpRequest();
    xhr.open("POST", "/upload", true);
    
    xhr.upload.onprogress = function(event) {
        if(event.lengthComputable) {
            var progress = Math.round((event.loaded / event.total) * 100);
            document.getElementById("progress").innerHTML = "Progress: " + progress + "%";
        }
    };
    xhr.onload = function() {
        if (this.status == 200 && this.responseText == "refresh") {
            location.reload();  
        }
    };
    xhr.send(formData);
});
</script>
)rawliteral";


void listDir(File dir, String& fileList) {
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    if (isMusicFile(entry.name())) {
      fileList += " <a href=\"/alarmSong?file=";
      fileList += entry.name();
      fileList += "\">&#9200</a>";
      fileList += entry.name();
      fileList += " <a href=\"/delete?file=";
      fileList += entry.name();
      fileList += "\">Remove</a><br/>";
    }
    entry.close();
  }
}
void configPlaylistPage() {
  // Receive and save the file on POST request
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200); 
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(!index){
        audio.pauseResume();
        rootDirectory = SD.open("/" + filename, FILE_WRITE);
      }
      rootDirectory.write(data, len);
      if(final){
        if (isMusicFile(filename) && trackCount < MAX_TRACKS) {
          tracks[trackCount++] = filename;
          audio.pauseResume();
        }
        request->send(200, "text/plain", "refresh"); 
        rootDirectory.close();
      }
  });
  server.on("/playlist", HTTP_GET, [](AsyncWebServerRequest *request){
    String fileList = "<html><body>";
    File root = SD.open("/");
    listDir(root, fileList);
    String completePage = String(playlist_page) + "<br/>" + fileList + "</body></html>";
    request->send(200, "text/html", completePage);
  });
  // Remove file
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(request->hasParam("file")) {
      String filename = request->getParam("file")->value();
      if(SD.remove("/" + filename)) {
        request->send(200, "text/html", createOkResponse("/playlist",1));
        deleteTrack(tracks, trackCount, filename);
      } else {
        request->send(500, "text/plain", "Delete failed");
      }
    } else {
      request->send(400, "text/plain", "Bad request");
    }
  });
  server.on("/alarmSong", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(request->hasParam("file")) {
      String alarmNewSong = request->getParam("file")->value();
      if (alarmNewSong.length() <= MAX_SONGNAME_LENGTH){
        alarmSong = alarmNewSong;
        writeStringEEPROM(ALARM_SONG_ADDR, alarmSong);
        request->send(200, "text/html", createOkResponse("/playlist",1));
      } else {
        request->send(200, "text/plain", "Name of song more than " + String(MAX_SONGNAME_LENGTH) +" symbols! Make it shorter");
      }
    } else {
      request->send(400, "text/plain", "Bad request");
    }
  });
}

const char main_page_start[] PROGMEM= R"rawliteral(
<!DOCTYPE HTML><html>
<body>
<b>Now Playing: <span id="currentTrack"></span></b><br/><br/>
<button id="prevSongButton" type="button">PrevSong</button>
<button id="playStopButton" type="button">Play/Stop</button>
<button id="nextSongButton" type="button">NextSong</button> <br/><br/> 
)rawliteral";

const char main_page_end[] PROGMEM= R"rawliteral(
<b> Modes: </b>
<button id="modeAlarm" type="button" onclick="setTimeout(function(){ location.reload(); }, 500);">Alarm</button> 
<button id="modeDayNight" type="button" onclick="setTimeout(function(){ location.reload(); }, 500);">Day/Night</button> <br/>
<a href="/playlist">Playlist</a><br/>
<a href="/time">Time and Alarm</a><br/>
<a href="/wifi">Wi-Fi settings</a><br/>
<a href="/update">Update Firmware</a>
<script>
  function updateTrack() {
    fetch('/currentTrack')
      .then(response => response.text())
      .then(data => {
        document.getElementById('currentTrack').textContent = data;
      });
  }
  document.getElementById("prevSongButton").addEventListener("click", function(){
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/prev_song", true);
      xhr.send();
  });

  document.getElementById("playStopButton").addEventListener("click", function(){
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/play_stop", true);
      xhr.send();
  });

  document.getElementById("nextSongButton").addEventListener("click", function(){
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/next_song", true);
      xhr.send();
  });

  document.getElementById("volumeSlider1").addEventListener("input", function(){
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/set_volume1?volume=" + this.value, true);
      xhr.send();
      document.getElementById("volumeLabel1").innerHTML = "Volume: " + this.value;
  });
  document.getElementById("modeAlarm").addEventListener("click", function(){
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/modeAlarm", true);
      xhr.send();
  });
  document.getElementById("modeDayNight").addEventListener("click", function(){
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/modeDayNight", true);
      xhr.send();
  });
  updateTrack();
  setInterval(updateTrack, 2000);
</script>
</body>
</html>
)rawliteral";

void configMainPage() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String main_page = FPSTR(main_page_start);   // start the HTML
    main_page += R"rawliteral(<label id="volumeLabel1">Volume:)rawliteral" + String(currentVolume) + "</label> <br/> ";
    main_page += R"rawliteral(<input type="range" min="0" max="21" value=")rawliteral" + String(currentVolume) + R"rawliteral(" id="volumeSlider1"><br/>)rawliteral"; 
    if (flagModeAlarm) {
      main_page += "<p> Alarm is set: " + String(alarmHour) + ":" + fineStrMinute(alarmMinute) ;
      main_page += " volume: " + String(alarmVolume) + " Song: " + alarmSong + "</p>";
    }
    if (flagModeDayNight) {
      main_page += "<p> Day starts: " + String(startDayHour) + ":" + fineStrMinute(startDayMinute) + " with volume: " + String(dayVolume) + "</p>";
      main_page += "<p> Night starts: " + String(startNightHour) + ":" + fineStrMinute(startNightMinute) + " with volume: " + String(nightVolume) + "</p>";
    }
    main_page += FPSTR(main_page_end);           // finish the HTML
    request->send(200, "text/html", main_page);
  });
  server.on("/currentTrack", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", tracks[currentTrack]);
  });
    // Set the flag when the NextSong button is pressed
  server.on("/next_song", HTTP_GET, [](AsyncWebServerRequest *request){
    flagNextSong = true;
    request->send(200);
  });

    // Set the flag when the PrevSong button is pressed
  server.on("/prev_song", HTTP_GET, [](AsyncWebServerRequest *request){
    flagPrevSong = true;
    request->send(200);
  });

  // Toggle the state when the Play/Stop button is pressed
  server.on("/play_stop", HTTP_GET, [](AsyncWebServerRequest *request){
      audio.pauseResume();
      request->send(200);
  });

  // Adjust volume when the volume slider changes
  server.on("/set_volume1", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("volume")) {
          currentVolume = request->getParam("volume")->value().toInt();
          audio.setVolume(currentVolume);
          EEPROM.write(VOLUME_ADDR, currentVolume);
          EEPROM.commit();
          request->send(200);
      } else {
          request->send(400, "text/plain", "Bad request");
      }
  });
  server.on("/modeAlarm", HTTP_GET, [](AsyncWebServerRequest *request){
      flagModeAlarm = (flagModeAlarm) ? false : true; 
      EEPROM.write(ALARM_FLAG_ADDR,flagModeAlarm);
      EEPROM.commit();
  });
  server.on("/modeDayNight", HTTP_GET, [](AsyncWebServerRequest *request){
      flagModeDayNight = (flagModeDayNight) ? false : true; 
      EEPROM.write(DAY_NIGHT_FLAG_ADDR,flagModeDayNight);
      EEPROM.commit();
  });
}
const char* clock_page PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<body>
  <a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>
  <b>Current Time: <span id="currentTime"></span></b><br/>
  <b>Alarm Time: <span id="alarmTime"></span></b><br/>
  <b>Set Alarm</b>
  <form action="/setAlarm">
    <input type="text" name="alarmHour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="alarmMinute" placeholder="Minute" style="width: 40px;">
    <input type="text" name="alarmVolume" placeholder="Volume" style="width: 40px;">
    <input type="submit" value="Set Alarm">
  </form>

  <b>Set Time</b>
  <form action="/setTime">
    <input type="text" name="hour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="minute" placeholder="Minute" style="width: 40px;">
    <input type="text" name="second" placeholder="Second" style="width: 50px;">
    <input type="submit" value="Set Time">
  </form> 

  <b>Set Day start</b>
  <form action="/setStartDay">
    <input type="text" name="startDayHour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="startDayMinute" placeholder="Minute" style="width: 40px;">
    <input type="text" name="dayVolume" placeholder="Volume" style="width: 40px;">
    <input type="submit" value="Set">
  </form>

  <b>Set Night start</b>
  <form action="/setStartNight">
    <input type="text" name="startNightHour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="startNightMinute" placeholder="Minute" style="width: 40px;">
    <input type="text" name="nightVolume" placeholder="Volume" style="width: 40px;">
    <input type="submit" value="Set">
  </form>

  <script>
    function updateTime() {
      fetch('/getTime')
        .then(response => response.text())
        .then(data => {
          document.getElementById('currentTime').textContent = data;
        });
    }
    function updateAlarm() {
      fetch('/getAlarm')
        .then(response => response.text())
        .then(data => {
          document.getElementById('alarmTime').textContent = data;
        });
    }
    
    // Update the time immediately, and then every second
    updateTime();
    setInterval(updateTime, 60000);
    updateAlarm(); 
  </script>

</body>
</html>
)rawliteral"; 

void configClockPage(){
  // Serve the HTML
  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", clock_page);
  });
// Handle setting the time
server.on("/setTime", HTTP_GET, [] (AsyncWebServerRequest *request) {
  int hour, minute, second;
  int params = request->params();
  for(int i=0;i<params;i++){
    AsyncWebParameter* p = request->getParam(i);
    if(p->name()=="hour"){
      hour = p->value().toInt();
    }
    if(p->name()=="minute"){
      minute = p->value().toInt();
    }
    if(p->name()=="second"){
      second = p->value().toInt();
    }
  }
  DateTime newTime = DateTime(now.year(), now.month(), now.day(), hour, minute, second);
  rtc.adjust(newTime);
  request->send(200, "text/html", createOkResponse("/time",1));
});

  // Handle setting the alarm
  server.on("/setAlarm", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->name()=="alarmHour") {
        alarmHour = p->value().toInt();
        EEPROM.write(ALARM_HOUR_ADDR, alarmHour);
      }
      if(p->name()=="alarmMinute") {
        alarmMinute = p->value().toInt();
        EEPROM.write(ALARM_MIN_ADDR, alarmMinute);
      }
      if(p->name()=="alarmVolume") {
        alarmVolume = p->value().toInt();
        EEPROM.write(ALARM_VOLUME_ADDR, alarmVolume);
      }
    }
    flagModeAlarm = true;
    EEPROM.write(ALARM_FLAG_ADDR, flagModeAlarm);
    EEPROM.commit();
    request->send(200, "text/html", createOkResponse("/time",1));
  });

  server.on("/setStartDay", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->name()=="startDayHour") {
        startDayHour = p->value().toInt();
        EEPROM.write(START_DAY_HOUR_ADDR, startDayHour);
      }
      if(p->name()=="startDayMinute") {
        startDayMinute = p->value().toInt();
        EEPROM.write(START_DAY_MIN_ADDR, startDayMinute);
      }
      if(p->name()=="dayVolume") {
        dayVolume = p->value().toInt();
        EEPROM.write(DAY_VOLUME_ADDR, dayVolume);
      }
    }
    flagModeDayNight = true;
    EEPROM.write(DAY_NIGHT_FLAG_ADDR, flagModeDayNight);
    EEPROM.commit();
    request->send(200, "text/html", createOkResponse("/time",1));
  });

  server.on("/setStartNight", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->name()=="startNightHour") {
        startNightHour = p->value().toInt();
        EEPROM.write(START_NIGHT_HOUR_ADDR, startNightHour); 
      }
      if(p->name()=="startNightMinute") {
        startNightMinute = p->value().toInt();
        EEPROM.write(START_NIGHT_MIN_ADDR, startNightMinute);
      }
      if(p->name()=="nightVolume") {
        nightVolume = p->value().toInt();
        EEPROM.write(NIGHT_VOLUME_ADDR, nightVolume);
      }
    }

    flagModeDayNight = true;
    EEPROM.write(DAY_NIGHT_FLAG_ADDR, flagModeDayNight);
    EEPROM.commit();
    request->send(200, "text/html", createOkResponse("/time",1));
  });

  server.on("/getTime", HTTP_GET, [] (AsyncWebServerRequest *request) {
  DateTime now = rtc.now();
  String currentTime = String(now.hour()) + ":" + fineStrMinute(now.minute());
  request->send(200, "text/plain", currentTime);
  });
  server.on("/getAlarm", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String alarmTime = String(alarmHour) + ":" + fineStrMinute(alarmMinute);
    request->send(200, "text/plain", alarmTime);
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

void deleteTrack(String tracksList[], int &trackCount, const String &trackName) {
    int i = 0;
    while (i < trackCount) {
        if (tracksList[i] == trackName) {
            for (int j = i; j < trackCount - 1; j++) {
                tracksList[j] = tracksList[j + 1];
            }
            trackCount--;
        } else {
            i++;
        }
    }
}

String fineStrMinute(int minute){
  String fineMinute; 
  if (minute < 10){
    fineMinute += "0"; 
  }
  fineMinute += String(minute); 
  return fineMinute;
}
String createOkResponse(const String& url, int delaySec) {
  String delaySecStr = String(delaySec); 
    return "<html>"
           "<head>"
           "<meta http-equiv='refresh' content='"+ delaySecStr + ";url=" + url + "'>"
           "</head>"
           "<body>OK. Redirecting in " + delaySecStr + " seconds...</body>"
           "</html>";
}
int findIdSong(String tracksList[], int trackCount, const String& songName){
  for (int i = 0; i < trackCount; i++) {
    if (tracksList[i] == songName) {
      return i;
    }
  }
  return -1; 
}