

// URL to get current weather
//const char* OWM_URL = "http://api.openweathermap.org/data/2.5/weather?id=3220035&appid=YOUR_API_KEY_HERE&units=metric";


#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ── WiFi ──────────────────────────────────────────────────────
const char* WIFI_SSID     = "iPhone";
const char* WIFI_PASSWORD = "123456789";  

// ── MQTT ──────────────────────────────────────────────────────
const char* MQTT_BROKER = "192.168.0.185";  
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "esp8266_dht11";

// ── MQTT Topics ───────────────────────────────────────────────
const char* TOPIC_TEMP = "chocolate/temperature";
const char* TOPIC_HUM  = "chocolate/humidity";
const char* TOPIC_STATUS = "chocolate/status";
const char* TOPIC_ALERT  = "chocolate/alert";


// ── Telegram ─────────────────────────────────────────────────
const char* BOT_TOKEN = "8967677472:AAHAxoqtlVwoGYk4S4tX_nYv7qMabaqBVkY";
const char* CHAT_ID   = "7483647404";


// ── OpenWeatherMap ────────────────────────────────────────────
const char* OWM_API_KEY = "05d64a06e0b25aac99eb3884854df429";  
const char* OWM_CITY_ID = "2946447";            // Bonn

// ── DHT11 ─────────────────────────────────────────────────────
#define DHTPIN  D4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ── LED Ampel ─────────────────────────────────────────────────
#define LED_GREEN D2
#define LED_RED   D3

// ── Thresholds ────────────────────────────────────────────────
const float TEMP_MAX          = 22.0;
const float HUM_MAX           = 55.0;
const float WEATHER_TEMP_WARN = 20.0;
const float WEATHER_HUM_WARN  = 60.0;

// ── Objects ───────────────────────────────────────────────────
WiFiClient   espClient;
PubSubClient mqttClient(espClient);

// ── Timing ────────────────────────────────────────────────────
unsigned long lastMsg          = 0;
unsigned long lastAlert        = 0;
unsigned long lastWeather      = 0;
const long    INTERVAL         = 5000;
const long    ALERT_COOLDOWN   = 60000;
const long    WEATHER_INTERVAL = 300000;

// ── Weather data ──────────────────────────────────────────────
float weatherTemp   = 0;
float weatherHum    = 0;
bool  weatherLoaded = false;

// ─────────────────────────────────────────────────────────────
// LED Ampel
// RED  = temperature > 22°C OR humidity > 55%
// GREEN = temperature <= 22°C AND humidity <= 55%
// ─────────────────────────────────────────────────────────────
void setAmpel(float temperature, float humidity) {
  bool tempAlarm = temperature > TEMP_MAX;
  bool humAlarm  = humidity    > HUM_MAX;

  if (tempAlarm || humAlarm) {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED,   HIGH);
    Serial.print("AMPEL: ROT  — ");
    if (tempAlarm) Serial.print("Temp > 22C  ");
    if (humAlarm)  Serial.print("Feuchte > 55%");
    Serial.println();
  } else {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED,   LOW);
    Serial.println("AMPEL: GRUEN — Alle Werte normal");
  }
}

// ─────────────────────────────────────────────────────────────
// Telegram
// ─────────────────────────────────────────────────────────────
void sendTelegram(String message) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += BOT_TOKEN;
  url += "/sendMessage?chat_id=";
  url += CHAT_ID;
  url += "&text=";
  url += message;
  http.begin(client, url);
  int code = http.GET();
  Serial.println("Telegram: " + String(code));
  http.end();
}

