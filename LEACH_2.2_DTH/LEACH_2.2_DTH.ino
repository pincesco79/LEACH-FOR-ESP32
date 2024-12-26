/***************************************************************
 * NODO 1: DHT22 (Temperatura y Humedad)
 ***************************************************************/
#include "Arduino.h"
#include "LoRa_E220.h"
#include <Pangodream_18650_CL.h>
#include <DHT.h>

// ------------------- PINES Y DEFINICIONES -------------------
#define DHTPIN 2        // Pin donde se conecta el DHT22
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define ADC_PIN 4       
#define CONV_FACTOR 3.1 
#define READS 20        
Pangodream_18650_CL battery(ADC_PIN, CONV_FACTOR, READS);

int getBatteryChargeLevel(float voltage) {
  const float VOLTAGE_MIN = 2.4;
  const float VOLTAGE_MAX = 3.7;
  if (voltage >= VOLTAGE_MAX) return 100;
  if (voltage <= VOLTAGE_MIN) return 0;
  return (voltage / VOLTAGE_MAX) * 100;
}

// ------------------- ESTADOS Y CONFIG -----------------------
enum NodeState {
  SEARCHING_CH,
  CLUSTER_HEAD,
  MEMBER,
  WAITING_COOLDOWN
};

const int dry = 2400;
const int wet = 900;

// IMPORTANTE: NODO 1
const uint8_t NODE_ID = 1; 
const uint8_t TOTAL_NODES = 3;

// LoRa E220 (igual que tu código anterior)
LoRa_E220 e220ttl(17, 16, &Serial2, 15, 21, 19, UART_BPS_RATE_9600);
const int ledPin = 23;

NodeState currentState = SEARCHING_CH;
bool recentlyWasCH = false;

float batteryLevelFloat;     
unsigned long lastClusterHeadTime = 0; 
const unsigned long cooldownTime = 60000; 
unsigned long memberCheckTime = 25000;

// ------------------- ARRAYS DE DATOS ------------------------
bool membersConfirmed[TOTAL_NODES + 1] = {false, false, false, false};
// 1=> Temp/Hum en nodo1, 2 => Soil en nodo2, 3 => Rain en nodo3
float nodeTemp[TOTAL_NODES + 1] = {0,0,0,0};
float nodeHum[TOTAL_NODES + 1]  = {0,0,0,0};
float nodeSoil[TOTAL_NODES + 1] = {0,0,0,0};
float nodeRain[TOTAL_NODES + 1] = {0,0,0,0};

float batteryLevels[TOTAL_NODES + 1] = {0,0,0,0};
bool mustWaitForNextCH = false; 

// ------------------- PROTOTIPOS FUNCIONES --------------------
void performSearchClusterHead();
void actAsClusterHead();
void actAsMember();
void processReceivedMessage(String incoming);

// ------------------- SETUP --------------------
void setup() {
  Serial.begin(9600);
  delay(2000);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  dht.begin();  // Inicializar DHT

  if (!e220ttl.begin()) {
    Serial.println("Error inicializando el módulo E220");
    while (1);
  }
  Serial.print("Módulo E220 inicializado correctamente. Nodo ID: ");
  Serial.println(NODE_ID);

  randomSeed(analogRead(0) * NODE_ID);
  currentState = SEARCHING_CH;
}

// ------------------- LOOP --------------------
void loop() {
  unsigned long currentMillis = millis();

  switch (currentState) {
    case SEARCHING_CH:   performSearchClusterHead(); break;
    case CLUSTER_HEAD:   actAsClusterHead();         break;
    case MEMBER:         actAsMember();             break;
    case WAITING_COOLDOWN:
      if (currentMillis - lastClusterHeadTime >= cooldownTime) {
        Serial.println("Periodo de enfriamiento terminado. Iniciando nueva ronda.");
        currentState = SEARCHING_CH;
      }
      break;
  }
}

