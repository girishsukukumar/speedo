
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WiFiMulti.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "SPIFFS.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include "RemoteDebug.h"  //https://github.com/JoaoLopesF/RemoteDebug
#define USE_ARDUINO_INTERRUPTS true
#include <PulseSensorPlayground.h>
#include <NTPClient.h>
#include "SPIFFS.h"
 
#include <ESP8266FtpServer.h>




#define WEBSERVER_PORT 80
#define JSON_CONFIG_FILE_NAME "/config.json" 
#define SSID_NAME_LEN 20 
#define SSID_PASSWD_LEN 20
#define NAME_LEN 20


FtpServer ftpSrv; 
WebServer   webServer(WEBSERVER_PORT);
RemoteDebug Debug;
WiFiUDP     ntpUDP;
NTPClient   timeClient(ntpUDP);


typedef struct configData 
{
  char ssid1[SSID_NAME_LEN]   ;
  char password1[SSID_PASSWD_LEN]  ;
  char ssid2[SSID_NAME_LEN] ;
  char password2[SSID_PASSWD_LEN] ;
  char ssid3[SSID_NAME_LEN] ;
  char password3[SSID_PASSWD_LEN] ;
  float    wheelCirumference ;
  // String   servername ;
  // int      portNo;
  // String   apiKey;
  char    wifiDeviceName[NAME_LEN] ;  
  
};

struct configData ConfigData ;
WiFiMulti wifiMulti;          //  Create  an  instance  of  the ESP32WiFiMulti 
float gRPM = 0.0 ;
float gSpeed = 0.0 ;
float gTripDistance = 0.0 ;
float gLastRPMComputedTime = 0 ;
float gLastSpeedComputedTime = 0 ; 
int   gHeartRate = 0 ;


/*
 * Login page
 */
const char* loginIndex = 
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<td>Username:</td>"
        "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";
 
/*
 * Server Index Page
 */
 
const char* serverIndex = "<HTML>" 
"<H1> Cyclo Computer Config Page </H1>"
"<TABLE>"
"<TR>"
"<TD> <form  action=\"/showRecord\" method=\"POST\"> <button type=\"submit\">Show files</button></form></TD>"
"</TR>"
"</TABLE>"
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')" 
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>"
 "</HTML>" ;

const int PULSE_INPUT = 34;
const byte CADENCE_PIN= 18 ;
const byte SPEED_PIN = 19 ;
const int THRESHOLD = 550;   // Adjust this number to avoid noise when idle

volatile byte  cadenceTicks = 0;
volatile byte  speedTicks = 0 ;
TaskHandle_t   ComputeValuesTask;
TaskHandle_t   DisplayValuesTask ;
TaskHandle_t   MeasureHeartRateTask ;

char   recordFileName[NAME_LEN];

PulseSensorPlayground pulseSensor;
// Software SPI (slower updates, more flexible pin options):
// GPIO14 - Serial clock out (SCLK)
// GPIO13 - Serial data out (DIN)
// GPIO27 - Data/Command select (D/C)
// GPIO15 - LCD chip select (CS)
// GPIO26 - LCD reset (RST)
//Adafruit_PCD8544 display = Adafruit_PCD8544(7, 6, 5, 4, 3);

// How connection are made to Display in ESP32
//         *DISPLAY PINS                    SCLK   DIN    DC   CS     RST
//                                            |      |    |     |      | 
//                                            V      V    V     V      V
Adafruit_PCD8544 display = Adafruit_PCD8544( 14,    13,   27,  15,    26);

