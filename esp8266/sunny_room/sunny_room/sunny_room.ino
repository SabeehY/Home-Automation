#include <DHT.h>

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <EEPROM.h>

#define WIFI_SSID "Sabeeh 2.4Ghz"
#define WIFI_PASSWORD "Sabeeh_697"

#define MQTT_HOST IPAddress(192, 168, 0, 100)
#define MQTT_PORT 1883

#define LED 2
#define RELAY 5
#define DHTPIN 4     // Temp/Humidity signal

#define HOSTNAME "sunny_room"

String hostname_string = String(HOSTNAME) + String("-") + String(ESP.getChipId(), HEX);
const char* hostname = hostname_string.c_str();

const char* light_command_channel = "sunny_room/light/switch";
const char* light_status_channel = "sunny_room/light/status";
const char* temp_channel = "sunny_room/climate/temp";
const char* humid_channel = "sunny_room/climate/humid";

volatile int8_t dht_status = 0;
char tmp[50];
char hum[50];

DHT11 dht11;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
Ticker wifiReconnectTimer;
Ticker mdnsReconnectTimer;
Ticker dhtReadTimer;
Ticker dhtSendTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
  initalizeOTA();
  setupMdns();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void initalizeOTA() {
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostname);

  // No authentication by default
  ArduinoOTA.setPassword("esp8266");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void setupMdns() {
  if(!MDNS.begin(hostname)) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.print("mDNS responder started, hostname: ");
    Serial.println(hostname);
    MDNS.addService("esp", "tcp", 8080); // Announce esp tcp service on port 8080
  }
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT");
  ledOn();
  Serial.println(sessionPresent);
  Serial.print("Subscribing to:");
  Serial.println(light_command_channel);
  mqttClient.subscribe(light_command_channel, 1);
  // Attach the dht checker
  dhtSendTimer.attach(30, dhtReadAndPublish);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  ledOff();
  dhtSendTimer.detach();
  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.print("Payload recieved: "); Serial.println(payload);
  Serial.print("Topic: "); Serial.println(topic);
  char message = payload[0];
  Serial.print("Message:"); Serial.println(message);

  if (message == '1' || message == 'ON') {
    lightOn();
    saveLightStateToEEPROM(1);
    mqttClient.publish(light_status_channel, 0, false, "ON");
  
  } else if (message == '0' || message == 'OFF') {
    lightOff();
    saveLightStateToEEPROM(0);
    Serial.print("Turning OFF");
    mqttClient.publish(light_status_channel, 0, false, "OFF");
 
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void ledOff() {
  digitalWrite(LED, HIGH);
}

void ledOn() {
  digitalWrite(LED, LOW);
}

void lightOff() {
  digitalWrite(RELAY, LOW);
}

void lightOn() {
  digitalWrite(RELAY, HIGH);
}

void readDHT() {
  dht11.read();
}

// this callback will be call from an interrupt
// it should be short and carry the ICACHE_RAM_ATTR attribute
void ICACHE_RAM_ATTR dhtCallback(int8_t res) {
  dht_status = res;
}

void dhtReadAndPublish() {
  if (dht_status > 0) {
    dht_status = 0;
    float t = dht11.getTemperature();
    float h = dht11.getHumidity();
    Serial.printf("Temp: %g°C\n", t);
    Serial.printf("Humid: %g%%\n", h);
    itoa(t,tmp,10);
    itoa(h,hum,10);
    mqttClient.publish(temp_channel, 0, false, tmp);
    mqttClient.publish(humid_channel, 0, false, hum);
  } else {
    dht_status = 0;
    Serial.printf("Error: %s\n", dht11.getError());
  }
}

void saveLightStateToEEPROM(uint state) {
  // Address in eeprom
  uint addr = 0;


  EEPROM.begin(512);

  // load EEPROM data into RAM, see it
  EEPROM.put(addr,state);
  EEPROM.commit();
}

void readLightStateFromEEPROM() {
  // Address in eeprom
  uint addr = 0;
  uint state = 1; // Initial State is low


  EEPROM.begin(512);

  // load EEPROM data into RAM, see it
  EEPROM.get(addr,state);
  if (state == 1) {
    lightOn();
  } else {
    lightOff();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\r\nsetup()");
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  dht11.setPin(DHTPIN);
  dht11.setCallback(dhtCallback);
  dhtReadTimer.attach(10, readDHT);
  
  
  WiFi.hostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("Hostname: " + WiFi.hostname());

  // Initialize LED to HIGH
  ledOff();

  // Read saved light state form EEPROM
  readLightStateFromEEPROM();
  

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setClientId(hostname);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();
}

void loop() {
  ArduinoOTA.handle();
  MDNS.update();
  yield();
}
