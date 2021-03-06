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
#include <NTPClient.h>
#include "SPIFFS.h"
 
#include <ESP8266FtpServer.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#define DHTPIN 25    // Digital pin connected to the DHT sensor 
#define DHTTYPE    DHT11     // DHT 11

#include "MAX30105.h"

#include "heartRate.h"
#include "spo2_algorithm.h"
#define   ENABLE_PRINTF 1 

#ifdef ENABLE_PRINTF 
    #define    DEBUG_PRINTF(f,...)   Serial.printf(f,##__VA_ARGS__)
#else
    #define    DEBUG_PRINTF(f,...)
#endif


#define WEBSERVER_PORT 80
#define JSON_CONFIG_FILE_NAME "/config.json" 
#define SSID_NAME_LEN 20 
#define SSID_PASSWD_LEN 20
#define NAME_LEN 20
#define FTP_USER_NAME "apollo11"
#define FTP_PASSWORD  "eagle"
#define CORE_ONE 1
#define CORE_ZERO 0 

FtpServer     ftpSrv; 
WebServer     webServer(WEBSERVER_PORT);
RemoteDebug   Debug;
WiFiUDP       ntpUDP;
NTPClient     timeClient(ntpUDP);
MAX30105      maxSensor;

const byte    RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte          rates[RATE_SIZE]; //Array of heart rates
byte          rateSpot = 0;
long          lastBeat = 0; //Time at which the last beat occurred
float         beatsPerMinute;
int           beatAvg;
uint32_t      irBuffer[100]; //infrared LED sensor data
uint32_t      redBuffer[100];  //red LED sensor data
int32_t       spo2; //SPO2 value
int8_t        validSPO2; //indicator to show if the SPO2 calculation is valid
int8_t        validHeartRate; //indicator to show if the heart rate calculation is valid
int8_t        idx =0;


typedef struct configData 
{
  char     ssid1[SSID_NAME_LEN]   ;
  char     password1[SSID_PASSWD_LEN]  ;
  char     ssid2[SSID_NAME_LEN] ;
  char     password2[SSID_PASSWD_LEN] ;
  char     ssid3[SSID_NAME_LEN] ;
  char     password3[SSID_PASSWD_LEN] ;
  float    wheelCirumference ;
  char     wifiDeviceName[NAME_LEN] ;  
  
};

struct configData ConfigData ;
WiFiMulti         wifiMulti;          //  Create  an  instance  of  the ESP32WiFiMulti 
float             gRPM = 0.0 ;
float             gSpeed = 0.0 ;
float             gTripDistance = 0.0 ;
float             gLastRPMComputedTime = 0 ;
float             gLastSpeedComputedTime = 0 ; 
int               gHeartRate = 0 ;
float             bodyTempInCelius=0.0 ;
float             gRoomTemp ;
float             gRoomHumidity ;

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

const int      PULSE_INPUT = 34  ;
const byte     CADENCE_PIN = 18  ;
const byte     SPEED_PIN   = 19  ;
const int      THRESHOLD   = 550 ;   // Adjust this number to avoid noise when idle

volatile byte  prevCadenceTicks = 0;

volatile byte  cadenceTicks = 0;
volatile byte  speedTicks   = 0 ;
TaskHandle_t   ComputeValuesTask;
TaskHandle_t   DisplayValuesTask ;
TaskHandle_t   MeasureHeartRateTask ;
TaskHandle_t   MeasureTempHumidityTask ;

char            recordFileName[NAME_LEN];

// Software SPI (slower updates, more flexible pin options):
// GPIO14 - Serial clock out (SCLK)
// GPIO27 - Data/Command select (D/C)
// GPIO15 - LCD chip select (CS)
// GPIO26 - LCD reset (RST)
//Adafruit_PCD8544 display = Adafruit_PCD8544(7, 6, 5, 4, 3);


// How connection are made to Display in ESP32
//         *DISPLAY PINS                    SCLK   DIN    DC   CS     RST
//                                            |      |    |     |      | 
//                                            V      V    V     V      V
Adafruit_PCD8544 display = Adafruit_PCD8544( 14,    13,   27,  15,    26);

