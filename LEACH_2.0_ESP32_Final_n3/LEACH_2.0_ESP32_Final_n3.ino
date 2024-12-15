#include "Arduino.h"
#include "LoRa_E220.h"

// Define node states
enum NodeState {
  SEARCHING_CH,
  CLUSTER_HEAD,
  MEMBER,
  WAITING_COOLDOWN
};

// Adjust this unique ID for each node: 1, 2, or 3
const uint8_t NODE_ID = 2; 
const uint8_t TOTAL_NODES = 3;

// Pin configuration for ESP32 and LoRa
LoRa_E220 e220ttl(17, 16, &Serial2, 15, 21, 19, UART_BPS_RATE_9600);

// Define the LED pin
const int ledPin = 23;

// State variables
NodeState currentState = SEARCHING_CH;

// Battery and temperature variables
float batteryLevelFloat;  
unsigned long lastClusterHeadTime = 0; 
const unsigned long cooldownTime = 60000; // 60 seconds
unsigned long memberCheckTime = 25000;

// Arrays to store states and levels
bool membersConfirmed[TOTAL_NODES + 1] = {false, false, false, false};
float nodeTemp[TOTAL_NODES + 1] = {0.0, 0.0, 0.0, 0.0};
float batteryLevels[TOTAL_NODES + 1] = {0.0, 0.0, 0.0, 0.0};

// Indicates if it must wait for the next Cluster Head
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

  if (!e220ttl.begin()) { // Correct the condition
    Serial.println("Error initializing E220 module");
    while (1);
  }
  Serial.print("E220 module initialized successfully. Node ID: ");
  Serial.println(NODE_ID);

  randomSeed(analogRead(0) * NODE_ID);

  // Start in the searching cluster head state
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
        Serial.println("Cooldown period over. Starting a new round.");
        currentState = SEARCHING_CH;
      }
      break;
  }
}

