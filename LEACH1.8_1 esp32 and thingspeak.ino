#include <WiFi.h>
#include "Arduino.h"
#include "LoRa_E220.h"
#include <ThingSpeak.h>

// Ajustar este ID único para cada nodo: 1, 2 o 3
const uint8_t NODE_ID = 1; 

// Credenciales WiFi (reemplazar con las tuyas)
const char* ssid = "iPhone Anthonny";
const char* password = "Anthonny1998";

// Datos de ThingSpeak
unsigned long myChannelNumber = 2781726; // Reemplaza con tu número de canal
const char * myWriteAPIKey = "ZIYZH4FIN4XDJAVC";

WiFiClient client;

// Configuración de pines para el ESP32
LoRa_E220 e220ttl(17, 16, &Serial2, 18, 21, 19, UART_BPS_RATE_9600);

// Definir el pin del LED
const int ledPin = 23;

// Número de intentos de envío por miembro
#define MEMBER_ATTEMPTS 3

// Variables globales
bool isClusterHead = false;   
bool hasClusterHead = false;  
int batteryLevel;             
float probabilityThreshold;
unsigned long lastClusterHeadTime = 0; 
unsigned long cooldownTime = 5000;     
unsigned long lastMemberTime = 0;

// Ajustes de tiempo (no se cambian)
unsigned long arbitrationTime = 2000;   // 2s
unsigned long memberCheckTime = 6000;   // 6s

// Estructura para rastrear miembros confirmados
bool membersConfirmed[4] = {false, false, false, false}; // Índices 1-3 usados

// Arreglo para almacenar la temperatura de cada nodo (1,2,3)
float nodeTemp[4] = {0.0, 0.0, 0.0, 0.0}; // Por defecto 0.0

void setup() {
  Serial.begin(9600);
  delay(1000);

  pinMode(ledPin, OUTPUT);

  if (e220ttl.begin()) {
    Serial.println("Error inicializando el módulo E220");
    while (1);
  }
  Serial.print("Módulo E220 inicializado correctamente. Nodo ID: ");
  Serial.println(NODE_ID);

  randomSeed(analogRead(0)*NODE_ID);

  // Conectar a WiFi
  Serial.println("Conectando a WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Conectado.");
  ThingSpeak.begin(client); 
}

void loop() {
  batteryLevel = random(80, 101); 
  probabilityThreshold = random(90, 96) / 100.0; 

  float probability = batteryLevel / 100.0;

  if (hasClusterHead) {
    listenForClusterHead();
  } else {
    if (millis() - lastClusterHeadTime >= cooldownTime && probability >= probabilityThreshold) {
      startArbitration(probability);
    } else {
      listenForClusterHead();
    }
  }

  delay(100); // Pausa corta
}

// Iniciar arbitraje para ser Cluster Head
void startArbitration(float probability) {
  Serial.println("Iniciando arbitraje para ser Cluster Head...");

  String msg = "Solicitud CH:" + String(NODE_ID) + ":" + String(batteryLevel) + ":" + String(probability);
  e220ttl.sendMessage(msg);

  unsigned long startTime = millis();
  bool canBecomeCH = true;
  uint8_t bestNodeID = NODE_ID;
  int bestBattery = batteryLevel;
  float bestProb = probability;

  while (millis() - startTime < arbitrationTime) {
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String m = rc.data;
      if (m.startsWith("Solicitud CH:")) {
        int firstColon = m.indexOf(':', 12);
        int secondColon = m.indexOf(':', firstColon + 1);

        uint8_t otherID = m.substring(12, firstColon).toInt();
        int otherBattery = m.substring(firstColon + 1, secondColon).toInt();
        float otherProb = m.substring(secondColon + 1).toFloat();

        // Menor ID gana prioridad
        if (otherID < bestNodeID) {
          bestNodeID = otherID;
          bestBattery = otherBattery;
          bestProb = otherProb;
        } else if (otherID == bestNodeID) {
          // Desempate final 50%
          if (otherBattery > bestBattery || (otherBattery == bestBattery && otherProb > bestProb)) {
            if (random(0,2) == 0) {
              canBecomeCH = false;
            }
          }
        }
      } else if (m == "Yo soy el Cluster Head") {
        hasClusterHead = true;
        canBecomeCH = false;
        Serial.println("Detectado otro Cluster Head durante arbitraje. Abandonando arbitraje.");
        break; 
      }
    }
  }

  if (bestNodeID != NODE_ID) {
    canBecomeCH = false;
  }

  if (!canBecomeCH) {
    Serial.println("Pierdo arbitraje o detecto CH antes que yo, entro en cooldown corto...");
    lastClusterHeadTime = millis();
    // No se cambian cooldowns si no se pidió, queda igual
    cooldownTime = random(5000, 10000); 
    hasClusterHead = false;
    isClusterHead = false;
    return;
  }

  becomeClusterHead(probability);
}