bool max30102Setup()
{
  // Initialize sensor
  //Use default I2C port, 400kHz speed
  if (!maxSensor.begin(Wire, I2C_SPEED_STANDARD,MAX30105_ADDRESS)) 
  {
     DEBUG_PRINTF("MAX30105 was not found. Please check wiring/power. ");
     return false ;
  }

  maxSensor.setup(); //Configure sensor with default settings
  maxSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
  maxSensor.setPulseAmplitudeGreen(0); //Turn off Green LED
  maxSensor.enableDIETEMPRDY();
  return true ;
}
bool setupWifi()
{
  int count ;
  wifiMulti.addAP(ConfigData.ssid1, ConfigData.password1);   
  wifiMulti.addAP(ConfigData.ssid2, ConfigData.password2);    
  wifiMulti.addAP(ConfigData.ssid3, ConfigData.password3);    
  count  = 0 ;
  while  (wifiMulti.run()  !=  WL_CONNECTED) 
  { 
    //  Wait  for the Wi-Fi to  connect:  scan  for Wi-Fi networks, and connect to  the strongest of  the networks  above       
    delay(1000);        
    DEBUG_PRINTF("*");    
    count++ ;
    if (count > 40)
    {
       return false ;  
    }
  }   
  delay(5000);
  WiFi.setHostname(ConfigData.wifiDeviceName);
  DEBUG_PRINTF("\n");   
  DEBUG_PRINTF("Connected to  ");   
  DEBUG_PRINTF("%s\n",WiFi.SSID().c_str());         
  DEBUG_PRINTF("IP  address: ");   
  DEBUG_PRINTF("%s",WiFi.localIP().toString()); 
  WiFi.softAPdisconnect (true);   //Disable the Access point mode.
  return true ;
}
void showRecords()
{
  char fileList[200];
  DEBUG_PRINTF("showRecords");
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
  DEBUG_PRINTF("%s \n", fileList);
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
      DEBUG_PRINTF("Update: %s\n", upload.filename.c_str());
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
        DEBUG_PRINTF("Update Success: %u\nRebooting...\n", upload.totalSize);
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
     DEBUG_PRINTF("Unable to open %s",JSON_CONFIG_FILE_NAME);
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
   DEBUG_PRINTF("ssid1 %s \n",ConfigData.ssid1);
   DEBUG_PRINTF("Password %s \n", ConfigData.password1);

   DEBUG_PRINTF("ssid2 %s \n",ConfigData.ssid2);
   DEBUG_PRINTF("Password2 %s \n", ConfigData.password2);

   DEBUG_PRINTF("ssid3 %s \n",ConfigData.ssid3);
   DEBUG_PRINTF("Password3 %s \n", ConfigData.password3);

   DEBUG_PRINTF("Wheel Circumference = %f\n", ConfigData.wheelCirumference);
   DEBUG_PRINTF("Device name = %s ", ConfigData.wifiDeviceName);
}
void DisplayRPM()
{
    byte rpm ;
    display.setTextSize(1);
    display.printf("RPM:");
    rpm = (byte) gRPM ; // Display RPM as integer and not as float
    display.setTextSize(2);
    display.printf("%d\n",rpm);
 
}
void DisplayPower(float p)
{
    display.setTextSize(1);
    display.printf("Pwr:");
    display.setTextSize(2);
    display.printf("%0.1f\n",p);
}
void DisplaySpeed()
{
    byte Speed ;
    
    Speed = round(gSpeed);
    display.setTextSize(1);
    display.printf("KM/H:");
    display.setTextSize(2);
    display.printf("%d\n",Speed);
}

void DisplayRoomTemp()
{
    display.setTextSize(1);
    display.printf("T:");
    display.setTextSize(2);
    display.printf("%0.1f\n",gRoomTemp);
}
void DisplayRoomHumidity()
{
    display.setTextSize(1);
    display.printf("H:");
    display.setTextSize(2);
    display.printf("%0.1f\n",gRoomHumidity);
}
void DisplayDistance()
{

 
}

