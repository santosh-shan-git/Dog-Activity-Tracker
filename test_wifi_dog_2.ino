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

// Eastern Time with daylight saving
const char* tzInfo = "EST5EDT,M3.2.0/2,M11.1.0/2";

long stepsToday = 0;

int currentYear = 0;
int currentMonth = 0;
int currentDay = 0;

float previousMagnitude = 0;
unsigned long lastStepTime = 0;
unsigned long lastSaveTime = 0;

const char* monthNames[] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};

const char* dayNames[] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("Disconnected. Reason = ");
    Serial.println(info.wifi_sta_disconnected.reason);
  }
}

String makeDateKey(int year, int month, int day) {
  char key[12];
  snprintf(key, sizeof(key), "d%04d%02d%02d", year, month, day);
  return String(key);
}

long getStepsForDate(int year, int month, int day) {
  String key = makeDateKey(year, month, day);
  return prefs.getLong(key.c_str(), 0);
}

void setStepsForDate(int year, int month, int day, long steps) {
  if (year <= 0 || month <= 0 || day <= 0) return;

  String key = makeDateKey(year, month, day);
  prefs.putLong(key.c_str(), steps);
}

bool isLeapYear(int year) {
  if (year % 400 == 0) return true;
  if (year % 100 == 0) return false;
  return year % 4 == 0;
}

int daysInMonth(int year, int month) {
  if (month == 2) {
    return isLeapYear(year) ? 29 : 28;
  }

  if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  }

  return 31;
}

void saveCounts() {
  prefs.putLong("today", stepsToday);
  prefs.putInt("curYear", currentYear);
  prefs.putInt("curMonth", currentMonth);
  prefs.putInt("curDay", currentDay);

  if (currentYear > 0 && currentMonth > 0 && currentDay > 0) {
    setStepsForDate(currentYear, currentMonth, currentDay, stepsToday);
  }

  lastSaveTime = millis();
}

void loadCounts() {
  prefs.begin("dogtrack", false);

  stepsToday = prefs.getLong("today", 0);
  currentYear = prefs.getInt("curYear", 0);
  currentMonth = prefs.getInt("curMonth", 0);
  currentDay = prefs.getInt("curDay", 0);
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

  Serial.println();

  if (tries < 20) {
    Serial.println("Time synced");
  } else {
    Serial.println("Time sync failed");
  }
}

void updateCurrentDate() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return;
  }

  int newYear = timeinfo.tm_year + 1900;
  int newMonth = timeinfo.tm_mon + 1;
  int newDay = timeinfo.tm_mday;

  if (currentYear == 0 || currentMonth == 0 || currentDay == 0) {
    currentYear = newYear;
    currentMonth = newMonth;
    currentDay = newDay;

    long savedStepsForToday = getStepsForDate(currentYear, currentMonth, currentDay);

    if (savedStepsForToday > stepsToday) {
      stepsToday = savedStepsForToday;
    }

    saveCounts();
    return;
  }

  if (newYear != currentYear || newMonth != currentMonth || newDay != currentDay) {
    setStepsForDate(currentYear, currentMonth, currentDay, stepsToday);

    currentYear = newYear;
    currentMonth = newMonth;
    currentDay = newDay;

    stepsToday = getStepsForDate(currentYear, currentMonth, currentDay);

    saveCounts();
  }
}

long getYesterdaySteps() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return 0;
  }

  timeinfo.tm_hour = 12;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;

  time_t todayNoon = mktime(&timeinfo);
  time_t yesterdayNoon = todayNoon - 86400;

  struct tm yesterday;
  localtime_r(&yesterdayNoon, &yesterday);

  int year = yesterday.tm_year + 1900;
  int month = yesterday.tm_mon + 1;
  int day = yesterday.tm_mday;

  return getStepsForDate(year, month, day);
}