// ------------------------------------------------------------
//  BUSQUEDA DE CLUSTER HEAD
// ------------------------------------------------------------
void performSearchClusterHead() {
  Serial.println("=== BUSQUEDA DE CLUSTER-HEAD (FASE INICIAL) ===");
  unsigned long startSearchTime = millis();
  bool chFound = false;  
  int fib[5] = {1, 1, 2, 3, 5};
  int fibIndex = 0;
  unsigned long lastSend = 0;

  while (millis() - startSearchTime < 60000 && !chFound) {
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String incoming = rc.data;
      processReceivedMessage(incoming);
      incoming.trim();
      Serial.print("Mensaje recibido durante búsqueda CH: '");
      Serial.print(incoming);
      Serial.println("'");

      if (incoming == "Si hay cluster-head" || incoming == "Yo soy el Cluster Head") {
        chFound = true;
        mustWaitForNextCH = true;
        Serial.println("Cluster Head detectado.");
        break; 
      }
    }

    unsigned long interval = (unsigned long)(fib[fibIndex] * 1000);
    if (millis() - lastSend >= interval) {
      e220ttl.sendBroadcastFixedMessage(23, "¿Hay cluster-head?\n");
      Serial.println("Preguntando: ¿Hay cluster-head?");
      lastSend = millis();
      fibIndex++;
      if (fibIndex >= 5) fibIndex = 0;
    }
    delay(10);
  }

  if (mustWaitForNextCH) {
    Serial.println("CH ya existe, esperando siguiente ronda...");
    bool nextCHfound = false;
    while (!nextCHfound) {
      ResponseContainer rc2 = e220ttl.receiveMessage();
      if (rc2.status.code == 1) {
        String msg2 = rc2.data;
        processReceivedMessage(msg2);
        msg2.trim();
        Serial.print("Mensaje recibido mientras espera siguiente CH: '");
        Serial.print(msg2);
        Serial.println("'");

        if (msg2 == "Yo soy el Cluster Head") {
          nextCHfound = true;
          Serial.println("Nuevo CH detectado, listo para unirse.");
        }
      }
      delay(10);
    }
  }

  Serial.println("Fin de la fase de búsqueda de cluster-head.");
  delay(2000);
  currentState = MEMBER;
}