void DisplayHeartBeat()
{
    display.setTextSize(1);
    display.printf("HB:");
    display.setTextSize(2);
    display.printf("%d\n",gHeartRate);
}
void DisplayBodyTemp()
{
  
    display.setTextSize(1);
    display.printf("BT:");    
    display.setTextSize(2);
    display.printf("%0.1fC\n",bodyTempInCelius);
    delay(2000);
}
void ClearDisplay()
{
    display.display();
    display.clearDisplay();
    display.setTextColor(BLACK);
    display.setCursor(0,0);
}
void DisplayValues( void * pvParameters )
{
  boolean flag ;
  File    recordFile ;
  float   distance ;
  String  currentTime ;
  int     len ; 
  char    deviceTime[20] ;
  float   Power ;


  flag = true ;


  while(flag)
  {
    distance = gTripDistance /1000 ; //Convert into KM
    currentTime = timeClient.getFormattedTime(); 
    len = currentTime.length();
    currentTime.toCharArray(deviceTime,len+1);
    Power = (5.244820 * gSpeed) + (0.01968 * gSpeed * gSpeed * gSpeed) ;

    ClearDisplay();
    DisplayRPM();
    DisplaySpeed();
    display.setTextSize(1);
    display.printf("KM:");    
    display.setTextSize(2);
    display.printf("%0.1f\n",distance);
    delay(2000);
#if 0
    ClearDisplay();
    DisplayPower(Power);
    DisplayHeartBeat();
    DisplayRoomTemp();
    DisplayRoomHumidity();
#endif 
#if 0
    ClearDisplay();
    DisplayHeartBeat() ;
    DisplayBodyTemp();
#endif

    if (gSpeed > 0)
    {
       // Do not record while cycle is stopped.
       
       
       recordFile =  SPIFFS.open(recordFileName, FILE_APPEND);

       recordFile.printf("%s, %0.1f,%0.1f,%0.1f,%0.1f,%0.1f,%0.1f\n",
                       deviceTime,gRPM,gSpeed,distance,Power,gRoomTemp,gRoomHumidity);
       recordFile.close();
    }    
    delay(3000);
 
  }
}

void MeasureHeartRate( void * pvParameters )
{
  while(1)
  {  
    long irValue = maxSensor.getIR();
    idx++ ;
    
    bodyTempInCelius = maxSensor.readTemperature(); 

    if (checkForBeat(irValue) == true)
    {
      // We sensed a beat!
      long delta = millis() - lastBeat;
      //DEBUG_PRINTF("Delta = %d \n", delta);
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20)
      {
        rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
        rateSpot %= RATE_SIZE; //Wrap variable

        //Take average of readings
        beatAvg = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++)
        {
          beatAvg += rates[x];
        }
        beatAvg /= RATE_SIZE;
      }
    }

    if (irValue > 50000)
    {
      
      if (idx == 100)
      {
         DEBUG_PRINTF("Avg BPM:%d\n",beatAvg);
         Debug.printf("Avg BPM:%d\n",beatAvg);
         gHeartRate = beatAvg ;
         idx = 0 ;
      }
      
    }
    else
    {
      DEBUG_PRINTF(" No finger?\n");
      Debug.printf(" No finger?\n");
    }
  }
}

