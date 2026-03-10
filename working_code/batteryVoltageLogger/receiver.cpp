// ESP32 UART Receiver (Arduino)
#include <Arduino.h>

static const int UART_TX = D6;  // not used here, but required by begin() signature
static const int UART_RX = D7;

HardwareSerial Link(1);

void setup() {
  Serial.begin(115200);  // USB serial console
  Link.begin(9600, SERIAL_8N1, UART_RX, UART_TX); // RX, TX order [web:61]
  Link.setTimeout(50);   // affects readStringUntil() timeout [web:68]
}

void loop() {
  if (Link.available()) {
    String s = Link.readStringUntil('\n'); // reads up to '\n' or timeout [web:68]
    Serial.print("Got: ");
    Serial.println(s);
  }
}