// ─────────────────────────────────────────────────────────────
// OpenWeatherMap
// ─────────────────────────────────────────────────────────────
void fetchWeather() {
  Serial.println("Fetching weather...");
  WiFiClient client;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?id=";
  url += OWM_CITY_ID;
  url += "&appid=";
  url += OWM_API_KEY;
  url += "&units=metric";
  http.begin(client, url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);
    weatherTemp   = doc["main"]["temp"];
    weatherHum    = doc["main"]["humidity"];
    weatherLoaded = true;
    Serial.println("Weather: " + String(weatherTemp) + "C  " + String(weatherHum) + "%");
    char wtStr[8], whStr[8];
    dtostrf(weatherTemp, 4, 2, wtStr);
    dtostrf(weatherHum,  4, 2, whStr);
    mqttClient.publish("chocolate/weather/temperature", wtStr);
    mqttClient.publish("chocolate/weather/humidity",    whStr);
    if (weatherTemp >= WEATHER_TEMP_WARN || weatherHum >= WEATHER_HUM_WARN) {
      String warn = "PRAEVENTIVE WARNUNG\n\n";
      warn += "Aussenwetter Aachen:\n";
      warn += "Temperatur: " + String(weatherTemp, 1) + "C\n";
      warn += "Luftfeuchtigkeit: " + String(weatherHum, 0) + "%\n\n";
      warn += "Risiko: Innentemperatur koennte bald steigen!\n";
      warn += "Bitte Kuehlung pruefen.";
      sendTelegram(warn);
    }
  } else {
    Serial.println("Weather fetch failed: " + String(code));
  }
  http.end();
}

// ─────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
}

// ─────────────────────────────────────────────────────────────
// MQTT
// ─────────────────────────────────────────────────────────────
void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(MQTT_CLIENT)) {
      Serial.println("connected!");
      mqttClient.publish(TOPIC_STATUS, "online");
    } else {
      Serial.println("failed rc=" + String(mqttClient.state()));
      delay(5000);
    }
  }
}

// ─────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);

  // LED setup
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED,   OUTPUT);

  // start with green ON
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED,   LOW);
  Serial.println("AMPEL: GRUEN (System startet)");

  // DHT11
  dht.begin();
  Serial.println("DHT11 ready");

  // WiFi + MQTT
  connectWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  connectMQTT();

  // fetch weather on startup
  fetchWeather();

  // startup message
  sendTelegram("System gestartet — Schokoladenüberwachung + LED Ampel aktiv");
}

// ─────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  unsigned long now = millis();

  // fetch weather every 5 minutes
  if (now - lastWeather >= WEATHER_INTERVAL) {
    lastWeather = now;
    fetchWeather();
  }

  // read sensor every 5 seconds
  if (now - lastMsg >= INTERVAL) {
    lastMsg = now;

    float temperature = dht.readTemperature();
    float humidity    = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("DHT11 read failed!");
      return;
    }

    // convert to string
    char tempStr[8], humStr[8];
    dtostrf(temperature, 4, 2, tempStr);
    dtostrf(humidity,    4, 2, humStr);

    // publish to MQTT
    mqttClient.publish(TOPIC_TEMP, tempStr);
    mqttClient.publish(TOPIC_HUM,  humStr);

    // print to serial
    Serial.println("──────────────────────────────");
    Serial.print("Temperature: "); Serial.print(tempStr); Serial.println(" C");
    Serial.print("Humidity:    "); Serial.print(humStr);  Serial.println(" %");
    if (weatherLoaded) {
      Serial.print("Outdoor:     "); Serial.print(weatherTemp); Serial.println(" C");
    }

    // update LED ampel
    setAmpel(temperature, humidity);

    // send Telegram alarm if threshold exceeded
    bool tempAlarm = temperature > TEMP_MAX;
    bool humAlarm  = humidity    > HUM_MAX;

    if ((tempAlarm || humAlarm) && (now - lastAlert >= ALERT_COOLDOWN)) {
      lastAlert = now;
      String alert = "ALARM — Schokoladenproduktion\n\n";
      if (tempAlarm) alert += "Temperatur: " + String(temperature, 1) + "C (Grenzwert: 22C)\n";
      if (humAlarm)  alert += "Luftfeuchtigkeit: " + String(humidity, 1) + "% (Grenzwert: 55%)\n";
      alert += "\nBitte sofort pruefen!";
      mqttClient.publish(TOPIC_ALERT, "ALARM");
      sendTelegram(alert);
    }
  }
}