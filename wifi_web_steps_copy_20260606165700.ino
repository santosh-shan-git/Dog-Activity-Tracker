#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

const char* ssid = "bahamas";
const char* password = "Agnew2!8!";

Adafruit_MPU6050 mpu;
WebServer server(80);

long stepCount = 0;

float previousMagnitude = 0;
unsigned long lastStepTime = 0;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("Disconnected. Reason = ");
    Serial.println(info.wifi_sta_disconnected.reason);
  }
}

void handleNotFound() {
  Serial.print("NOT FOUND: ");
  Serial.println(server.uri());
  server.send(404, "text/plain", "Not found");
}

void handleRoot() {

Serial.println("Received request for /");
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta http-equiv='refresh' content='5'>
<title>Dog Tracker</title>
<style>
body {
  font-family: Arial;
  text-align: center;
  margin-top: 50px;
}
.card {
  padding: 20px;
  border-radius: 10px;
  width: 300px;
  margin: auto;
  box-shadow: 0px 0px 10px #ccc;
}
</style>
</head>
<body>

<div class="card">
<h1>Dog Activity</h1>
<h2>Steps Today</h2>
<p style="font-size:48px;">
)rawliteral";

  page += stepCount;

  page += R"rawliteral(
</p>
</div>

</body>
</html>
)rawliteral";

// server.send(
//     200,
//     "text/html",
//     "<html><body><h1>Dog Tracker</h1><p>Hello!</p></body></html>"
//   );
// //Serial.println(page.length());

server.send(200, "text/html", page);
}

void setup() {

  Serial.begin(115200);

  // WiFi.mode(WIFI_STA);
  // WiFi.disconnect();

  // delay(1000);

  // Wire.begin();

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found");
    while (1);
  }

  // Serial.println(WiFi.softAPIP());

  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    
    Serial.print("WiFi status = ");
    Serial.println(WiFi.status());
  }

  Serial.println();
  Serial.println("Connected");

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  // server.begin();
  Serial.println("Starting server...");
  server.begin();
  Serial.println("Server started");
}

void loop() {

  server.handleClient();

  sensors_event_t accel, gyro, temp;

  mpu.getEvent(&accel, &gyro, &temp);

  float ax = accel.acceleration.x;
  float ay = accel.acceleration.y;
  float az = accel.acceleration.z;

  float magnitude =
      sqrt(ax * ax + ay * ay + az * az);

  float delta = abs(magnitude - previousMagnitude);

  if (delta > 2.0) {

    if (millis() - lastStepTime > 300) {

      stepCount++;
      lastStepTime = millis();

      Serial.print("Steps: ");
      Serial.println(stepCount);
    }
  }

  previousMagnitude = magnitude;

  delay(20);
}