/***************************************************************
 * NODE 1: DHT22 (Temperature and Humidity)
 ***************************************************************/
#include "Arduino.h"
#include "LoRa_E220.h"
#include <Pangodream_18650_CL.h>
#include <DHT.h>

// ------------------- PINS AND DEFINITIONS -------------------
#define DHTPIN 2        // Pin where DHT22 is connected
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

// ------------------- STATES AND CONFIG -----------------------
enum NodeState {
  SEARCHING_CH,
  CLUSTER_HEAD,
  MEMBER,
  WAITING_COOLDOWN
};

const int dry = 2400;
const int wet = 900;

// IMPORTANT: NODE 1
const uint8_t NODE_ID = 1; 
const uint8_t TOTAL_NODES = 3;

// LoRa E220 (same as previous code)
LoRa_E220 e220ttl(17, 16, &Serial2, 15, 21, 19, UART_BPS_RATE_9600);
const int ledPin = 23;

NodeState currentState = SEARCHING_CH;
bool recentlyWasCH = false;

float batteryLevelFloat;
unsigned long lastClusterHeadTime = 0;
const unsigned long cooldownTime = 60000;
unsigned long memberCheckTime = 25000;

// ------------------- DATA ARRAYS ------------------------
bool membersConfirmed[TOTAL_NODES + 1] = {false, false, false, false};
// 1 => Temp/Hum at node1, 2 => Soil at node2, 3 => Rain at node3
float nodeTemp[TOTAL_NODES + 1] = {0,0,0,0};
float nodeHum[TOTAL_NODES + 1]  = {0,0,0,0};
float nodeSoil[TOTAL_NODES + 1] = {0,0,0,0};
float nodeRain[TOTAL_NODES + 1] = {0,0,0,0};

float batteryLevels[TOTAL_NODES + 1] = {0,0,0,0};
bool mustWaitForNextCH = false;

// ------------------- FUNCTION PROTOTYPES --------------------
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

  dht.begin();  // Initialize DHT

  if (!e220ttl.begin()) {
    Serial.println("Error initializing E220 module");
    while (1);
  }
  Serial.print("E220 module initialized successfully. Node ID: ");
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
    case MEMBER:         actAsMember();              break;
    case WAITING_COOLDOWN:
      if (currentMillis - lastClusterHeadTime >= cooldownTime) {
        Serial.println("Cooldown period ended. Starting a new round.");
        currentState = SEARCHING_CH;
      }
      break;
  }
}

// ------------------------------------------------------------
//  CLUSTER HEAD SEARCH
// ------------------------------------------------------------
void performSearchClusterHead() {
  Serial.println("=== CLUSTER-HEAD SEARCH (INITIAL PHASE) ===");
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
      Serial.print("Message received during CH search: '");
      Serial.print(incoming);
      Serial.println("'");

      if (incoming == "Yes, there is a cluster-head" || incoming == "I am the Cluster Head") {
        chFound = true;
        mustWaitForNextCH = true;
        Serial.println("Cluster Head detected.");
        break;
      }
    }

    unsigned long interval = (unsigned long)(fib[fibIndex] * 1000);
    if (millis() - lastSend >= interval) {
      e220ttl.sendBroadcastFixedMessage(23, "Is there a cluster-head?\n");
      Serial.println("Asking: Is there a cluster-head?");
      lastSend = millis();
      fibIndex++;
      if (fibIndex >= 5) fibIndex = 0;
    }
    delay(10);
  }

  if (mustWaitForNextCH) {
    Serial.println("CH already exists, waiting for next round...");
    bool nextCHfound = false;
    while (!nextCHfound) {
      ResponseContainer rc2 = e220ttl.receiveMessage();
      if (rc2.status.code == 1) {
        String msg2 = rc2.data;
        processReceivedMessage(msg2);
        msg2.trim();
        Serial.print("Message received while waiting for next CH: '");
        Serial.print(msg2);
        Serial.println("'");

        if (msg2 == "I am the Cluster Head") {
          nextCHfound = true;
          Serial.println("New CH detected, ready to join.");
        }
      }
      delay(10);
    }
  }

  Serial.println("End of cluster-head search phase.");
  delay(2000);
  currentState = MEMBER;
}

