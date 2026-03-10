// ESP32 UART Transmitter (Arduino)
// TX/RX pins can be changed; pick safe GPIOs for your board.
#include <Arduino.h>

static const int UART_TX = D6;
static const int UART_RX = D7;   // not used here, but required by begin() signature

HardwareSerial Link(1);

void setup() {
  Link.begin(9600, SERIAL_8N1, UART_RX, UART_TX); // RX, TX order [web:61]
  pinMode(A0, INPUT);         // ADC
}

void loop() {
  uint32_t Vbatt = 0;
  for(int i = 0; i < 16; i++) {
    Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction   
  }
  float Vbattf = 2 * Vbatt / 16 / 1000.0;     // attenuation ratio 1/2, mV --> V
  Link.println(Vbattf, 3);
  delay(1000);
}
