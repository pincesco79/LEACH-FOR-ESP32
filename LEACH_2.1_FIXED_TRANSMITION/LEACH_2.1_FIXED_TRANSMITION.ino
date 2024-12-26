#include "Arduino.h"
#include "LoRa_E220.h"

// Definir estados del nodo
enum NodeState {
  SEARCHING_CH,
  CLUSTER_HEAD,
  MEMBER,
  WAITING_COOLDOWN
};

// Ajustar este ID único para cada nodo: 1, 2 o 3
const uint8_t NODE_ID = 1; 
const uint8_t TOTAL_NODES = 3;

// Configuración de pines para el ESP32 y LoRa
LoRa_E220 e220ttl(17, 16, &Serial2, 15, 21, 19, UART_BPS_RATE_9600);

// Definir el pin del LED
const int ledPin = 23;

// Variables de estado
NodeState currentState = SEARCHING_CH;

// Variables de batería y temperatura
float batteryLevelFloat;  
unsigned long lastClusterHeadTime = 0; 
const unsigned long cooldownTime = 60000; // 60 segundos
unsigned long memberCheckTime = 25000;

// Arrays para almacenar estados y niveles
bool membersConfirmed[TOTAL_NODES + 1] = {false, false, false, false};
float nodeTemp[TOTAL_NODES + 1] = {0.0, 0.0, 0.0, 0.0};
float batteryLevels[TOTAL_NODES + 1] = {0.0, 0.0, 0.0, 0.0};

// Indica si debe esperar al siguiente Cluster Head
bool mustWaitForNextCH = false; 

void performSearchClusterHead();
void actAsClusterHead();
void actAsMember();
void processReceivedMessage(String incoming);

void setup() {
  Serial.begin(9600);
  delay(2000);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  if (!e220ttl.begin()) { // Corregir la condición
    Serial.println("Error inicializando el módulo E220");
    while (1);
  }
  Serial.print("Módulo E220 inicializado correctamente. Nodo ID: ");
  Serial.println(NODE_ID);

  randomSeed(analogRead(0) * NODE_ID);

  // Inicio en estado de búsqueda de Cluster Head
  currentState = SEARCHING_CH;
}

void loop() {
  unsigned long currentMillis = millis();

  switch (currentState) {
    case SEARCHING_CH:
      performSearchClusterHead();
      break;

    case CLUSTER_HEAD:
      actAsClusterHead();
      break;

    case MEMBER:
      actAsMember();
      break;

    case WAITING_COOLDOWN:
      if (currentMillis - lastClusterHeadTime >= cooldownTime) {
        Serial.println("Periodo de enfriamiento terminado. Iniciando nueva ronda.");
        currentState = SEARCHING_CH;
      }
      break;
  }
}

