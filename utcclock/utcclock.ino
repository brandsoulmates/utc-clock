#include <ESP8266WiFi.h>
#include "Adafruit_HT1632.h"
#include <TimeLib.h>
#include <Timezone.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#define HT_DATA 2 //Data on Pin 2 of ESP8266ThingDev
#define HT_WR 14 //Write on Pin 14 of ESP8266ThingDev
#define HT_CS 16 //Matrix 1 on Pin 16 of ESP8266ThingDev
#define HT_CS2 13 //Matrix 2 on Pin 13 of ESP8266ThingDev
#define HT_CS3 12 //Matrix 3 on Pin 12 of ESP8266ThingDev
extern "C" {
#include "user_interface.h"
}

//enter your WIFI credentials here
const char* ssid = "Your SSID";
const char* password = "Your WIFI Password";
ESP8266WebServer server(80);

//JSON unix time API
const char* host = "www.convert-unix-time.com";
String data = "";   // String with json data
boolean showingMessage = false; //don't display clock if displaying message
boolean firstSync = true; //don't show syncing message after first time

WiFiClient client;
unsigned long lastConnectionTime = 0;            // last time you connected to the server, in milliseconds
//sync every 12 hrs
const unsigned long postingInterval = 12L * 60L * 60L * 1000L; // delay between updates, in milliseconds

//adafruit 16x24-led-matrix
Adafruit_HT1632LEDMatrix matrix = Adafruit_HT1632LEDMatrix(HT_DATA, HT_WR, HT_CS, HT_CS2, HT_CS3);

//timezone for Pacific Standard Time and Pacific Savings Time
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};  //UTC - 7 hours
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};   //UTC - 8 hours
Timezone usPacific(usPDT, usPST);

//int to keep track of pacific time in MS
time_t pacificTime;

//not needed, for reference, 24 * 3
const int matrixWidth = 72;

void setup() {
  pinMode(HT_DATA, OUTPUT);
  pinMode(HT_WR, OUTPUT);
  pinMode(HT_CS, OUTPUT);
  pinMode(HT_CS2, OUTPUT);
  pinMode(HT_CS3, OUTPUT);
  Serial.begin(115200);
  matrix.begin(ADA_HT1632_COMMON_16NMOS);
  
  matrix.setBrightness(1); //1 to 15, default 1, have API to idecrease brightness
  matrix.setTextSize(1);    // size 1 == 8 pixels high
  matrix.setTextColor(1);   // 'lit' LEDs
  matrix.setCursor(0, 0);

  Serial.println("Connecting to ");
  Serial.println(ssid);
  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    matrix.clearScreen();
    matrix.setCursor(0, 0);
    matrix.print("Connecting WIFI");
    matrix.writeScreen();
  }
  matrix.clearScreen();
  matrix.setCursor(0, 0);
  matrix.print("Connected");
  matrix.writeScreen();
  MDNS.begin("utcclock");
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
  server.begin();

  //say "Hello" on root web requests
  server.on("/", handle_root);

  //provide simple form for sending messages
  server.on("/sendmessage", [](){
    server.send(200, "text/html", "<html><head><title>UTC Clock: Send Message</title><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body><form action='/' method='POST'>Message:<br/><input type='text' name='message' /><br/><input type='submit' name='submit' /></form></body></html>");
  });

  //debug: reset API
  server.on("/reset", [](){
        matrix.setCursor(0, 0);
        matrix.setTextSize(1);
        matrix.fillScreen();
        matrix.writeScreen();
        delay(200);
        matrix.clearScreen();
        matrix.writeScreen();
        delay(200);
        server.send(200, "application/json", "{\"message\":\"OK\"}");
    
  });

  //debug: light all LEDs for 5 seconds
  server.on("/fullburn", [](){
        matrix.fillScreen();
        matrix.writeScreen();
        delay(5000);
        matrix.clearScreen();
        matrix.writeScreen();
        delay(200);
        server.send(200, "application/json", "{\"message\":\"OK\"}");
  });

  //debug: return how much free memory there is
  server.on("/memory", [](){
    uint32_t free = system_get_free_heap_size();
    server.send(200, "application/json", "{\"message\":\"Free Memory: " + (String) free + "\"}");
  });

  //inital sync, show syncing message
  syncClock();
  }


void loop() {
  // if 12 hrs have passed since the last connection, then connect again and sync:
  if (millis() - lastConnectionTime > postingInterval) {
    syncClock();
  }
  
  //listen for clients
  server.handleClient();

  //update PST time
  pacificTime = usPacific.toLocal(now());
  showTime();
  
}