// ------------------------------------------------------------
//  ACTUAR COMO CLUSTER HEAD
// ------------------------------------------------------------
void actAsClusterHead() {
  Serial.println("Soy Cluster Head elegido por batería");
  digitalWrite(ledPin, HIGH);

  e220ttl.sendBroadcastFixedMessage(23, "Yo soy el Cluster Head\n");
  Serial.println("CH envia: Yo soy el Cluster Head");
  delay(2000); 

  // Reiniciar info
  for(int i = 1; i <= TOTAL_NODES; i++) {
    membersConfirmed[i] = false;
    nodeTemp[i] = 0.0;
    nodeHum[i]  = 0.0;
    nodeSoil[i] = 0.0;
    nodeRain[i] = 0.0;
  }

  Serial.println("CH escuchando membresías...");
  unsigned long startTime = millis();
  while (millis() - startTime < memberCheckTime) {
    delay(10); 
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String msg = rc.data;
      processReceivedMessage(msg);
      msg.trim();
      Serial.print("Mensaje recibido en CH: '");
      Serial.print(msg);
      Serial.println("'");

      if (msg == "¿Hay cluster-head?") {
        e220ttl.sendBroadcastFixedMessage(23, "Si hay cluster-head\n");
        Serial.println("CH responde: Si hay cluster-head");
      }
      if (msg.startsWith("Yo sere tu miembro:")) {
        uint8_t memberID = msg.substring(19).toInt();
        if (memberID >=1 && memberID <=3 && memberID != NODE_ID) {
          if(!membersConfirmed[memberID]){
            membersConfirmed[memberID] = true;
            Serial.print("Miembro confirmado: Nodo ");
            Serial.println(memberID);
            delay(1000); 
            e220ttl.sendBroadcastFixedMessage(23, "Miembro confirmado:" + String(memberID) + "\n");
            Serial.print("CH confirma miembro: Nodo ");
            Serial.println(memberID);
          }
        }
      }
    }
  }

  // Marcar la hora actual
  lastClusterHeadTime = millis();
  delay(2000); 

  // Enviar "Envio de horario" 5 veces
  for (int i = 0; i < 5; i++) {
    e220ttl.sendBroadcastFixedMessage(23, "Envio de horario\n");
    Serial.println("CH envia: Envio de horario");
    delay(1000);
  }

  // --- Leer MI sensor DHT (Temp/Hum) ---
  float localT = dht.readTemperature();
  float localH = dht.readHumidity();
  nodeTemp[NODE_ID] = localT;
  nodeHum[NODE_ID]  = localH;

  // Esperar 60s a que lleguen variables de los demás
  unsigned long scheduleStart = millis();
  unsigned long listenTime = 60000; 
  while (millis() - scheduleStart < listenTime) {
    delay(10);
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String incoming = rc.data;
      processReceivedMessage(incoming);
      Serial.print("CH recibe: ");
      Serial.println(incoming);
    }
  }

  // Ahora solo imprimimos UNA VEZ
  // nodeTemp[1], nodeHum[1], nodeSoil[2], nodeRain[3]
  Serial.println("=== Datos finales de la ronda ===");
  String finalData = "Temp=" + String(nodeTemp[1]) +
                     " | Hum=" + String(nodeHum[1]) +
                     " | Soil=" + String(nodeSoil[2]) +
                     " | Rain=" + String(nodeRain[3]);
  Serial.println("Datos recogidos: " + finalData);

  // Enviar estos datos a un nodo específico (0x0002, canal 23)
  ResponseStatus rs = e220ttl.sendFixedMessage(0,2,23, finalData.c_str());
  Serial.println("Datos enviados a 0x0002 canal 23.");

  delay(2000); 
  e220ttl.sendBroadcastFixedMessage(23, "Termine mi ronda\n");
  Serial.println("CH envia: Termine mi ronda");
  digitalWrite(ledPin, LOW);
  delay(1000); 

  recentlyWasCH = true;
  currentState = WAITING_COOLDOWN;
}