void MeasureTempHumidity(void *params)
{
    DHT_Unified   dht(DHTPIN, DHTTYPE);

    sensors_event_t event;
    dht.begin();

    while(1)
    {
       dht.temperature().getEvent(&event);
       DEBUG_PRINTF("Temp = %f \n",event.temperature);
       if (isnan(event.temperature) == false) 
       {
         gRoomTemp = event.temperature ;
       }
       else
          DEBUG_PRINTF("Error in reading temp \n");
       
       dht.humidity().getEvent(&event);
       DEBUG_PRINTF("Temp = %f \n",event.relative_humidity);

       if (isnan(event.relative_humidity) == false) 
       {
         gRoomHumidity = event.relative_humidity ;
       }
       else
          DEBUG_PRINTF("Error in reading humudity \n");
       delay(60000);
    }
}
void ComputeValues( void * pvParameters )
{
  int currentTime  ;
  int diff ;
  DEBUG_PRINTF("Core ID where Computer Values running is : %d",xPortGetCoreID());
 

  while(1)
  {
     timeClient.update(); // Keep the device time up to date
     currentTime = millis(); 
     debugV("cadenceTicks = %u", cadenceTicks);
     debugV("speedTicks = %u", speedTicks);

     diff = currentTime -gLastRPMComputedTime ;
     if (diff > 60000)
     {
#if 0      
       float timeSlots = 60000/diff ;
       debugV("FOR COMPUTE cadenceTicks = %u", cadenceTicks);
       gRPM =  cadenceTicks * timeSlots ;
#endif
       gRPM = cadenceTicks ; // True RPM, we will wait for one minute to get this
       debugV("FOR COMPUTE cadenceTicks = %u", cadenceTicks);
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
   DEBUG_PRINTF("Setup Display");
   display.begin();
   //display.setContrast(60);

   display.display(); // show splashscreen
   delay(1000);
   display.clearDisplay();   // clears the screen and buffer   
}
void ConfigureAsAccessPoint()
{
  
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
  DEBUG_PRINTF("Speedo");

  pinMode(CADENCE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CADENCE_PIN), cadencePinHandler, FALLING);

  pinMode(SPEED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SPEED_PIN), speedPinHandler, FALLING);
  delay(2000);

  ClearDisplay();
  display.printf("Sensors:Ready \n");


  SPIFFS.begin(true) ;
  
  //SPIFFS.format();
      
  ReadConfigValuesFromSPIFFS();
  display.printf("Files:Ready\n");

  DisplayConfigValues();
  DEBUG_PRINTF("Configuratio file reading : Success \n");
  if (setupWifi() == false)
  {
      DEBUG_PRINTF("WifiSetup: failed \n");
      ConfigureAsAccessPoint(); 
  }
  else
  {
     DEBUG_PRINTF("WiFiSetup:Success\n");
  }

  timeClient.begin();
  timeClient.setTimeOffset(GMTOffset); /* GMT + 5:30 hours */

  delay(2000);
  timeClient.update(); // Keep the device time up to date
  delay(5000);
  debugV("FOR COMPUTE cadenceTicks = %u", cadenceTicks);

  formattedDate = timeClient.getFormattedDate(); 
  currentTime = timeClient.getFormattedTime(); 
  DEBUG_PRINTF("Current Date = %s \n", formattedDate);
  DEBUG_PRINTF("Current time = %s \n", currentTime);
  currentTime.replace(':','_');
  
  splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  //Serial.println(dayStamp);

  dayStamp.remove(0,5);
  splitDash = dayStamp.indexOf("-");
  month = dayStamp.substring(0,splitDash);
  monthArrayIdx = month.toInt();
  //Serial.println(dayStamp);
 // Serial.println(month);
  DEBUG_PRINTF("Month idx = %d \n",monthArrayIdx);
  dayStamp.remove(0,3);
  
  len = dayStamp.length();
  dayStamp.toCharArray(deviceDate,len+1); // We got day of month
  
  len = currentTime.length();
  currentTime.toCharArray(deviceTime,len+1);
  sprintf(recordFileName,"/%s%s%s.csv",deviceDate,monthArray[monthArrayIdx-1],deviceTime);
  DEBUG_PRINTF("File name = %s \n", recordFileName);
  
  setupWebHandler();
  display.printf("Web Server:Ready\n");
  Debug.begin(ConfigData.wifiDeviceName); // Initialize the WiFi server
  Debug.setResetCmdEnabled(true); // Enable the reset command
  Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true); // Colors
  ftpSrv.begin(FTP_USER_NAME,FTP_PASSWORD);
  DEBUG_PRINTF("WeSuccessb Server configuration: Success \n");
  delay(2000);
  display.clearDisplay();
  xTaskCreatePinnedToCore(
                    ComputeValues,   /* Task function. */
                    "Task1",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &ComputeValuesTask,      /* Task handle to keep track of created task */
                    CORE_ONE);          /* pin task to core 1 */      

  xTaskCreatePinnedToCore(
                    DisplayValues,   /* Task function. */
                    "Task2",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &DisplayValuesTask,      /* Task handle to keep track of created task */
                    CORE_ONE);          /* pin task to core 1 */      

  xTaskCreatePinnedToCore(
                    MeasureTempHumidity,   /* Task function. */
                    "Task3",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &MeasureTempHumidityTask,      /* Task handle to keep track of created task */
                    CORE_ZERO);          /* pin task to core 0 */      

  if (max30102Setup() == true)
  {
      xTaskCreatePinnedToCore(
                      MeasureHeartRate,   /* Task function. */
                     "Task4",     /* name of task. */
                     10000,       /* Stack size of task */
                     NULL,        /* parameter of the task */
                     1,           /* priority of the task */
                     &MeasureHeartRateTask,      /* Task handle to keep track of created task */
                     CORE_ZERO);          /* pin task to core 0 */      
  }
  else
  {
    DEBUG_PRINTF("MAX 300102 not found \n");
  }

}

void loop() 
{
  webServer.handleClient();
  
  ftpSrv.handleFTP();   
  
  if (prevCadenceTicks != cadenceTicks )
  {
    DEBUG_PRINTF("prev-%d current-%d s-%d\n",prevCadenceTicks, cadenceTicks,speedTicks);
    prevCadenceTicks = cadenceTicks ;
  }
  
  Debug.handle();
  yield();
  

  // put your main code here, to run repeatedly:

}
