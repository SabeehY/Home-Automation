#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define WIFI_SSID "Sabeeh 2.4Ghz"
#define WIFI_PASSWORD "Sabeeh_697"

#define MQTT_HOST IPAddress(192, 168, 0, 100)
#define MQTT_PORT 1883

#define LED 2
#define RELAY 5

#define HOSTNAME "sunny_room"

String hostname_string = String(HOSTNAME) + String("-") + String(ESP.getChipId(), HEX);
const char* hostname = hostname_string.c_str();

const char* light_command_channel = "sunny_room/light/switch";
const char* light_status_channel = "sunny_room/light/status";

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
Ticker wifiReconnectTimer;
Ticker mdnsReconnectTimer;

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
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  ledOff();
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
    mqttClient.publish(light_status_channel, 0, false, "ON");
  
  } else if (message == '0' || message == 'OFF') {
    lightOff();
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

void setup() {
  Serial.begin(115200);
  Serial.println("\r\nsetup()");
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  
  WiFi.hostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("Hostname: " + WiFi.hostname());

  // Initialize LED to HIGH
  ledOff();
  lightOn();
  

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