long getLastWeekSteps() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return 0;
  }

  timeinfo.tm_hour = 12;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;

  time_t todayNoon = mktime(&timeinfo);

  int weekday = timeinfo.tm_wday; 
  time_t startOfLastWeek = todayNoon - ((weekday + 7) * 86400);

  long total = 0;

  for (int i = 0; i < 7; i++) {
    time_t dayTime = startOfLastWeek + (i * 86400);

    struct tm dayInfo;
    localtime_r(&dayTime, &dayInfo);

    int year = dayInfo.tm_year + 1900;
    int month = dayInfo.tm_mon + 1;
    int day = dayInfo.tm_mday;

    total += getStepsForDate(year, month, day);
  }

  return total;
}

long getLastMonthSteps() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return 0;
  }

  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;

  month--;

  if (month < 1) {
    month = 12;
    year--;
  }

  long total = 0;
  int totalDays = daysInMonth(year, month);

  for (int day = 1; day <= totalDays; day++) {
    total += getStepsForDate(year, month, day);
  }

  return total;
}

String makeCalendarHTML(int year, int month) {
  String calendar = "";

  int prevYear = year;
  int prevMonth = month - 1;

  if (prevMonth < 1) {
    prevMonth = 12;
    prevYear--;
  }

  int nextYear = year;
  int nextMonth = month + 1;

  if (nextMonth > 12) {
    nextMonth = 1;
    nextYear++;
  }

  calendar += "<div class='calendar-nav'>";
  calendar += "<a href='/?year=" + String(prevYear) + "&month=" + String(prevMonth) + "'>Previous Month</a>";
  calendar += "<h2>" + String(monthNames[month - 1]) + " " + String(year) + "</h2>";
  calendar += "<a href='/?year=" + String(nextYear) + "&month=" + String(nextMonth) + "'>Next Month</a>";
  calendar += "</div>";

  struct tm firstDay;
  memset(&firstDay, 0, sizeof(firstDay));

  firstDay.tm_year = year - 1900;
  firstDay.tm_mon = month - 1;
  firstDay.tm_mday = 1;
  firstDay.tm_hour = 12;

  mktime(&firstDay);

  int startingWeekday = firstDay.tm_wday;
  int totalDays = daysInMonth(year, month);

  calendar += "<table class='calendar'>";
  calendar += "<tr>";

  for (int i = 0; i < 7; i++) {
    calendar += "<th>";
    calendar += dayNames[i];
    calendar += "</th>";
  }

  calendar += "</tr><tr>";

  for (int i = 0; i < startingWeekday; i++) {
    calendar += "<td class='empty'></td>";
  }

  for (int day = 1; day <= totalDays; day++) {
    int weekday = (startingWeekday + day - 1) % 7;

    long steps = getStepsForDate(year, month, day);

    String cellClass = "day";

    if (year == currentYear && month == currentMonth && day == currentDay) {
      cellClass += " today-cell";
    }

    calendar += "<td class='" + cellClass + "'>";
    calendar += "<div class='day-number'>" + String(day) + "</div>";
    calendar += "<div class='step-number'>" + String(steps) + "</div>";
    calendar += "<div class='step-label'>steps</div>";
    calendar += "</td>";

    if (weekday == 6 && day != totalDays) {
      calendar += "</tr><tr>";
    }
  }

  calendar += "</tr>";
  calendar += "</table>";

  return calendar;
}

void handleNotFound() {
  Serial.print("NOT FOUND: ");
  Serial.println(server.uri());
  server.send(404, "text/plain", "Not found");
}

