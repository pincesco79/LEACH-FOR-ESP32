/*
 Basic MQTT example with Authentication

  - connects to an MQTT server, providing username
    and password
  - publishes "hello world" to the topic "outTopic"
  - subscribes to the topic "inTopic"
*/

#include <WiFi.h>
#include <PubSubClient.h>


IPAddress server(192, 168, 79, 16);

const char* WIFI_SSID     = "IoT-Net";
const char* WIFI_PASSWORD = "4ndr0m3d4;1";


void setupWiFi() {
  Serial.print("Connecting to wifi SSID ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}



void callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
}

WiFiClient espClient;
PubSubClient client(server, 1883, callback, espClient);

void setup()
{
   setupWiFi();
  // Note - the default maximum packet size is 128 bytes. If the
  // combined length of clientId, username and password exceed this use the
  // following to increase the buffer size:
  // client.setBufferSize(255);
  
  if (client.connect("arduinoClient", "dayana", "12345")) {
    client.publish("sensor/Temp","25");
    client.subscribe("sensor/Temp");
  }
}

void loop()
{
  client.loop();
}