void setupWifi()
{

  wifiMulti.addAP(ConfigData.ssid1, ConfigData.password1);   
  wifiMulti.addAP(ConfigData.ssid2, ConfigData.password2);    
  wifiMulti.addAP(ConfigData.ssid3, ConfigData.password3);    

  while  (wifiMulti.run()  !=  WL_CONNECTED) 
  { 
    //  Wait  for the Wi-Fi to  connect:  scan  for Wi-Fi networks, and connect to  the strongest of  the networks  above       
    delay(1000);        
    Serial.print('*');    
  }   
  delay(5000);
  WiFi.setHostname(ConfigData.wifiDeviceName);
  Serial.printf("\n");   
  Serial.printf("Connected to  ");   
  Serial.println(WiFi.SSID());         
  Serial.printf("IP  address: ");   
  Serial.println(WiFi.localIP()); 
  WiFi.softAPdisconnect (true);   //Disable the Access point mode.

}
void showRecords()
{
    char fileList[200];
        Serial.printf("showRecords");

    File root = SPIFFS.open("/");
 
  File file = root.openNextFile();

  sprintf(fileList, "<HTML> <H1> List of Records <\H1> <OL>");
  while(file)
  {
      char fileName[30] ;
      sprintf(fileName,"<LI>  %s </LI>", fileName);
      strcat(fileList,fileName);
      file = root.openNextFile();
  }
  strcat(fileList,"<\OL> <\HTML>");
      webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/html", fileList);
    Serial.printf("%s \n", fileList);
    

}
void DisplayserverIndex()
{
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/html", serverIndex);

}
void DisplayLoginIndex()
{
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/html", loginIndex);

}
void setupWebHandler()
{
   /*return index page which is stored in serverIndex */

  webServer.on("/showRecord", HTTP_POST, showRecords);
  webServer.on("/", HTTP_GET, DisplayLoginIndex);
  webServer.on("/serverIndex", HTTP_GET, DisplayserverIndex);
  /*handling uploading firmware file */
  webServer.on("/update", HTTP_POST, []() {
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  webServer.begin();
}
void ICACHE_RAM_ATTR cadencePinHandler()
{
  cadenceTicks++ ;
}

void ICACHE_RAM_ATTR speedPinHandler()
{
  speedTicks++ ;
}
void ReadConfigValuesFromSPIFFS()
{
  File jsonFile ;
  const size_t capacity = JSON_OBJECT_SIZE(8) + 240;
  DynamicJsonDocument doc(capacity);

  //const char* json = "{\"ssid1\":\"xxxxxxxxxxxxxxxxxxxx\",\"password1\":\"xxxxxxxxxxxxxxxxxxxx\",\"ssid2\":\"xxxxxxxxxxxxxxxxxxxx\",\"password2\":\"xxxxxxxxxxxxxxxxxxxx\",\"ssid3\":\"xxxxxxxxxxxxxxxxxxxx\",\"password3\":\"xxxxxxxxxxxxxxx\",\"wheelDiameter\":85.99,\"devicename\":\"xxxxxxxxxxxxxx\"}";
  
  jsonFile = SPIFFS.open(JSON_CONFIG_FILE_NAME, FILE_READ);
  
  if (jsonFile == NULL)
  {
     Serial.printf("Unable to open %s",JSON_CONFIG_FILE_NAME);
     return ;
  }
  
  deserializeJson(doc, jsonFile);

  const char* ssid1 = doc["ssid1"]; // "xxxxxxxxxxxxxxxxxxxx"
  const char* password1 = doc["password1"]; // "xxxxxxxxxxxxxxxxxxxx"
  const char* ssid2 = doc["ssid2"]; // "xxxxxxxxxxxxxxxxxxxx"
  const char* password2 = doc["password2"]; // "xxxxxxxxxxxxxxxxxxxx"
  const char* ssid3 = doc["ssid3"]; // "xxxxxxxxxxxxxxxxxxxx"
  const char* password3 = doc["password3"]; // "xxxxxxxxxxxxxxx"
  float wheelDiameter = doc["wheelDiameter"]; // 85.99
  const char* devicename = doc["devicename"]; // "xxxxxxxxxxxxxx"
  jsonFile.close();
  
  strcpy(ConfigData.ssid1,ssid1);
  strcpy(ConfigData.password1,password1);
  
  strcpy(ConfigData.ssid2,ssid2);
  strcpy(ConfigData.password2,password2);

  strcpy(ConfigData.ssid3,ssid3);
  strcpy(ConfigData.password3,password3);

  ConfigData.wheelCirumference = (3.14 * wheelDiameter)/100 ;// in meters
  strcpy(ConfigData.wifiDeviceName,devicename);

}
void DisplayConfigValues()
{
   Serial.printf("ssid1 %s \n",ConfigData.ssid1);
   Serial.printf("Password %s \n", ConfigData.password1);

   Serial.printf("ssid2 %s \n",ConfigData.ssid2);
   Serial.printf("Password2 %s \n", ConfigData.password2);

   Serial.printf("ssid3 %s \n",ConfigData.ssid3);
   Serial.printf("Password3 %s \n", ConfigData.password3);

   Serial.printf("Wheel Circumference = %f\n", ConfigData.wheelCirumference);
   Serial.printf("Device name = %s ", ConfigData.wifiDeviceName);
}
void DisplayValues( void * pvParameters )
{
  boolean flag ;
  float distance ;
  File  recordFile ;
  flag = true ;
  

  while(flag)
  {
    distance = gTripDistance /1000 ; //Convert into KM
    recordFile =  SPIFFS.open(recordFileName, FILE_APPEND);

    display.display();
    display.clearDisplay();

    display.setTextSize(2);
    display.setTextColor(BLACK);
    display.setCursor(0,0);
  
    display.printf("C:%0.1f\n",gRPM);
    display.printf("S:%0.1f\n",gSpeed);
    display.printf("D:%0.1f\n",distance);
    recordFile.printf("%0.1f,%0.1f,%0.1f\n",
                       gRPM,gSpeed,distance);
    recordFile.close();
    delay(3000);
 
  }
}
void MeasureHeartRate( void * pvParameters )
{
  boolean flag ;
  float distance ;
  boolean  pulseSensorReady= true ;
  flag = true ;

  pulseSensor.analogInput(PULSE_INPUT);
  pulseSensor.setThreshold(THRESHOLD);
  if (!pulseSensor.begin()) 
  {
     pulseSensorReady = false ;
  }


  while(flag)
  {
     if (pulseSensorReady == false)
     {
         gHeartRate  = -1 ;
     }
     else
     {
        delay(20);
        gHeartRate = pulseSensor.getBeatsPerMinute();
     }
     delay(2) ; 
  }
}
void ComputeValues( void * pvParameters )
{
  int currentTime  ;
  int diff ;
  Serial.printf("Core ID where Computer Values running is : ");
  Serial.println(xPortGetCoreID());

  while(1)
  {
     timeClient.update(); // Keep the device time up to date
     currentTime = millis(); 
     debugV("cadenceTicks = %u", cadenceTicks);
     debugV("speedTicks = %u", speedTicks);

     diff = currentTime -gLastRPMComputedTime ;
     if (diff > 3000)
     {
       float timeSlots = 60000/diff ;
       debugV("FOR COMPUTE cadenceTicks = %u", cadenceTicks);
       gRPM =  cadenceTicks * timeSlots ;
       cadenceTicks = 0 ;
       gLastRPMComputedTime = currentTime ;
     }
     
     diff = currentTime - gLastSpeedComputedTime ;
     if (diff > 5000)
     {
       float distanceTravelled ;
       float timeFrame ; 
       distanceTravelled = speedTicks * ConfigData.wheelCirumference ;
       timeFrame =  (diff /1000) ; // Convert timeFrame into seconds 
       gSpeed  = distanceTravelled/timeFrame ;  // Speed  in meters per second      
       gSpeed  = gSpeed * 3.6 ; // convert m/sec into Km/hr 
       speedTicks =  0 ;
       gTripDistance =gTripDistance + distanceTravelled ; // In Meters
       gLastSpeedComputedTime = currentTime ;

     }
     
     delay(1000) ;
  }
}

void SetupDisplay()
{
   Serial.printf("Setup Display");
   display.begin();
   //display.setContrast(60);

   display.display(); // show splashscreen
   delay(1000);
   display.clearDisplay();   // clears the screen and buffer   
}
void setup() 
{
  String formattedDate ;
  String currentTime;
  int    splitT, splitDash;
  String dayStamp;
  String month ;
  int    len ;
   char deviceDate[20];
   char deviceTime[20];
   char *monthArray[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul","Aug", "Sep", "Oct", "Nov", "Dec"};
   int monthArrayIdx ;

  int GMTOffset = 19800;

  SetupDisplay();
  Serial.begin(115200);
  Serial.printf("Speedo");

  pinMode(CADENCE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CADENCE_PIN), cadencePinHandler, FALLING);

  pinMode(SPEED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SPEED_PIN), speedPinHandler, FALLING);
  display.display();
  display.clearDisplay();

  display.display();
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(BLACK);
  
  display.setCursor(0,0);
  display.printf("Sensors:Ready \n");


  SPIFFS.begin(true) ;
  
  //SPIFFS.format();
  
  ReadConfigValuesFromSPIFFS();
  display.printf("Files:Ready\n");

  DisplayConfigValues();
  Serial.printf("Configuratio file reading : Success \n");
  setupWifi();
  Serial.printf("WifiSetup : Success \n");
  display.printf("WiFi:Ready\n");

  timeClient.begin();
  timeClient.setTimeOffset(GMTOffset); /* GMT + 5:30 hours */

  delay(2000);
  timeClient.update(); // Keep the device time up to date
  delay(5000);

  formattedDate = timeClient.getFormattedDate(); 
  currentTime = timeClient.getFormattedTime(); 
  Serial.printf("Current Date = %s \n", formattedDate);
  Serial.printf("Current time = %s \n", currentTime);
  currentTime.replace(':','_');
  
  splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  Serial.println(dayStamp);

  dayStamp.remove(0,5);
  splitDash = dayStamp.indexOf("-");
  month = dayStamp.substring(0,splitDash);
  monthArrayIdx = month.toInt();
  Serial.println(dayStamp);
  Serial.println(month);
  Serial.printf("Month idx = %d \n",monthArrayIdx);
  dayStamp.remove(0,3);
  
  len = dayStamp.length();
  dayStamp.toCharArray(deviceDate,len+1); // We got day of month
  
  len = currentTime.length();
  currentTime.toCharArray(deviceTime,len+1);
  sprintf(recordFileName,"/%s%s%s.csv",deviceDate,monthArray[monthArrayIdx-1],deviceTime);
  Serial.printf("File name = %s \n", recordFileName);
  
  setupWebHandler();
  display.printf("Web Server:Ready\n");
  Debug.begin(ConfigData.wifiDeviceName); // Initialize the WiFi server
  Debug.setResetCmdEnabled(true); // Enable the reset command
  Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true); // Colors
  ftpSrv.begin("esp8266","esp8266");
  Serial.printf("Web Server configuration: Success \n");
  delay(2000);
  display.clearDisplay();
  
  xTaskCreatePinnedToCore(
                    ComputeValues,   /* Task function. */
                    "Task1",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &ComputeValuesTask,      /* Task handle to keep track of created task */
                    1);          /* pin task to core 0 */      

  xTaskCreatePinnedToCore(
                    DisplayValues,   /* Task function. */
                    "Task2",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &DisplayValuesTask,      /* Task handle to keep track of created task */
                    1);          /* pin task to core 0 */      


#if 0
  xTask CreatePinnedToCore(
                    MeasureHeartRate,   /* Task function. */
                    "Task3",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &MeasureHeartRateTask,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */      
#endif
}

void loop() 
{
  webServer.handleClient();
  ftpSrv.handleFTP();   
  Debug.handle();
  yield();



  // put your main code here, to run repeatedly:

}