void showTime() {
  //update the clock display if time is synced and not displaying a message
  if(timeStatus() == timeSet && showingMessage == false)
  {
    matrix.clearScreen();
    matrix.setCursor(0, 0);
    if(hour(pacificTime) < 10)
      matrix.print('0');
    matrix.print(hour(pacificTime));
    matrix.setCursor(10, 0);
    matrix.print(":");
    matrix.setCursor(14, 0);
    if (minute(pacificTime) < 10)
      matrix.print('0');
    matrix.print(minute(pacificTime));
    if(month(pacificTime) < 10) //we have more room
    {
      if(usPacific.locIsDST(pacificTime))
        matrix.print("PDT");
      else
        matrix.print("PST");
      matrix.setCursor(47, 0);
    }else
    { //we have less room, use 2 characters
      matrix.print("PT");
      matrix.setCursor(41, 0);
    }
    matrix.print(month(pacificTime));
    matrix.print("/");
    matrix.print(day(pacificTime));
    matrix.setCursor(0, 8);
    if(hour() < 10)
      matrix.print('0');
    matrix.print(hour());
    matrix.setCursor(10, 8);
    matrix.print(":");
    matrix.setCursor(14, 8);
    if (minute() < 10)
      matrix.print('0');
    matrix.print(minute());
    if(month() < 10) //we have more room
    {
      matrix.print("UTC");
      matrix.setCursor(47, 8);
    }else
    { //we have less room, use 2 characters
      matrix.print("UT");
      matrix.setCursor(41, 8);
    }
    matrix.print(month());
    matrix.print("/");
    matrix.print(day());
    matrix.writeScreen();
    
    uint32_t free = system_get_free_heap_size();
    Serial.println("Free memory: ");
    Serial.println(free);
    yield();
    //clock flickers on redraw. Update every 10 seconds instead of every 1.
    //count to 100 (10 seconds) to deminish flickers without blocking web requests
    for(int i = 0; i < 101; i++)
    {
      server.handleClient();
      delay(100);
    }
  }
}

void syncClock() {
  // close any connection before send a new request.
  // This will free the socket
  client.stop();
  delay(100);
  Serial.println("attempting time sync");
  if(firstSync)
  {
    matrix.clearScreen();
    matrix.setCursor(0, 0);
    matrix.print("Syncing");
    matrix.writeScreen();
    firstSync = false;
  }

  // Use WiFiClient class to create TCP connections
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
     matrix.clearScreen();
    matrix.setCursor(0, 0);
    matrix.print("Disconnected");
    matrix.writeScreen();
    delay(1000);
    return;
  }
   
  String url = "/api?timestamp=now";

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n");
  client.print("Host: " + String(host) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  // note the time that the connection was made:

  lastConnectionTime = millis();
  // Read all the lines of the reply from server and print them to Serial
  while (client.available() == 0) {
    if (millis() - lastConnectionTime > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  while (client.available()) {
    data = client.readStringUntil('\r');
    //Serial.print(data);
  }  
  char buffer[data.length() + 1];
  data.toCharArray(buffer, sizeof(buffer));     // Copy json String to char array
  buffer[data.length() + 1] = '\0';
  Serial.println(buffer);
  parseJSON(buffer);
}

void parseJSON(char json[400])
{
  StaticJsonBuffer<400> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success())
  {
    Serial.println("ParseObject() failed");
    return;
  }

  int timestamp = root["timestamp"];
  Serial.println(timestamp);
  setTime(timestamp);
  pacificTime = usPacific.toLocal(timestamp);
  
}

void handle_root() {
    if (server.args() > 0 ) { // Arguments were received
        for ( uint8_t i = 0; i < server.args(); i++ ) {
        //Serial.println(server.argName(i)); // Display the argument
        if (server.argName(i) == "message") {
          server.send(200, "application/json", "{\"message\":\"OK\"}");
          showingMessage = true;
          String message = server.arg(i);
          message.remove(255);
          Serial.print("GET message: ");
          Serial.println(message);
          //scroll message
          matrix.setTextSize(2);
          for(int x = 72; x >= (int) (message.length() * -11); x--)
          {
            matrix.clearScreen();
            matrix.setCursor(x, 0);
            matrix.print(message);
            matrix.writeScreen();
            yield();
            delay(10);
          }
          //Static message instead of scrolling:
          /*
          matrix.setTextSize(1);
          matrix.clearScreen();
          matrix.setCursor(0, 0);
          matrix.print(message);
          matrix.writeScreen();
          delay(5000);
          */
          matrix.setTextSize(1);
          matrix.setCursor(0, 0);
          showingMessage = false;
          showTime();
        }
        if (server.argName(i) == "brightness") {
          String b = (String) server.arg(i);
          int brightness = b.toInt();
          Serial.print("Setting brightness: ");
          Serial.println(brightness);
          if(brightness < 16 && brightness > 0)
          {
            matrix.setBrightness(brightness);
            server.send(200, "application/json", "{\"message\":\"OK\"}");
          }else
          {
            server.send(200, "application/json", "{\"message\":\"Invalid Brightness\"}");
          }
        }
      }
    }else
  {
    server.send(200, "application/json", "{\"message\":\"hello!\"}");
  }
  delay(100);
}