void performSearchClusterHead() {
  Serial.println("=== BUSQUEDA DE CLUSTER-HEAD (FASE INICIAL) ===");
  unsigned long startSearchTime = millis();
  bool chFound = false;  

  // Secuencia Fibonacci fija para búsquedas: [1, 1, 2, 3, 5]
  int fib[5] = {1, 1, 2, 3, 5};
  int fibIndex = 0;
  unsigned long lastSend = 0;

  while (millis() - startSearchTime < 60000 && !chFound) { // 1 min buscando CH
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String incoming = rc.data;
      processReceivedMessage(incoming);
      incoming.trim(); // Eliminar espacios en blanco y caracteres de control
      Serial.print("Mensaje recibido durante búsqueda CH: '");
      Serial.print(incoming);
      Serial.println("'");

      // Detectar la existencia de un Cluster Head
      if (incoming == "Si hay cluster-head" || incoming == "Yo soy el Cluster Head") {
        chFound = true;
        mustWaitForNextCH = true;
        Serial.println("Cluster Head detectado.");
        break; 
      }
    }

    unsigned long interval = (unsigned long)(fib[fibIndex] * 1000); // Intervalo en ms
    if (millis() - lastSend >= interval) {
      e220ttl.sendBroadcastFixedMessage(23, "¿Hay cluster-head?\n"); // Aquí
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
        msg2.trim(); // Eliminar espacios en blanco y caracteres de control
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

  // Proceder al intercambio de baterías
  currentState = MEMBER;
}

void actAsClusterHead() {
  Serial.println("Soy Cluster Head elegido por batería");
  digitalWrite(ledPin, HIGH);

  e220ttl.sendBroadcastFixedMessage(23, "Yo soy el Cluster Head\n"); // Aquí
  Serial.println("CH envia: Yo soy el Cluster Head");
  
  delay(2000); 

  for(int i = 1; i <= TOTAL_NODES; i++) {
    membersConfirmed[i] = false;
    nodeTemp[i] = 0.0;
  }

  Serial.println("CH escuchando membresías...");
  unsigned long startTime = millis();
  int memberCount = 0; 
  while (millis() - startTime < memberCheckTime) {
    delay(10); 
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String msg = rc.data;
      processReceivedMessage(msg);
      msg.trim(); // Eliminar espacios en blanco y caracteres de control
      Serial.print("Mensaje recibido en CH: '");
      Serial.print(msg);
      Serial.println("'");

      if (msg == "¿Hay cluster-head?") {
        e220ttl.sendBroadcastFixedMessage(23, "Si hay cluster-head\n"); // Aquí
        Serial.println("CH responde: Si hay cluster-head");
      }

      if (msg.startsWith("Yo sere tu miembro:")) {
        uint8_t memberID = msg.substring(19).toInt();
        if (memberID >=1 && memberID <=3 && memberID != NODE_ID) {
          if(!membersConfirmed[memberID]){
            membersConfirmed[memberID] = true;
            memberCount++;
            Serial.print("Miembro confirmado: Nodo ");
            Serial.println(memberID);
            
            // Añadir un pequeño delay antes de enviar la confirmación
            delay(1000); // 100 ms
            
            // Enviar confirmación al miembro
            e220ttl.sendBroadcastFixedMessage(23, "Miembro confirmado:" + String(memberID) + "\n"); // Aquí
            Serial.print("CH confirma miembro: Nodo ");
            Serial.println(memberID);
          }
        }
      }
    }
  }

  
  lastClusterHeadTime = millis(); // Registrar el tiempo actual como última vez que fue CH

  delay(2000); 
  
  // Enviar "Envio de horario" 5 veces (cada 1 segundo)
  for (int i = 0; i < 5; i++) {
    e220ttl.sendBroadcastFixedMessage(23, "Envio de horario\n"); // Aquí
    Serial.println("CH envia: Envio de horario");
    delay(1000);
  }

  // Escucha de temperaturas
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
      incoming.trim(); // Eliminar espacios en blanco y caracteres de control

      if (incoming.startsWith("Temp:")) {
        int c1 = incoming.indexOf(':',5);
        if (c1 != -1) {
          uint8_t tID = incoming.substring(5,c1).toInt();
          float tVal = incoming.substring(c1+1).toFloat();
          if (tID >=1 && tID <=3 && tID != NODE_ID) {
            nodeTemp[tID] = tVal;
            Serial.print("Temperatura recibida de nodo ");
            Serial.println(tID);
          }
        }
      }
    }
  }

  // CH asume su propia temperatura simulada
  float myTemp = random(200,301) / 10.0; 
  nodeTemp[NODE_ID] = myTemp;
  Serial.print("Mi temp (CH): ");
  Serial.println(myTemp);

  // Imprimir datos por consola
  Serial.println("=== Datos finales de la ronda ===");
  for (int i=1; i<=TOTAL_NODES; i++) {
    Serial.print("Nodo ");
    Serial.print(i);
    Serial.print(": Temperatura = ");
    Serial.println(nodeTemp[i]);
  }
  
  delay(2000); 
  e220ttl.sendBroadcastFixedMessage(23, "Termine mi ronda\n"); // Aquí
  Serial.println("CH envia: Termine mi ronda");
  digitalWrite(ledPin, LOW);
  delay(1000); 

  // Cambiar estado a periodo de enfriamiento
  currentState = WAITING_COOLDOWN;
}