// Convertirse en Cluster Head
void becomeClusterHead(float probability) {
  isClusterHead = true;
  hasClusterHead = false; 
  lastClusterHeadTime = millis();

  Serial.println("Soy Cluster Head. Avisando a los demás...");
  Serial.print("Nivel de batería (CH): ");
  Serial.println(batteryLevel);
  Serial.print("Probabilidad (CH): ");
  Serial.println(probability);
  Serial.print("Umbral utilizado: ");
  Serial.println(probabilityThreshold);

  digitalWrite(ledPin, HIGH);
  e220ttl.sendMessage("Yo soy el Cluster Head");

  // Reiniciar la estructura de miembros confirmados y temperaturas
  for(int i = 0; i < 4; i++) {
    membersConfirmed[i] = false;
    nodeTemp[i] = 0.0;
  }

  unsigned long startTime = millis();
  int memberCount = 0; 
  while (millis() - startTime < memberCheckTime) {
    delay(10); // Pequeña pausa
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String msg = rc.data;
      // Esperamos "Temp:<ID>:<TEMP>"
      // Reemplazamos "Yo soy tu miembro:" por "Temp:<ID>:<TEMP>"
      if (msg.startsWith("Temp:")) {
        int firstColon = msg.indexOf(':', 5); 
        if(firstColon == -1) continue; 
        uint8_t memberID = msg.substring(5, firstColon).toInt();
        float memberTemp = msg.substring(firstColon+1).toFloat();

        if(memberID >=1 && memberID <=3) {
          if(!membersConfirmed[memberID]){
            membersConfirmed[memberID] = true;
            nodeTemp[memberID] = memberTemp; 
            memberCount++;
            Serial.print("Temperatura recibida de nodo ");
            Serial.print(memberID);
            Serial.print(": ");
            Serial.println(memberTemp);
          } else {
            Serial.print("Mensaje duplicado de nodo ");
            Serial.println(memberID);
          }
        } else {
          Serial.print("Nodo ID inválido recibido: ");
          Serial.println(memberID);
        }
      }
    }
  }

  // Asignar temperatura del CH (23 a 25 grados)
  nodeTemp[NODE_ID] = (float)random(23,26);

  digitalWrite(ledPin, LOW);
  isClusterHead = false;
  hasClusterHead = false; 
  lastClusterHeadTime = millis();

  if (memberCount == 0) {
    Serial.println("Sin miembros. Reseteando estado...");
    cooldownTime = random(5000,10000); 
  } else {
    // Aumentar cooldown largo a 20-25 segundos
    cooldownTime = random(20000,25000); 
  }

  // Si no se recibió de un nodo, nodeTemp para ese nodo se queda en 0
  // Ahora enviar datos a ThingSpeak
  // field1 = TEMP nodo 1, field2 = TEMP nodo 2, field3 = TEMP nodo 3
  ThingSpeak.setField(1, nodeTemp[1]);
  ThingSpeak.setField(2, nodeTemp[2]);
  ThingSpeak.setField(3, nodeTemp[3]);

  int result = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if(result == 200) {
    Serial.println("Datos enviados correctamente a ThingSpeak.");
  } else {
    Serial.print("Error enviando datos a ThingSpeak. Código: ");
    Serial.println(result);
  }
}

// Escuchar mensajes de otros nodos (cuando no es CH)
void listenForClusterHead() {
  ResponseContainer rc = e220ttl.receiveMessage();
  if (rc.status.code == 1) {
    String message = rc.data;
    if (message == "Yo soy el Cluster Head") {
      hasClusterHead = true;
      Serial.println("Otro nodo es Cluster Head. Soy miembro.");

      // Generar temperatura aleatoria del miembro (23 a 25 grados)
      float myTemp = (float)random(23,26);

      // Retraso aleatorio antes del primer envío (0-1000ms)
      unsigned long randomWait = random(0,1001);
      delay(randomWait);

      // Enviar varios intentos (MEMBER_ATTEMPTS) del tipo "Temp:<ID>:<TEMP>"
      for (int i = 0; i < MEMBER_ATTEMPTS; i++) {
        String memberMsg = "Temp:" + String(NODE_ID) + ":" + String(myTemp,1);
        e220ttl.sendMessage(memberMsg);
        Serial.print("Enviando temperatura de miembro (Intento ");
        Serial.print(i+1);
        Serial.print(") Temp: ");
        Serial.println(myTemp,1);

        if (i < MEMBER_ATTEMPTS - 1) {
          // Multiplicador aleatorio de 0 a 1.5
          float factor = (float)random(0,151) / 100.0; 
          unsigned long attemptDelay = (unsigned long)(500 * factor); 
          delay(attemptDelay); 
        }
      }

      delay(200);
      hasClusterHead = false; 
    }
  }
}
