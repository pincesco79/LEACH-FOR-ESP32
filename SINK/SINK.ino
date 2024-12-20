#include "Arduino.h"
#include "LoRa_E220.h"

// Configuración pines (ajustar si es necesario, deben ser iguales que el primer setup)
LoRa_E220 e220ttl(17, 16, &Serial2, 15, 21, 19, UART_BPS_RATE_9600);

void setup() {
  Serial.begin(9600);
  delay(2000);

  if (!e220ttl.begin()) {
    Serial.println("Error inicializando el módulo E220 (receptor)");
    while (1);
  }
  Serial.println("Receptor listo para recibir datos...");
}

void loop() {
  ResponseContainer rc = e220ttl.receiveMessage();
  if (rc.status.code == 1) {
    String incoming = rc.data;
    incoming.trim(); // Eliminar espacios en blanco y caracteres de control
    if (incoming.length() > 0) {
      Serial.print("Datos recibidos: ");
      Serial.println(incoming);
    }
  }
  delay(10);
}
