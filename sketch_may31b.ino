int messagecount = 0;

void setup() {
  // Configured to the ideal ESP32 baud rate
  Serial.begin(9600);

  // Brief delay to let the serial chip stabilize
  delay(1000);

  while (true) {
    messagecount++;
    Serial.printf("Boot sequence complete. Count: %d\n", messagecount);
    Serial.println("The program is now sitting in an idle state.");
    delay(1000);
    
    if (messagecount >= 10) {
      break;
    }
  }
}

void loop() {
  // Leaving this completely empty keeps the chip active
  // without looping any repeating text or messages.
}