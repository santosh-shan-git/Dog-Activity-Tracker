#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <time.h>
#include <Preferences.h>

const char* ssid = "bahamas";
const char* password = "Agnew2!8!";

Adafruit_MPU6050 mpu;
WebServer server(80);
Preferences prefs;

// Timezone for Eastern Time
const char* tzInfo = "EST5EDT,M3.2.0/2,M11.1.0/2";

// Step counters
long stepsToday = 0;
long stepsYesterday = 0;
long stepsThisWeek = 0;
long stepsLastWeek = 0;
long stepsThisMonth = 0;
long stepsLastMonth = 0;

// Date tracking
int currentDayKey = -1;
int currentWeekKey = -1;
int currentMonthKey = -1;

float previousMagnitude = 0;
unsigned long lastStepTime = 0;
unsigned long lastSaveTime = 0;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("Disconnected. Reason = ");
    Serial.println(info.wifi_sta_disconnected.reason);
  }
}

void saveCounts() {
  prefs.putLong("today", stepsToday);
  prefs.putLong("yday", stepsYesterday);
  prefs.putLong("week", stepsThisWeek);
  prefs.putLong("lweek", stepsLastWeek);
  prefs.putLong("month", stepsThisMonth);
  prefs.putLong("lmonth", stepsLastMonth);

  prefs.putInt("dayKey", currentDayKey);
  prefs.putInt("weekKey", currentWeekKey);
  prefs.putInt("monKey", currentMonthKey);

  lastSaveTime = millis();
}

void loadCounts() {
  prefs.begin("dogtrack", false);

  stepsToday = prefs.getLong("today", 0);
  stepsYesterday = prefs.getLong("yday", 0);
  stepsThisWeek = prefs.getLong("week", 0);
  stepsLastWeek = prefs.getLong("lweek", 0);
  stepsThisMonth = prefs.getLong("month", 0);
  stepsLastMonth = prefs.getLong("lmonth", 0);

  currentDayKey = prefs.getInt("dayKey", -1);
  currentWeekKey = prefs.getInt("weekKey", -1);
  currentMonthKey = prefs.getInt("monKey", -1);
}

int getDayKey(struct tm timeinfo) {
  return (timeinfo.tm_year + 1900) * 1000 + timeinfo.tm_yday;
}

int getWeekKey(struct tm timeinfo) {
  char weekString[10];
  strftime(weekString, sizeof(weekString), "%Y%U", &timeinfo);
  return atoi(weekString);
}

int getMonthKey(struct tm timeinfo) {
  return (timeinfo.tm_year + 1900) * 100 + (timeinfo.tm_mon + 1);
}

void updateTimePeriods() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return;
  }

  int newDayKey = getDayKey(timeinfo);
  int newWeekKey = getWeekKey(timeinfo);
  int newMonthKey = getMonthKey(timeinfo);

  bool changed = false;

  if (currentDayKey == -1) {
    currentDayKey = newDayKey;
    changed = true;
  }

  if (currentWeekKey == -1) {
    currentWeekKey = newWeekKey;
    changed = true;
  }

  if (currentMonthKey == -1) {
    currentMonthKey = newMonthKey;
    changed = true;
  }

  if (newDayKey != currentDayKey) {
    stepsYesterday = stepsToday;
    stepsToday = 0;
    currentDayKey = newDayKey;
    changed = true;
  }

  if (newWeekKey != currentWeekKey) {
    stepsLastWeek = stepsThisWeek;
    stepsThisWeek = 0;
    currentWeekKey = newWeekKey;
    changed = true;
  }

  if (newMonthKey != currentMonthKey) {
    stepsLastMonth = stepsThisMonth;
    stepsThisMonth = 0;
    currentMonthKey = newMonthKey;
    changed = true;
  }

  if (changed) {
    saveCounts();
  }
}

void setupTime() {
  configTzTime(tzInfo, "pool.ntp.org", "time.nist.gov");

  Serial.print("Syncing time");

  struct tm timeinfo;
  int tries = 0;

  while (!getLocalTime(&timeinfo, 500) && tries < 20) {
    Serial.print(".");
    tries++;
  }

  if (tries < 20) {
    Serial.println();
    Serial.println("Time synced");
  } else {
    Serial.println();
    Serial.println("Time sync failed, but tracker will still run");
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
  font-family: Arial, sans-serif;
  text-align: center;
  margin-top: 40px;
  background: #dff5e1;
  color: #064d19;
}

h1 {
  color: #0b6b2b;
}

.container {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 20px;
}

.card {
  background: #ffffff;
  padding: 20px;
  border-radius: 15px;
  width: 260px;
  box-shadow: 0px 0px 12px rgba(0, 80, 20, 0.25);
  border: 2px solid #4caf50;
}

.big-number {
  font-size: 46px;
  font-weight: bold;
  color: #0b8f36;
}

.label {
  font-size: 20px;
  color: #176b2c;
}
</style>
</head>

<body>

<h1>Dog Activity Tracker</h1>

<div class="container">

<div class="card">
  <div class="label">Steps Today</div>
  <div class="big-number">
)rawliteral";

  page += String(stepsToday);

  page += R"rawliteral(
  </div>
</div>

<div class="card">
  <div class="label">Steps Yesterday</div>
  <div class="big-number">
)rawliteral";

  page += String(stepsYesterday);

  page += R"rawliteral(
  </div>
</div>

<div class="card">
  <div class="label">Steps Last Week</div>
  <div class="big-number">
)rawliteral";

  page += String(stepsLastWeek);

  page += R"rawliteral(
  </div>
</div>

<div class="card">
  <div class="label">Steps Last Month</div>
  <div class="big-number">
)rawliteral";

  page += String(stepsLastMonth);

  page += R"rawliteral(
  </div>
</div>

</div>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found");
    while (1);
  }

  loadCounts();

  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(" WiFi status = ");
    Serial.println(WiFi.status());
  }

  Serial.println();
  Serial.println("Connected");

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  setupTime();
  updateTimePeriods();

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  Serial.println("Starting server...");
  server.begin();
  Serial.println("Server started");
}

void loop() {
  server.handleClient();

  updateTimePeriods();

  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float ax = accel.acceleration.x;
  float ay = accel.acceleration.y;
  float az = accel.acceleration.z;

  float magnitude = sqrt(ax * ax + ay * ay + az * az);
  float delta = abs(magnitude - previousMagnitude);

  if (delta > 2.0) {
    if (millis() - lastStepTime > 300) {
      stepsToday++;
      stepsThisWeek++;
      stepsThisMonth++;

      lastStepTime = millis();

      Serial.print("Steps Today: ");
      Serial.println(stepsToday);

      if (millis() - lastSaveTime > 10000) {
        saveCounts();
      }
    }
  }

  previousMagnitude = magnitude;

  delay(20);
}