// ------------------------------------------------------------
//  ACT AS CLUSTER HEAD
// ------------------------------------------------------------
void actAsClusterHead() {
  Serial.println("I am the Cluster Head chosen by battery");
  digitalWrite(ledPin, HIGH);

  e220ttl.sendBroadcastFixedMessage(23, "I am the Cluster Head\n");
  Serial.println("CH sends: I am the Cluster Head");
  delay(2000);

  // Reset info
  for(int i = 1; i <= TOTAL_NODES; i++) {
    membersConfirmed[i] = false;
    nodeTemp[i] = 0.0;
    nodeHum[i]  = 0.0;
    nodeSoil[i] = 0.0;
    nodeRain[i] = 0.0;
  }

  Serial.println("CH listening for memberships...");
  unsigned long startTime = millis();
  while (millis() - startTime < memberCheckTime) {
    delay(10);
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String msg = rc.data;
      processReceivedMessage(msg);
      msg.trim();
      Serial.print("Message received at CH: '");
      Serial.print(msg);
      Serial.println("'");

      if (msg == "Is there a cluster-head?") {
        e220ttl.sendBroadcastFixedMessage(23, "Yes, there is a cluster-head\n");
        Serial.println("CH responds: Yes, there is a cluster-head");
      }
      if (msg.startsWith("IWillBeYourMember: ")) {
        uint8_t memberID = msg.substring(19).toInt();
        if (memberID >=1 && memberID <=3 && memberID != NODE_ID) {
          if(!membersConfirmed[memberID]){
            membersConfirmed[memberID] = true;
            Serial.print("Member confirmed: Node ");
            Serial.println(memberID);
            delay(1000);
            e220ttl.sendBroadcastFixedMessage(23, "Member confirmed:" + String(memberID) + "\n");
            Serial.print("CH confirms member: Node ");
            Serial.println(memberID);
          }
        }
      }
    }
  }

  // Mark the current time
  lastClusterHeadTime = millis();
  delay(2000);

  // Send "Sending schedule" 5 times
  for (int i = 0; i < 5; i++) {
    e220ttl.sendBroadcastFixedMessage(23, "Sending schedule\n");
    Serial.println("CH sends: Sending schedule");
    delay(1000);
  }

  // --- Read MY DHT sensor (Temp/Hum) ---
  float localT = dht.readTemperature();
  float localH = dht.readHumidity();
  nodeTemp[NODE_ID] = localT;
  nodeHum[NODE_ID]  = localH;

  // Wait 60s for other variables to arrive
  unsigned long scheduleStart = millis();
  unsigned long listenTime = 60000;
  while (millis() - scheduleStart < listenTime) {
    delay(10);
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String incoming = rc.data;
      processReceivedMessage(incoming);
      Serial.print("CH receives: ");
      Serial.println(incoming);
    }
  }

  // Now just print ONCE
  // nodeTemp[1], nodeHum[1], nodeSoil[2], nodeRain[3]
  Serial.println("=== Final round data ===");
  String finalData = "Temp=" + String(nodeTemp[1]) +
                     " | Hum=" + String(nodeHum[1]) +
                     " | Soil=" + String(nodeSoil[2]) +
                     " | Rain=" + String(nodeRain[3]);
  Serial.println("Collected data: " + finalData);

  // Send this data to a specific node (0x0002, channel 23)
  ResponseStatus rs = e220ttl.sendFixedMessage(0,2,23, finalData.c_str());
  Serial.println("Data sent to 0x0002 channel 23.");

  delay(2000);
  e220ttl.sendBroadcastFixedMessage(23, "I finished my round\n");
  Serial.println("CH sends: I finished my round");
  digitalWrite(ledPin, LOW);
  delay(1000);

  recentlyWasCH = true;
  currentState = WAITING_COOLDOWN;
}