void performSearchClusterHead() {
  Serial.println("=== SEARCHING FOR CLUSTER HEAD (INITIAL PHASE) ===");
  unsigned long startSearchTime = millis();
  bool chFound = false;  

  // Fixed Fibonacci sequence for searches: [1, 1, 2, 3, 5]
  int fib[5] = {1, 1, 2, 3, 5};
  int fibIndex = 0;
  unsigned long lastSend = 0;

  while (millis() - startSearchTime < 60000 && !chFound) { // 1 minute searching for CH
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String incoming = rc.data;
      processReceivedMessage(incoming);
      incoming.trim(); // Remove whitespace and control characters
      Serial.print("Message received during CH search: '");
      Serial.print(incoming);
      Serial.println("'");

      // Detect the existence of a Cluster Head
      if (incoming == "Cluster head exists" || incoming == "I am the Cluster Head") {
        chFound = true;
        mustWaitForNextCH = true;
        Serial.println("Cluster Head detected.");
        break; 
      }
    }

    unsigned long interval = (unsigned long)(fib[fibIndex] * 1000); // Interval in ms
    if (millis() - lastSend >= interval) {
      e220ttl.sendMessage("Is there a cluster head?\n"); // Add delimiter
      Serial.println("Asking: Is there a cluster head?");
      lastSend = millis();
      fibIndex++;
      if (fibIndex >= 5) fibIndex = 0;
    }

    delay(10);
  }

  if (mustWaitForNextCH) {
    Serial.println("CH already exists, waiting for the next round...");
    bool nextCHfound = false;
    while (!nextCHfound) {
      ResponseContainer rc2 = e220ttl.receiveMessage();
      if (rc2.status.code == 1) {
        String msg2 = rc2.data;
        processReceivedMessage(msg2);
        msg2.trim(); // Remove whitespace and control characters
        Serial.print("Message received while waiting for the next CH: '");
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

  Serial.println("End of cluster head search phase.");
  delay(2000);

  // Proceed to the battery exchange
  currentState = MEMBER;
}

void actAsClusterHead() {
  Serial.println("I am the Cluster Head chosen by battery");
  digitalWrite(ledPin, HIGH);

  e220ttl.sendMessage("I am the Cluster Head\n"); // Add delimiter
  Serial.println("CH sends: I am the Cluster Head");
  
  delay(2000); 

  for(int i = 1; i <= TOTAL_NODES; i++) {
    membersConfirmed[i] = false;
    nodeTemp[i] = 0.0;
  }

  Serial.println("CH listening for memberships...");
  unsigned long startTime = millis();
  int memberCount = 0; 
  while (millis() - startTime < memberCheckTime) {
    delay(10); 
    ResponseContainer rc = e220ttl.receiveMessage();
    if (rc.status.code == 1) {
      String msg = rc.data;
      processReceivedMessage(msg);
      msg.trim(); // Remove whitespace and control characters
      Serial.print("Message received in CH: '");
      Serial.print(msg);
      Serial.println("'");

      if (msg == "Is there a cluster head?") {
        e220ttl.sendMessage("Cluster head exists\n"); // Add delimiter
        Serial.println("CH responds: Cluster head exists");
      }

      if (msg.startsWith("I will be your member:")) {
        uint8_t memberID = msg.substring(21).toInt();
        if (memberID >= 1 && memberID <= 3 && memberID != NODE_ID) {
          if(!membersConfirmed[memberID]){
            membersConfirmed[memberID] = true;
            memberCount++;
            Serial.print("Member confirmed: Node ");
            Serial.println(memberID);
            
            // Add a small delay before sending the confirmation
            delay(1000); // 100 ms
            
            // Send confirmation to the member
            e220ttl.sendMessage("Member confirmed:" + String(memberID) + "\n"); // Add delimiter
            Serial.print("CH confirms member: Node ");
            Serial.println(memberID);
          }
        }
      }
    }
  }

  lastClusterHeadTime = millis(); // Register the current time as the last time it was CH

  delay(2000); 
  
  // Send "Schedule sent" 5 times (every 1 second)
  for (int i = 0; i < 5; i++) {
    e220ttl.sendMessage("Schedule sent\n"); // Add delimiter
    Serial.println("CH sends: Schedule sent");
    delay(1000);
  }

  // Listen for temperatures
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
      incoming.trim(); // Remove whitespace and control characters

      if (incoming.startsWith("Temp:")) {
        int c1 = incoming.indexOf(':',5);
        if (c1 != -1) {
          uint8_t tID = incoming.substring(5,c1).toInt();
          float tVal = incoming.substring(c1+1).toFloat();
          if (tID >= 1 && tID <= 3 && tID != NODE_ID) {
            nodeTemp[tID] = tVal;
            Serial.print("Temperature received from node ");
            Serial.println(tID);
          }
        }
      }
    }
  }

  // CH assumes its own simulated temperature
  float myTemp = random(200,301) / 10.0; 
  nodeTemp[NODE_ID] = myTemp;
  Serial.print("My temp (CH): ");
  Serial.println(myTemp);

  // Print data to the console
  Serial.println("=== Final data of the round ===");
  for (int i = 1; i <= TOTAL_NODES; i++) {
    Serial.print("Node ");
    Serial.print(i);
    Serial.print(": Temperature = ");
    Serial.println(nodeTemp[i]);
  }
  
  delay(2000); 
  e220ttl.sendMessage("Round finished\n"); // Add delimiter
  Serial.println("CH sends: Round finished");
  digitalWrite(ledPin, LOW);

  // Return to cooldown
  currentState = WAITING_COOLDOWN;
}

// Function for the MEMBER state
void actAsMember() {
  Serial.println("Joining a cluster as a MEMBER.");
  e220ttl.sendMessage("I will be your member:" + String(NODE_ID) + "\n");
  Serial.print("Sent: I will be your member:");
  Serial.println(NODE_ID);

  delay(2000);
  e220ttl.sendMessage("Temp:" + String(NODE_ID) + ":" + String(random(200, 301) / 10.0) + "\n");
  Serial.print("Sent: Temp:");
  Serial.println(NODE_ID);

  // Simulate some other member-specific operations here

  currentState = WAITING_COOLDOWN;
}

// Function to process received messages
void processReceivedMessage(String incoming) {
  // Add message-specific handling here if needed
}
