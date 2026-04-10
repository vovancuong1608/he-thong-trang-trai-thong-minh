#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <PubSubClient.h>
#include <ArduinoJson.h>


const char* ssid = "Wokwi-GUEST"; 
const char* password = "";
const char* mqtt_server = "491a885f0fda4d9a8bd65b42c7168f35.s1.eu.hivemq.cloud"; 
const int mqtt_port = 8883; 
const char* mqtt_user = "vancuong";
const char* mqtt_pass = "01276263198Cuong";


const char* mqtt_topic_pub = "thuy_san_smart/data";
const char* mqtt_topic_sub = "thuy_san_smart/control"; 

WiFiClientSecure espClient; 
PubSubClient client(espClient);


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define LED_RED 5      
#define LED_BLUE 18    
#define LED_YELLOW 19  
#define LED_PUMP 2     

#define POT_PIN 34
#define TRIG_PIN 12
#define ECHO_PIN 14

int controlMode = 0; 


void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Nhan lenh tu Node-RED: ");
  Serial.println(message);
  

  if (message == "ON") controlMode = 1;
  else if (message == "OFF") controlMode = 2;
  else if (message == "AUTO") controlMode = 0;
}


void setup_wifi() {
  delay(10);
  Serial.println("Dang ket noi WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi da ket noi");
  espClient.setInsecure(); 
}


void reconnect() {
  while (!client.connected()) {
    Serial.print("Dang ket noi HiveMQ Cloud...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" KET NOI THANH CONG!");
    
      client.subscribe(mqtt_topic_sub); 
    } else {
      Serial.print(" That bai, ma loi: ");
      Serial.print(client.state());
      Serial.println(" Thu lai sau 5 giay");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT); 
  pinMode(LED_PUMP, OUTPUT); 
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  dht.begin();
  setup_wifi();
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback); 
  
  
  Wire.begin(); 
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Loi: Khong tim thay man hinh OLED (Kiem tra lai day SDA, SCL hoac thu doi thanh 0x3D)"));
    for(;;);
  }
  display.clearDisplay();
  display.display();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  //đọc dữ liệu cảm biến
  float t_raw = dht.readTemperature();
  int t = isnan(t_raw) ? 0 : (int)round(t_raw); 
  
  int salinity = map(analogRead(POT_PIN), 0, 4095, 0, 100);
  
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  float duration = pulseIn(ECHO_PIN, HIGH);
  int distance = (int)round(duration * 0.034 / 2);

  
  bool errorDist = (distance < 150 || distance > 350);
  bool errorTemp = (t < 10 || t > 30);
  bool errorSalt = (salinity > 50);

  digitalWrite(LED_RED, errorDist ? HIGH : LOW);    // Lỗi mực nước
  digitalWrite(LED_BLUE, errorTemp ? HIGH : LOW);   // Lỗi nhiệt độ
  digitalWrite(LED_YELLOW, errorSalt ? HIGH : LOW); // Lỗi độ mặn

 
  bool autoPump = false;
  String pumpAction = "";

  if (errorDist && errorTemp && errorSalt) {
    autoPump = true;
    pumpAction = "NGUY HIEM HO NUOC"; 
  } 
  else if (errorDist) {
    autoPump = true;
    pumpAction = (distance > 350) ? "HUT NUOC RA" : "BOM NUOC VAO";
  } 
  else if (errorSalt) {
    autoPump = true;
    pumpAction = "XU LY DO MAN";
  } 
  else if (errorTemp) {
    autoPump = true;
    pumpAction = "DIEU CHINH NHIET";
  } 
  else {
    autoPump = false;
    pumpAction = "HE THONG ON DINH";
  }

  
  bool finalPumping = false;
  
  if (controlMode == 1) { 
    finalPumping = true;
    pumpAction = "DIEU KHIEN TAY: BAT";
  } 
  else if (controlMode == 2) {
    finalPumping = false;
    pumpAction = "DIEU KHIEN TAY: TAT";
  } 
  else {
    finalPumping = autoPump;
   
  }

  digitalWrite(LED_PUMP, finalPumping ? HIGH : LOW);

  
  StaticJsonDocument<256> doc;
  doc["temp"] = t;
  doc["dist"] = distance;
  doc["salt"] = salinity;
  doc["pump"] = finalPumping ? "BAT" : "TAT";
  doc["mode"] = pumpAction;

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(mqtt_topic_pub, buffer); 

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  display.setTextSize(1);
  display.printf("N.Do: %d C | Nuoc:%d\n", t, distance);
  display.printf("Do Man: %d ppt\n", salinity);
  display.println("---------------------");
  
  display.print("MAY BOM: ");
  display.setTextSize(2);
  display.println(finalPumping ? " BAT" : " TAT");
  
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print("MODE: "); 
  display.print(pumpAction);
  
  display.display();

  delay(1000); 
}