/*******************************************************
 * Ejemplo: ESP8266 + MQTT + OLED SSD1306 + LED irrigación
 * 
 * - Se conecta a WiFi (ssid/password).
 * - Se conecta al broker MQTT (mqtt_server, mqtt_port, mqtt_user, mqtt_pass).
 * - Se suscribe a 4 tópicos: sensor/Temp, sensor/Hum, sensor/Soil, sensor/Rain.
 * - Muestra los valores en un OLED SSD1306 (128x64).
 * - Si Soil < 550 => Enciende un LED 1 min (simula irrigación), usando millis().
 * - No vuelve a encender hasta que llegue otro valor Soil que cumpla la condición.
 *******************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Librerías para el OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Credenciales WiFi ---
const char* ssid = "iPhone Anthonny";
const char* password = "Anthonny1998";

// --- Datos broker MQTT ---
const char* mqtt_server   = "172.20.10.3";
const int   mqtt_port     = 1883;
const char* mqtt_user     = "dayana";
const char* mqtt_pass     = "";

// Tópicos de interés
const char* topicTemp  = "sensor/Temp";
const char* topicHum   = "sensor/Hum";
const char* topicSoil  = "sensor/Soil";
const char* topicRain  = "sensor/Rain";

// --- Objetos para WiFi y MQTT ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Variables para almacenar los valores recibidos ---
String strTemp = "--";
String strHum  = "--";
String strSoil = "--";
String strRain = "--";

// --- Configuración OLED SSD1306 ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1  
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Lógica de riego (LED) ---
const int SOIL_THRESHOLD = 550;         // Umbral para Soil
const int LED_PIN = 2;                 // Pin del LED (D4 en NodeMCU)
bool ledIsOn = false;                  // Indica si LED está encendido
unsigned long ledOnTime = 0;           // Cuándo se encendió
const unsigned long LED_DURATION = 20000; // 1 min (60000 ms)

// ------------------------------------------------------
// (1) Conectarse a WiFi
// ------------------------------------------------------
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi conectado.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ------------------------------------------------------
// (2) Mostrar los datos en la pantalla
// ------------------------------------------------------
void displayValues() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  display.println("Values (MQTT):");
  display.println("---------------------");

  display.print("Temp: ");
  display.println(strTemp);

  display.print("Hum:  ");
  display.println(strHum);

  display.print("Soil: ");
  display.println(strSoil);

  display.print("Rain: ");
  display.println(strRain);

  // Mostrar estado de riego (LED)
  display.println("---------------------");
  if (ledIsOn) {
    display.println("IRRIGATION: ON");
  } else {
    display.println("IRRIGATION: OFF");
  }

  display.display();
}

// ------------------------------------------------------
// (3) Callback al llegar un mensaje MQTT
// ------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  // Convertir payload a String
  String incoming;
  for (int i = 0; i < length; i++) {
    incoming += (char)payload[i];
  }
  incoming.trim();

  Serial.print("Mensaje [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(incoming);

  // Verificar a qué tópico pertenece
  if (String(topic) == topicTemp) {
    strTemp = incoming;
  } 
  else if (String(topic) == topicHum) {
    strHum = incoming;
  }
  else if (String(topic) == topicSoil) {
    strSoil = incoming;

    // Convertir a entero para comparar
    int soilValue = incoming.toInt();
    Serial.print("Soil value: ");
    Serial.println(soilValue);

    // Encender LED si soilValue < 550 y LED está apagado
    if (!ledIsOn && soilValue < SOIL_THRESHOLD) {
      Serial.println("Soil < threshold => Riego ON por 1 min");
      digitalWrite(LED_PIN, HIGH);
      ledIsOn = true;
      ledOnTime = millis();
    }
  }
  else if (String(topic) == topicRain) {
    strRain = incoming;
  }

  // Actualizar OLED
  displayValues();
}

// ------------------------------------------------------
// (4) Reconectar MQTT
// ------------------------------------------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (client.connect("ESP8266OledClient", mqtt_user, mqtt_pass)) {
      Serial.println("Conectado!");
      // Suscribirse a los tópicos
      client.subscribe(topicTemp);
      client.subscribe(topicHum);
      client.subscribe(topicSoil);
      client.subscribe(topicRain);
    } else {
      Serial.print("falló (rc=");
      Serial.print(client.state());
      Serial.println("). Reintentamos en 5s...");
      delay(5000);
    }
  }
}

// ------------------------------------------------------
// setup()
// ------------------------------------------------------
void setup() {
  Serial.begin(9600);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Apagado inicialmente

  // (1) WiFi
  setup_wifi();

  // (2) MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // (3) Inicializar OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Fallo al inicializar SSD1306"));
    while(1);
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("LoRa");
  display.println("LEACH");
  display.println("PROJECT!");
  display.display();
  delay(1500);

  // Mostrar datos iniciales
  displayValues();
}

// ------------------------------------------------------
// loop()
// ------------------------------------------------------
void loop() {
  // Mantener conexión MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Revisar si LED está encendido y apagar tras 1 min
  if (ledIsOn) {
    unsigned long currentTime = millis();
    if (currentTime - ledOnTime >= LED_DURATION) {
      Serial.println("Apagando LED tras 1 min...");
      digitalWrite(LED_PIN, LOW);
      ledIsOn = false;
      // Actualizar OLED (opcional)
      displayValues();
    }
  }
}