// ------------------------------------------------------------
//  ACTUAR COMO MIEMBRO
// ------------------------------------------------------------
void actAsMember() {
  float voltageNow = battery.getBatteryVolts();
  if (recentlyWasCH) {
    Serial.println("Enviando bateria = 1 (Porque acabo de ser CH).");
    batteryLevels[NODE_ID] = 1;
    recentlyWasCH = false;
  } else {
    batteryLevels[NODE_ID] = voltageNow;
  }
  Serial.print("Mi nivel de bateria esta ronda: ");
  Serial.println(batteryLevels[NODE_ID]);

  // Leer DHT (porque soy nodo 1)
  float currentTemp = dht.readTemperature();
  float currentHum  = dht.readHumidity();

  // Intercambio de baterías (igual que antes)
  unsigned long batteryExchangeStart = millis();
  unsigned long batterySendDelay = NODE_ID * 10000;
  bool batterySent = false;
  bool chSelected = false;
  unsigned long memberCheckTime = 60000;

  while (millis() - batteryExchangeStart < memberCheckTime && !chSelected) {
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String inc = rc.data;
      processReceivedMessage(inc);
      inc.trim();
      Serial.print("Mensaje recibido como miembro: '");
      Serial.print(inc);
      Serial.println("'");
      
      if (inc == "¿Hay cluster-head?") {
        e220ttl.sendBroadcastFixedMessage(23, "Proceso ya inicio\n");
        Serial.println("Miembro responde: Proceso ya inicio");
      }
      if (currentState == CLUSTER_HEAD && inc == "¿Hay cluster-head?") {
        e220ttl.sendBroadcastFixedMessage(23, "Si hay cluster-head\n");
        Serial.println("Miembro responde: Si hay cluster-head");
      }
      if (inc.startsWith("Batt:")) {
        int c1 = inc.indexOf(':',5);
        if (c1 != -1) {
          uint8_t bID = inc.substring(5,c1).toInt();
          float bVal = inc.substring(c1+1).toFloat();
          if (bID >=1 && bID <=3) {
            batteryLevels[bID] = bVal;
            Serial.print("Bateria recibida de nodo ");
            Serial.print(bID);
            Serial.print(": ");
            Serial.println(bVal);
          }
        }
      }
    }

    // Enviar la batería una sola vez
    if (!batterySent && (millis() - batteryExchangeStart >= batterySendDelay)) {
      String battMsg = "Batt:" + String(NODE_ID) + ":" + String(batteryLevels[NODE_ID],1) + "\n";
      e220ttl.sendBroadcastFixedMessage(23, battMsg);
      Serial.print("Miembro envia nivel de batería: ");
      Serial.println(battMsg);
      batterySent = true;
    }
    delay(10);
  }

  // Elegir CH por mayor batería
  float maxVal = -1.0;
  int maxID = -1;
  for (int i=1; i<=TOTAL_NODES; i++) {
    if (batteryLevels[i] > maxVal || 
       (batteryLevels[i] == maxVal && (maxID == -1 || i < maxID))) {
      maxVal = batteryLevels[i];
      maxID = i;
    }
  }

  if (maxID == NODE_ID) {
    Serial.println("Miembro se convierte en Cluster Head.");
    currentState = CLUSTER_HEAD;
  } else {
    Serial.println("No soy CH, intentando membresía...");
    int fibMem[5] = {1, 1, 2, 3, 5};
    int fibIndexMem = 0;
    unsigned long lastMemSend = millis();
    bool miembroConfirmado = false;
    bool finRonda = false;

    // Enviar solicitudes de membresía
    while(!finRonda) {
      if (!miembroConfirmado) {
        unsigned long intervalMem = (unsigned long)(fibMem[fibIndexMem] * 1000);
        if (millis() - lastMemSend >= intervalMem) {
          String memberMsg = "Yo sere tu miembro:" + String(NODE_ID) + "\n";
          e220ttl.sendBroadcastFixedMessage(23, memberMsg);
          Serial.print("Miembro envia solicitud de membresía: ");
          Serial.println(memberMsg);
          lastMemSend = millis();
          fibIndexMem++;
          if (fibIndexMem >= 5) fibIndexMem = 0;
        }
      }
      delay(100);
      ResponseContainer rc2 = e220ttl.receiveMessage();
      if (rc2.status.code == 1) {
        String msg2 = rc2.data;
        processReceivedMessage(msg2);
        msg2.trim();
        Serial.print("Miembro recibe mensaje: '");
        Serial.print(msg2);
        Serial.println("'");
  
        if (msg2 == "Envio de horario") {
          Serial.println("Miembro: Recibido 'Envio de horario', ya soy parte del cluster");
          unsigned long waitTime = (unsigned long)(NODE_ID * 10000); 
          Serial.print("Miembro esperando ");
          Serial.print(waitTime / 1000);
          Serial.println(" s antes de enviar Temp/Hum...");
          delay(waitTime);

          // Enviar Temp/Hum
          String tempMsg = "Temp:" + String(NODE_ID) + ":" + String(currentTemp,1) + "\n";
          e220ttl.sendBroadcastFixedMessage(23, tempMsg);
          Serial.print("Miembro envia su temperatura: ");
          Serial.println(tempMsg);

          String humMsg = "Hum:" + String(NODE_ID) + ":" + String(currentHum,1) + "\n";
          e220ttl.sendBroadcastFixedMessage(23, humMsg);
          Serial.print("Miembro envia su humedad: ");
          Serial.println(humMsg);
        } 
        else if (msg2.startsWith("Miembro confirmado:")) {
          int colonPos = msg2.indexOf(':');
          if(colonPos != -1) {
            int confirmedID = msg2.substring(colonPos+1).toInt();
            if (confirmedID == NODE_ID) {
              miembroConfirmado = true;
              Serial.println("Miembro: Recibida confirmación del cluster-head, deteniendo envíos de membresía");
            }
          }
        } 
        else if (msg2 == "Termine mi ronda") {
          finRonda = true;
          Serial.println("Miembro: Fin de ronda antes de horario o nuevo CH detectado.");
        }
      }
    }

    // Esperar a recibir "Termine mi ronda"
    while(!finRonda) {
      delay(100);
      ResponseContainer rc3 = e220ttl.receiveMessage();
      if (rc3.status.code == 1) {
        String msg3 = rc3.data;
        processReceivedMessage(msg3);
        msg3.trim();
        Serial.print("Miembro recibe mensaje mientras espera terminar ronda: '");
        Serial.print(msg3);
        Serial.println("'");
  
        if (msg3 == "Termine mi ronda") {
          finRonda = true;
          Serial.println("Miembro: Fin de ronda antes de horario o nuevo CH detectado.");
        }
      }
    }
    if (finRonda) {
      Serial.println("Miembro: Iniciando nueva ronda.");
      currentState = SEARCHING_CH;
    }
  }
}

