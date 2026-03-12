#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// Pin layout
#define ESP_SCL_PIN D1
#define ESP_SDA_PIN D2
#define DHT22_PIN D5
#define MOISTURE_PIN A0
#define MOISTURE_VCC D6
#define MOISTURE_AVERAGING_LIMIT 10

// Network
#define SSID "SSID HERE"
#define PWD "PWD HERE"
#define MAX_RETRIES 10
#define TIMEOUT_DUR 5000
#define RECONNECT_INTERVAL 10000
#define READ_INTERVAL 3600000UL
// #define READ_INTERVAL 10000UL

unsigned long last_read_time = 0 - READ_INTERVAL;
unsigned long last_reconnect_attempt = 0;
const char* payload_format = "{\"airTemp\":%.2f,\"rawSoilMoisture\":%d,\"rawHumidity\":%.2f,\"luminosity\":%.2f}";
const char* url = "http://0.0.0.0:3000/data";


BH1750 lightMeter;
DHT dht(DHT22_PIN, DHT22);

WiFiClient client;

bool sendHttpRequest(const char* payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected.");
    return false;
  }

  HTTPClient http;

  http.setTimeout(TIMEOUT_DUR);
  http.begin(client, url);  
  http.addHeader("Content-Type", "application/json");

  Serial.println("Sending HTTP POST...");
  int httpCode = http.POST((uint8_t*)payload, strlen(payload));

  if (httpCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
      Serial.println("Request delivered successfully.");
      http.end();
      return true;
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
  return false;
}

int readMoisture() {
  digitalWrite(MOISTURE_VCC, HIGH);
  delay(200);
  
  // discard first analog read
  analogRead(MOISTURE_PIN);
  delay(10);

  int rawMoisture = 0;
  for(int i = 0; i < MOISTURE_AVERAGING_LIMIT; i++) {
    rawMoisture += analogRead(MOISTURE_PIN);
    yield();
    delay(10);
  }

  digitalWrite(MOISTURE_VCC, LOW);
  rawMoisture /= MOISTURE_AVERAGING_LIMIT;

  return rawMoisture;
}

void setup() {
  Serial.begin(9600);
  
  // Connect to WIFI
  Serial.println("Attempting to connect to WiFi...");
  WiFi.begin(SSID, PWD);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.println("Still attempting...");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Successfully connected to WiFi!");
  } else {
    Serial.println("WiFi connection failed.");
  }

  // Enable sensors
  Wire.begin(ESP_SDA_PIN, ESP_SCL_PIN);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  pinMode(MOISTURE_VCC, OUTPUT);
  digitalWrite(MOISTURE_VCC, LOW);
  
  dht.begin();

  Serial.println("Start sensor tests...");
}

void loop() {
  // WiFi dropped at some point
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - last_reconnect_attempt > RECONNECT_INTERVAL) {
      Serial.println("Attempting to reconnect to WiFi...");
      last_reconnect_attempt = millis();
      WiFi.reconnect();
    }

    return;
  }
  
  // Wait until 1 hour passed
  if (last_read_time == 0 || millis() - last_read_time >= READ_INTERVAL) {
    last_read_time = millis();
    
    float airTemp = dht.readTemperature();
    float humidity = dht.readHumidity();
    float lux = lightMeter.readLightLevel();
    int rawMoisture = readMoisture();

    // avoid losing rest of data if dht malfunctions
    if(isnan(humidity)) humidity = 0;
    if(isnan(airTemp)) airTemp = 0;
    
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.print(" % | Air Temperature: ");
    Serial.print(airTemp);
    Serial.print(" C | Light Level: ");
    Serial.print(lux);
    Serial.print(" Lx | Raw Moisture Value: ");
    Serial.println(rawMoisture);

    // min payload = template (60 chars) + floats (4 sensor data types * ~5 char float)
    // min payload = 80 chars
    char payload[150];
    snprintf(payload, 150, payload_format, airTemp, rawMoisture, humidity, lux);
    Serial.print("Payload: ");
    Serial.println(payload);

    int remaining_retries = MAX_RETRIES;
    while (remaining_retries-- > 0) {
      if (sendHttpRequest(payload)) {
        break;
      }

      yield();
      delay(500);
    }
  }
}