void handleRoot() {
  updateCurrentDate();
  saveCounts();

  struct tm timeinfo;
  bool hasTime = getLocalTime(&timeinfo, 10);

  int displayYear;
  int displayMonth;

  if (hasTime) {
    displayYear = timeinfo.tm_year + 1900;
    displayMonth = timeinfo.tm_mon + 1;
  } else {
    displayYear = currentYear;
    displayMonth = currentMonth;
  }

  if (server.hasArg("year")) {
    displayYear = server.arg("year").toInt();
  }

  if (server.hasArg("month")) {
    displayMonth = server.arg("month").toInt();
  }

  if (displayYear <= 0) displayYear = 2026;
  if (displayMonth < 1 || displayMonth > 12) displayMonth = 1;

  long yesterdaySteps = getYesterdaySteps();
  long lastWeekSteps = getLastWeekSteps();
  long lastMonthSteps = getLastMonthSteps();

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta http-equiv='refresh' content='10'>
<title>Dog Tracker</title>

<style>
body {
  font-family: Arial, sans-serif;
  text-align: center;
  background: #dff5e1;
  color: #064d19;
  margin: 0;
  padding: 30px;
}

h1 {
  color: #0b6b2b;
  margin-bottom: 25px;
}

.stats-container {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 20px;
  margin-bottom: 35px;
}

.stat-card {
  background: white;
  padding: 20px;
  border-radius: 15px;
  width: 220px;
  box-shadow: 0px 0px 12px rgba(0, 80, 20, 0.25);
  border: 2px solid #4caf50;
}

.stat-label {
  font-size: 18px;
  color: #176b2c;
}

.stat-number {
  font-size: 42px;
  font-weight: bold;
  color: #0b8f36;
  margin-top: 10px;
}

.calendar-nav {
  display: flex;
  justify-content: center;
  align-items: center;
  gap: 25px;
  margin-bottom: 15px;
}

.calendar-nav a {
  background: #0b8f36;
  color: white;
  padding: 10px 15px;
  border-radius: 8px;
  text-decoration: none;
  font-weight: bold;
}

.calendar-nav a:hover {
  background: #076b28;
}

.calendar {
  margin: auto;
  border-collapse: collapse;
  width: 95%;
  max-width: 1000px;
  background: white;
  box-shadow: 0px 0px 12px rgba(0, 80, 20, 0.25);
}

.calendar th {
  background: #0b8f36;
  color: white;
  padding: 12px;
  font-size: 18px;
}

.calendar td {
  border: 1px solid #b6e2bd;
  height: 95px;
  width: 14%;
  vertical-align: top;
  padding: 8px;
}

.empty {
  background: #f0fff2;
}

.day {
  background: #ffffff;
}

.today-cell {
  background: #c8f7cf;
  border: 3px solid #0b8f36;
}

.day-number {
  font-weight: bold;
  font-size: 18px;
  color: #064d19;
  text-align: left;
}

.step-number {
  font-size: 22px;
  font-weight: bold;
  color: #0b8f36;
  margin-top: 15px;
}

.step-label {
  font-size: 13px;
  color: #176b2c;
}
</style>
</head>

<body>

<h1>Dog Activity Tracker</h1>

<div class="stats-container">

<div class="stat-card">
  <div class="stat-label">Steps Today</div>
  <div class="stat-number">
)rawliteral";

  page += String(stepsToday);

  page += R"rawliteral(
  </div>
</div>

<div class="stat-card">
  <div class="stat-label">Steps Yesterday</div>
  <div class="stat-number">
)rawliteral";

  page += String(yesterdaySteps);

  page += R"rawliteral(
  </div>
</div>

<div class="stat-card">
  <div class="stat-label">Steps Last Week</div>
  <div class="stat-number">
)rawliteral";

  page += String(lastWeekSteps);

  page += R"rawliteral(
  </div>
</div>

<div class="stat-card">
  <div class="stat-label">Steps Last Month</div>
  <div class="stat-number">
)rawliteral";

  page += String(lastMonthSteps);

  page += R"rawliteral(
  </div>
</div>

</div>
)rawliteral";

  page += makeCalendarHTML(displayYear, displayMonth);

  page += R"rawliteral(

</body>
</html>
)rawliteral";

  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);

  Wire.begin();

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
  updateCurrentDate();

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  Serial.println("Starting server...");
  server.begin();
  Serial.println("Server started");
}

void loop() {
  server.handleClient();

  updateCurrentDate();

  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float ax = accel.acceleration.x;
  float ay = accel.acceleration.y;
  float az = accel.acceleration.z;

  float magnitude = sqrt(ax * ax + ay * ay + az * az);
  float delta = fabs(magnitude - previousMagnitude);

  if (delta > 2.0) {
    if (millis() - lastStepTime > 300) {
      stepsToday++;

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