void actAsMember() {
    // Generar batería aleatoria (80.0 a 100.0)
    int rawBattery = random(800,1001); 
    batteryLevelFloat = rawBattery / 10.0; 
    batteryLevels[NODE_ID] = batteryLevelFloat;
    Serial.print("Mi nivel de bateria esta ronda: ");
    Serial.println(batteryLevelFloat);

    // Variables para el nuevo mecanismo de envío de batería
    unsigned long batteryExchangeStart = millis();
    unsigned long batterySendDelay = NODE_ID * 10000; // ID * 10,000 ms = ID * 10 segundos
    bool batterySent = false; // Flag para asegurar envío único

    bool chSelected = false;
    unsigned long memberCheckTime = 60000; // 1 minuto intercambio de baterías

    // Intercambio de baterías durante 1 minuto
    while (millis() - batteryExchangeStart < memberCheckTime && !chSelected) { // 1 min intercambio de baterías
        ResponseContainer rc = e220ttl.receiveMessage();
        if (rc.status.code == 1) {
            String inc = rc.data;
            processReceivedMessage(inc);
            inc.trim(); // Eliminar espacios en blanco y caracteres de control
            Serial.print("Mensaje recibido como miembro: '");
            Serial.print(inc);
            Serial.println("'");
    
            if (inc == "¿Hay cluster-head?") {
                e220ttl.sendBroadcastFixedMessage(23, "Proceso ya inicio\n"); // Aquí
                Serial.println("Miembro responde: Proceso ya inicio");
            }

            if (currentState == CLUSTER_HEAD && inc == "¿Hay cluster-head?") {
                e220ttl.sendBroadcastFixedMessage(23, "Si hay cluster-head\n"); // Aquí
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

        // Nuevo mecanismo para enviar el nivel de batería una sola vez después de ID * 10 segundos
        if (!batterySent && (millis() - batteryExchangeStart >= batterySendDelay)) {
            String battMsg = "Batt:" + String(NODE_ID) + ":" + String(batteryLevelFloat,1) + "\n"; // Añadir delimitador
            e220ttl.sendBroadcastFixedMessage(23, battMsg); // Aquí
            Serial.print("Miembro envia nivel de batería: ");
            Serial.println(battMsg);
            batterySent = true; // Marcar que el nivel de batería ya fue enviado
        }

        delay(10); // Pequeña pausa para evitar saturar el bucle
    }

    if (!chSelected) {
        // Determinar CH con el mayor nivel de batería
        float maxVal = -1.0;
        int maxID = -1;
        for (int i=1; i<=TOTAL_NODES; i++) {
            if (batteryLevels[i] > maxVal || (batteryLevels[i] == maxVal && (maxID == -1 || i < maxID))) { // Criterio de desempate
                maxVal = batteryLevels[i];
                maxID = i;
            }
        }

        if (maxID == NODE_ID) {
            // Yo soy CH
            Serial.println("Miembro se convierte en Cluster Head.");
            currentState = CLUSTER_HEAD;
        } else {
            // No soy CH, intentar unirme como miembro
            Serial.println("No soy CH, intentando membresía...");
            int fibMem[5] = {1, 1, 2, 3, 5}; // Secuencia Fibonacci fija para membresías
            int fibIndexMem = 0;
            unsigned long lastMemSend = millis();
            bool miembroConfirmado = false;
            bool finRonda = false;

            // Enviar solicitudes de membresía hasta recibir "Termine mi ronda"
            while(!finRonda) {
                if (!miembroConfirmado) {
                    unsigned long intervalMem = (unsigned long)(fibMem[fibIndexMem] * 1000);
                    if (millis() - lastMemSend >= intervalMem) {
                        String memberMsg = "Yo sere tu miembro:" + String(NODE_ID) + "\n"; // Añadir delimitador
                        e220ttl.sendBroadcastFixedMessage(23, memberMsg); // Aquí
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
                    msg2.trim(); // Eliminar espacios en blanco y caracteres de control
                    Serial.print("Miembro recibe mensaje: '");
                    Serial.print(msg2);
                    Serial.println("'");
    
                    if (msg2 == "Envio de horario") { // Recepción de "Envio de horario"
                        Serial.println("Miembro: Recibido 'Envio de horario', ya soy parte del cluster");
                        // Esperar 10 segundos antes de enviar temperatura
                        unsigned long waitTime = (unsigned long)(NODE_ID * 10000); 
                        Serial.print("Miembro esperando ");
                        Serial.print(waitTime / 1000);
                        Serial.println(" s antes de enviar temperatura...");
                        delay(waitTime);
    
                        float myTemp = random(200,301) / 10.0; 
                        String tempMsg = "Temp:" + String(NODE_ID) + ":" + String(myTemp,1) + "\n"; // Añadir delimitador
                        e220ttl.sendBroadcastFixedMessage(23, tempMsg); // Aquí
                        Serial.print("Miembro envia su temperatura: ");
                        Serial.println(tempMsg);
                    } 
                    else if (msg2.startsWith("Miembro confirmado:")) { // Confirmación de membresía
                        int colonPos = msg2.indexOf(':');
                        if(colonPos != -1) {
                            int confirmedID = msg2.substring(colonPos+1).toInt();
                            if (confirmedID == NODE_ID) {
                                miembroConfirmado = true;
                                Serial.println("Miembro: Recibida confirmación del cluster-head, deteniendo envíos de membresía");
                            }
                        }
                    } 
                    else if (msg2 == "Termine mi ronda") { // Recepción de "Termine mi ronda"
                        finRonda = true;
                        Serial.println("Miembro: Fin de ronda antes de horario o nuevo CH detectado.");
                    }
                }
            }

            // Esperar a recibir "Termine mi ronda" antes de iniciar nueva ronda
            while(!finRonda) {
                delay(100);
                ResponseContainer rc3 = e220ttl.receiveMessage();
                if (rc3.status.code == 1) {
                    String msg3 = rc3.data;
                    processReceivedMessage(msg3);
                    msg3.trim(); // Eliminar espacios en blanco y caracteres de control
                    Serial.print("Miembro recibe mensaje mientras espera terminar ronda: '");
                    Serial.print(msg3);
                    Serial.println("'");
    
                    if (msg3 == "Termine mi ronda") { // Recepción de "Termine mi ronda"
                        finRonda = true;
                        Serial.println("Miembro: Fin de ronda antes de horario o nuevo CH detectado.");
                    }
                }
            }

            // Cambiar estado a SEARCHING_CH solo después de recibir "Termine mi ronda"
            if (finRonda) {
                Serial.println("Miembro: Iniciando nueva ronda.");
                currentState = SEARCHING_CH;
            }
        }

    } else {
        // chSelected = true (Otro CH o fin de ronda antes de tiempo)
        // Soy miembro
        Serial.println("Soy miembro, CH ya fue seleccionado en intercambio, intentando membresía...");
        int fibMem[5] = {1, 1, 2, 3, 5}; // Secuencia Fibonacci fija para membresías
        int fibIndexMem = 0;
        unsigned long lastMemSend = millis();
        bool miembroConfirmado = false;
        bool finRonda = false;

        // Enviar solicitudes de membresía hasta recibir "Termine mi ronda"
        while(!finRonda) {
            if (!miembroConfirmado) {
                unsigned long intervalMem = (unsigned long)(fibMem[fibIndexMem] * 1000);
                if (millis() - lastMemSend >= intervalMem) {
                    String memberMsg = "Yo sere tu miembro:" + String(NODE_ID) + "\n"; // Añadir delimitador
                    e220ttl.sendBroadcastFixedMessage(23, memberMsg); // Aquí
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
                msg2.trim(); // Eliminar espacios en blanco y caracteres de control
                Serial.print("Miembro recibe mensaje: '");
                Serial.print(msg2);
                Serial.println("'");
    
                if (msg2 == "Envio de horario") { // Recepción de "Envio de horario"
                    Serial.println("Miembro: Recibido 'Envio de horario', ya soy parte del cluster");
                    // Esperar 10 segundos antes de enviar temperatura
                    unsigned long waitTime = (unsigned long)(NODE_ID * 10000); 
                    Serial.print("Miembro esperando ");
                    Serial.print(waitTime / 1000);
                    Serial.println(" s antes de enviar temperatura...");
                    delay(waitTime);
    
                    float myTemp = random(200,301) / 10.0; 
                    String tempMsg = "Temp:" + String(NODE_ID) + ":" + String(myTemp,1) + "\n"; // Añadir delimitador
                    e220ttl.sendBroadcastFixedMessage(23, tempMsg); // Aquí
                    Serial.print("Miembro envia su temperatura: ");
                    Serial.println(tempMsg);
                } 
                else if (msg2.startsWith("Miembro confirmado:")) { // Confirmación de membresía
                    int colonPos = msg2.indexOf(':');
                    if(colonPos != -1) {
                        int confirmedID = msg2.substring(colonPos+1).toInt();
                        if (confirmedID == NODE_ID) {
                            miembroConfirmado = true;
                            Serial.println("Miembro: Recibida confirmación del cluster-head, deteniendo envíos de membresía");
                        }
                    }
                } 
                else if (msg2 == "Termine mi ronda") { // Recepción de "Termine mi ronda"
                    finRonda = true;
                    Serial.println("Miembro: Fin de ronda antes de horario o nuevo CH detectado.");
                }
            }
        }

        // Esperar a recibir "Termine mi ronda" antes de iniciar nueva ronda
        while(!finRonda) {
            delay(100);
            ResponseContainer rc3 = e220ttl.receiveMessage();
            if (rc3.status.code == 1) {
                String msg3 = rc3.data;
                processReceivedMessage(msg3);
                msg3.trim(); // Eliminar espacios en blanco y caracteres de control
                Serial.print("Miembro recibe mensaje mientras espera terminar ronda: '");
                Serial.print(msg3);
                Serial.println("'");
    
                if (msg3 == "Termine mi ronda") { // Recepción de "Termine mi ronda"
                    finRonda = true;
                    Serial.println("Miembro: Fin de ronda antes de horario o nuevo CH detectado.");
                }
            }
        }

        // Cambiar estado a SEARCHING_CH solo después de recibir "Termine mi ronda"
        if (finRonda) {
            Serial.println("Miembro: Iniciando nueva ronda.");
            currentState = SEARCHING_CH;
        }
    }
}

void processReceivedMessage(String incoming) {
    // Implementar cualquier procesamiento adicional de mensajes aquí
    // Por ejemplo, separar mensajes si se reciben concatenados
    int delimiterPos;
    while ((delimiterPos = incoming.indexOf('\n')) != -1) {
        String singleMsg = incoming.substring(0, delimiterPos);
        Serial.println("Procesando mensaje: " + singleMsg);
        incoming = incoming.substring(delimiterPos + 1);
        // Aquí puedes agregar más lógica si es necesario
    }
}