// ------------------------------------------------------------
//  PROCESAR MENSAJES RECIBIDOS
// ------------------------------------------------------------
void processReceivedMessage(String incoming) {
  int delimiterPos;
  while ((delimiterPos = incoming.indexOf('\n')) != -1) {
    String singleMsg = incoming.substring(0, delimiterPos);
    Serial.println("Procesando mensaje: " + singleMsg);
    incoming = incoming.substring(delimiterPos + 1);
    singleMsg.trim();

    // Chequeo de "Temp:", "Hum:", "Soil:", "Rain:"
    if (singleMsg.startsWith("Temp:")) {
      int c1 = singleMsg.indexOf(':', 5);
      if (c1 != -1) {
        uint8_t tID = singleMsg.substring(5, c1).toInt();
        float tVal = singleMsg.substring(c1 + 1).toFloat();
        if (tID >=1 && tID <=3) {
          nodeTemp[tID] = tVal;
          Serial.print("Temperatura recibida de nodo ");
          Serial.println(tID);
        }
      }
    }
    else if (singleMsg.startsWith("Hum:")) {
      int c1 = singleMsg.indexOf(':', 4);
      if (c1 != -1) {
        uint8_t tID = singleMsg.substring(4, c1).toInt();
        float tVal = singleMsg.substring(c1 + 1).toFloat();
        if (tID >=1 && tID <=3) {
          nodeHum[tID] = tVal;
          Serial.print("Humedad recibida de nodo ");
          Serial.println(tID);
        }
      }
    }
    else if (singleMsg.startsWith("Soil:")) {
      int c1 = singleMsg.indexOf(':', 5);
      if (c1 != -1) {
        uint8_t tID = singleMsg.substring(5, c1).toInt();
        float tVal = singleMsg.substring(c1 + 1).toFloat();
        if (tID >=1 && tID <=3) {
          nodeSoil[tID] = tVal;
          Serial.print("Soil moisture recibido de nodo ");
          Serial.println(tID);
        }
      }
    }
    else if (singleMsg.startsWith("Rain:")) {
      int c1 = singleMsg.indexOf(':', 5);
      if (c1 != -1) {
        uint8_t tID = singleMsg.substring(5, c1).toInt();
        float tVal = singleMsg.substring(c1 + 1).toFloat();
        if (tID >=1 && tID <=3) {
          nodeRain[tID] = tVal;
          Serial.print("Rain sensor recibido de nodo ");
          Serial.println(tID);
        }
      }
    }
  }
}
