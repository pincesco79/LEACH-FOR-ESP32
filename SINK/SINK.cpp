#include "Arduino.h"
#include "LoRa_E220.h"
#include <WiFi.h>
#include <PubSubClient.h>

// LoRa E220 (pines ajusta si requieres)
LoRa_E220 e220ttl(17, 16, &Serial2, 15, 21, 19, UART_BPS_RATE_9600);

// Credenciales WiFi
//const char* WIFI_SSID     = "iPhone Anthonny";
//const char* WIFI_PASSWORD = "Anthonny1998";

const char* WIFI_SSID     = "IoT-Net";
const char* WIFI_PASSWORD = "4ndr0m3d4;1";

// Broker MQTT
//const char* MQTT_SERVER   = "172.20.10.3";
const char* MQTT_SERVER   = "192.168.79.16";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "dayana";
const char* MQTT_PASS     = "12345";
const char* MQTT_CLIENT_ID = "ESP32SinkClient";

// Tópicos
const char* TOPIC_TEMP  = "sensor/Temp";
const char* TOPIC_HUM   = "sensor/Hum";
const char* TOPIC_SOIL  = "sensor/Soil";
const char* TOPIC_RAIN  = "sensor/Rain";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setupWiFi();
void reconnectMQTT();
void publishSensorValues(String line);

void setup() {
  Serial.begin(9600);
  delay(2000);

  // Iniciar LoRa
  if (!e220ttl.begin()) {
    Serial.println("Error inicializando E220");
    while(1);
  }
  Serial.println("Receptor LoRa listo.");

  // WiFi + MQTT
  setupWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // Podrías reconectar si deseas
  }
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Escuchar LoRa
  ResponseContainer rc = e220ttl.receiveMessage();
  if (rc.status.code == 1) {
    String incoming = rc.data;
    incoming.trim();
    if (incoming.length() > 0) {
      Serial.print("Datos recibidos: ");
      Serial.println(incoming);
      
      // Ver si empieza con "Temp=" (o "Hum=", etc.)
      if (incoming.startsWith("Temp=")) {
        // Parseamos y publicamos con retain
        publishSensorValues(incoming);
      }
    }
  }
  delay(10);
}

// ------------------ SETUP WIFI ------------------
void setupWiFi() {
  Serial.print("Conectando a ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado, IP: ");
  Serial.println(WiFi.localIP());
}

// ------------------ RECONNECT MQTT ------------------
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Intentando conexión MQTT... ");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("¡Conectado al broker!");
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" intentando de nuevo en 5s");
      delay(5000);
    }
  }
}

// ------------------ PARSE & PUBLISH ------------------
// Con 'retain=true' para que los mensajes se guarden en el broker
void publishSensorValues(String line) {
  // line: "Temp=21.20 | Hum=37.10 | Soil=0.00 | Rain=0.00"
  // Splitear por " | "
  String splitted[4];
  int startIndex = 0;
  for(int i=0; i<4; i++) {
    int foundPos = line.indexOf(" | ", startIndex);
    if (foundPos == -1) {
      splitted[i] = line.substring(startIndex);
      break;
    } else {
      splitted[i] = line.substring(startIndex, foundPos);
      startIndex = foundPos + 3; // saltar " | "
    }
  }
  // splitted[0] = "Temp=21.20"
  // splitted[1] = "Hum=37.10"
  // splitted[2] = "Soil=0.00"
  // splitted[3] = "Rain=0.00"

  for (int i=0; i<4; i++) {
    splitted[i].trim();
    int eqPos = splitted[i].indexOf('=');
    if (eqPos == -1) continue;
    String varName = splitted[i].substring(0, eqPos);
    String varValue= splitted[i].substring(eqPos+1);
    varName.trim();
    varValue.trim();

    // Publicar con retain=true
    if (varName.equalsIgnoreCase("Temp")) {
      if (mqttClient.publish(TOPIC_TEMP, varValue.c_str(), true)) {
        Serial.println("Publicado sensor/Temp (retain): " + varValue);
      }
    } 
    else if (varName.equalsIgnoreCase("Hum")) {
      if (mqttClient.publish(TOPIC_HUM, varValue.c_str(), true)) {
        Serial.println("Publicado sensor/Hum (retain): " + varValue);
      }
    }
    else if (varName.equalsIgnoreCase("Soil")) {
      if (mqttClient.publish(TOPIC_SOIL, varValue.c_str(), true)) {
        Serial.println("Publicado sensor/Soil (retain): " + varValue);
      }
    }
    else if (varName.equalsIgnoreCase("Rain")) {
      if (mqttClient.publish(TOPIC_RAIN, varValue.c_str(), true)) {
        Serial.println("Publicado sensor/Rain (retain): " + varValue);
      }
    }
  }
}