// ------------------------------------------------------------
//  ACT AS MEMBER
// ------------------------------------------------------------
void actAsMember() {
  float voltageNow = battery.getBatteryVolts();
  if (recentlyWasCH) {
    Serial.println("Sending battery = 1 (Because I was just CH).");
    batteryLevels[NODE_ID] = 1;
    recentlyWasCH = false;
  } else {
    batteryLevels[NODE_ID] = voltageNow;
  }
  Serial.print("My battery level this round: ");
  Serial.println(batteryLevels[NODE_ID]);

  // Read DHT (because I'm node 1)
  float currentTemp = dht.readTemperature();
  float currentHum  = dht.readHumidity();

  // Battery exchange (same as before)
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
      Serial.print("Message received as member: '");
      Serial.print(inc);
      Serial.println("'");

      if (inc == "Is there a cluster-head?") {
        e220ttl.sendBroadcastFixedMessage(23, "Process has already started\n");
        Serial.println("Member replies: Process has already started");
      }
      if (currentState == CLUSTER_HEAD && inc == "Is there a cluster-head?") {
        e220ttl.sendBroadcastFixedMessage(23, "Yes, there is a cluster-head\n");
        Serial.println("Member replies: Yes, there is a cluster-head");
      }
      if (inc.startsWith("Batt:")) {
        int c1 = inc.indexOf(':',5);
        if (c1 != -1) {
          uint8_t bID = inc.substring(5,c1).toInt();
          float bVal = inc.substring(c1+1).toFloat();
          if (bID >=1 && bID <=3) {
            batteryLevels[bID] = bVal;
            Serial.print("Battery received from node ");
            Serial.print(bID);
            Serial.print(": ");
            Serial.println(bVal);
          }
        }
      }
    }

    // Send battery only once
    if (!batterySent && (millis() - batteryExchangeStart >= batterySendDelay)) {
      String battMsg = "Batt:" + String(NODE_ID) + ":" + String(batteryLevels[NODE_ID],1) + "\n";
      e220ttl.sendBroadcastFixedMessage(23, battMsg);
      Serial.print("Member sends battery level: ");
      Serial.println(battMsg);
      batterySent = true;
    }
    delay(10);
  }

  // Choose CH by highest battery
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
    Serial.println("Member becomes Cluster Head.");
    currentState = CLUSTER_HEAD;
  } else {
    Serial.println("I am not CH, trying membership...");
    int fibMem[5] = {1, 1, 2, 3, 5};
    int fibIndexMem = 0;
    unsigned long lastMemSend = millis();
    bool miembroConfirmado = false;
    bool finRonda = false;

    // Send membership requests
    while(!finRonda) {
      if (!miembroConfirmado) {
        unsigned long intervalMem = (unsigned long)(fibMem[fibIndexMem] * 1000);
        if (millis() - lastMemSend >= intervalMem) {
          String memberMsg = "IWillBeYourMember: " + String(NODE_ID) + "\n";
          e220ttl.sendBroadcastFixedMessage(23, memberMsg);
          Serial.print("Member sends membership request: ");
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
        Serial.print("Member receives message: '");
        Serial.print(msg2);
        Serial.println("'");

        if (msg2 == "Sending schedule") {
          miembroConfirmado = true;
          Serial.println("Member: Received 'Sending schedule', I'm now part of the cluster");
          unsigned long waitTime = (unsigned long)(NODE_ID * 10000);
          Serial.print("Member waiting ");
          Serial.print(waitTime / 1000);
          Serial.println(" s before sending Temp/Hum...");
          delay(waitTime);

          // Send Temp/Hum
          String tempMsg = "Temp:" + String(NODE_ID) + ":" + String(currentTemp,1) + "\n";
          e220ttl.sendBroadcastFixedMessage(23, tempMsg);
          Serial.print("Member sends its temperature: ");
          Serial.println(tempMsg);
          delay(500);
          String humMsg = "Hum:" + String(NODE_ID) + ":" + String(currentHum,1) + "\n";
          e220ttl.sendBroadcastFixedMessage(23, humMsg);
          Serial.print("Member sends its humidity: ");
          Serial.println(humMsg);

        } 
        else if (msg2.startsWith("Member confirmed:")) {
          int colonPos = msg2.indexOf(':');
          if(colonPos != -1) {
            int confirmedID = msg2.substring(colonPos+1).toInt();
            if (confirmedID == NODE_ID) {
              miembroConfirmado = true;
              Serial.println("Member: Confirmation from cluster-head received, stopping membership requests");
            }
          }
        }
        else if (msg2 == "I finished my round") {
          finRonda = true;
          Serial.println("Member: Round ended before schedule or new CH detected.");
        }
      }
    }

    // Wait to receive "I finished my round"
    while(!finRonda) {
      delay(100);
      ResponseContainer rc3 = e220ttl.receiveMessage();
      if (rc3.status.code == 1) {
        String msg3 = rc3.data;
        processReceivedMessage(msg3);
        msg3.trim();
        Serial.print("Member receives message while waiting to finish round: '");
        Serial.print(msg3);
        Serial.println("'");

        if (msg3 == "I finished my round") {
          finRonda = true;
          Serial.println("Member: Round ended before schedule or new CH detected.");
        }
      }
    }
    if (finRonda) {
      Serial.println("Member: Starting a new round.");
      currentState = SEARCHING_CH;
    }
  }
}

// ------------------------------------------------------------
//  PROCESS RECEIVED MESSAGES
// ------------------------------------------------------------
void processReceivedMessage(String incoming) {
  int delimiterPos;
  while ((delimiterPos = incoming.indexOf('\n')) != -1) {
    String singleMsg = incoming.substring(0, delimiterPos);
    Serial.println("Processing message: " + singleMsg);
    incoming = incoming.substring(delimiterPos + 1);
    singleMsg.trim();

    // Check "Temp:", "Hum:", "Soil:", "Rain:"
    if (singleMsg.startsWith("Temp:")) {
      int c1 = singleMsg.indexOf(':', 5);
      if (c1 != -1) {
        uint8_t tID = singleMsg.substring(5, c1).toInt();
        float tVal = singleMsg.substring(c1 + 1).toFloat();
        if (tID >=1 && tID <=3) {
          nodeTemp[tID] = tVal;
          Serial.print("Temperature received from node ");
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
          Serial.print("Humidity received from node ");
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
          Serial.print("Soil moisture received from node ");
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
          Serial.print("Rain sensor received from node ");
          Serial.println(tID);
        }
      }
    }
  }
}
