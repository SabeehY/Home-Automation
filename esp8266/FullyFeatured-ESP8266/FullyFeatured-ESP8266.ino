#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define MQTT_HOST IPAddress(192, 168, 0, 100)
#define MQTT_PORT 1883

#define LED 2
#define RELAY D7
#define Subscribe_Channel "main_door"
#define Publish_Channel "status/main_door"
#define id "main_door"

// BELL RELATED CODE
#define BELL D3
int buttonState = 0;
long previousMillis = 0;        // will store last time LED was updated
 
// the follow variables is a long because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long interval = 10000;           // interval at which to blink (milliseconds)

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
Ticker bellCheckTimer;
Ticker switchTimer;
Ticker wifiReconnectTimer;

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
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void initalizeOTA() {
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(id);

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

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT");
  ledOn();
  Serial.println(sessionPresent);
  mqttClient.subscribe(Subscribe_Channel, 1);
  mqttClient.publish(Publish_Channel, 0, false, "Module is Online!");
  bellCheckTimer.attach_ms(500, checkBell);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  bellCheckTimer.detach();
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
  Serial.print("Message recieved: ");
  Serial.println(payload);

  char message = payload[0];
  if (message == '1') {
    triggerSwitch();
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

void relayOff() {
  digitalWrite(RELAY, HIGH);
}

void bellOff() {
  pinMode(BELL, OUTPUT);
  digitalWrite(BELL, HIGH);
  pinMode(BELL, INPUT);
}

void triggerSwitch() {
  digitalWrite(RELAY, LOW);
  switchTimer.once(0.5, relayOff);
}

void sendAlert() {
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis > interval) {
    // save the last time the bell was pushed 
    previousMillis = currentMillis;
    mqttClient.publish("bell", 0, false, "1");
  }
}

void checkBell() {
  buttonState = digitalRead(BELL);
  if (buttonState == LOW) {
      bellOff();
      sendAlert();
  }
}

void setup() {
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BELL, INPUT);
  
  Serial.begin(115200);
  Serial.println();

  // Initialize LED to HIGH
  relayOff();
  ledOff();
  bellOff();

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setClientId(id);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();
}

void loop() {
  ArduinoOTA.handle();
}
