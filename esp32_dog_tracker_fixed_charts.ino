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
long currentHourSteps = 0;

int currentYear = 0;
int currentMonth = 0;
int currentDay = 0;
int currentHour = -1;

float previousMagnitude = 0;
unsigned long lastStepTime = 0;
unsigned long lastSaveTime = 0;

const char* monthNames[] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};

const char* shortMonthNames[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

const char* dayNames[] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

const char* mondayFirstDayNames[] = {
  "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
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

String makeHourKey(int year, int month, int day, int hour) {
  char key[14];
  snprintf(key, sizeof(key), "h%04d%02d%02d%02d", year, month, day, hour);
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

long getStepsForHour(int year, int month, int day, int hour) {
  String key = makeHourKey(year, month, day, hour);
  return prefs.getLong(key.c_str(), 0);
}

void setStepsForHour(int year, int month, int day, int hour, long steps) {
  if (year <= 0 || month <= 0 || day <= 0 || hour < 0 || hour > 23) return;

  String key = makeHourKey(year, month, day, hour);
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
  prefs.putLong("hourSteps", currentHourSteps);

  prefs.putInt("curYear", currentYear);
  prefs.putInt("curMonth", currentMonth);
  prefs.putInt("curDay", currentDay);
  prefs.putInt("curHour", currentHour);

  if (currentYear > 0 && currentMonth > 0 && currentDay > 0) {
    setStepsForDate(currentYear, currentMonth, currentDay, stepsToday);
  }

  if (currentYear > 0 && currentMonth > 0 && currentDay > 0 && currentHour >= 0) {
    setStepsForHour(currentYear, currentMonth, currentDay, currentHour, currentHourSteps);
  }

  lastSaveTime = millis();
}

void loadCounts() {
  prefs.begin("dogtrack", false);

  stepsToday = prefs.getLong("today", 0);
  currentHourSteps = prefs.getLong("hourSteps", 0);

  currentYear = prefs.getInt("curYear", 0);
  currentMonth = prefs.getInt("curMonth", 0);
  currentDay = prefs.getInt("curDay", 0);
  currentHour = prefs.getInt("curHour", -1);
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

void updateCurrentDateAndHour() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return;
  }

  int newYear = timeinfo.tm_year + 1900;
  int newMonth = timeinfo.tm_mon + 1;
  int newDay = timeinfo.tm_mday;
  int newHour = timeinfo.tm_hour;

  if (currentYear == 0 || currentMonth == 0 || currentDay == 0 || currentHour < 0) {
    currentYear = newYear;
    currentMonth = newMonth;
    currentDay = newDay;
    currentHour = newHour;

    long savedStepsForToday = getStepsForDate(currentYear, currentMonth, currentDay);
    if (savedStepsForToday > stepsToday) {
      stepsToday = savedStepsForToday;
    }

    currentHourSteps = getStepsForHour(currentYear, currentMonth, currentDay, currentHour);

    saveCounts();
    return;
  }

  bool dateChanged = newYear != currentYear || newMonth != currentMonth || newDay != currentDay;
  bool hourChanged = newHour != currentHour;

  if (dateChanged) {
    setStepsForDate(currentYear, currentMonth, currentDay, stepsToday);
    setStepsForHour(currentYear, currentMonth, currentDay, currentHour, currentHourSteps);

    currentYear = newYear;
    currentMonth = newMonth;
    currentDay = newDay;
    currentHour = newHour;

    stepsToday = getStepsForDate(currentYear, currentMonth, currentDay);
    currentHourSteps = getStepsForHour(currentYear, currentMonth, currentDay, currentHour);

    saveCounts();
    return;
  }

  if (hourChanged) {
    setStepsForHour(currentYear, currentMonth, currentDay, currentHour, currentHourSteps);

    currentHour = newHour;
    currentHourSteps = getStepsForHour(currentYear, currentMonth, currentDay, currentHour);

    saveCounts();
  }
}

bool getDateOffsetFromToday(int offsetDays, int &year, int &month, int &day) {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return false;
  }

  timeinfo.tm_hour = 12;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;

  time_t baseTime = mktime(&timeinfo);
  time_t targetTime = baseTime + ((long)offsetDays * 86400L);

  struct tm targetInfo;
  localtime_r(&targetTime, &targetInfo);

  year = targetInfo.tm_year + 1900;
  month = targetInfo.tm_mon + 1;
  day = targetInfo.tm_mday;

  return true;
}

long getYesterdaySteps() {
  int year, month, day;

  if (!getDateOffsetFromToday(-1, year, month, day)) {
    return 0;
  }

  return getStepsForDate(year, month, day);
}

long getMonthSteps(int year, int month) {
  long total = 0;
  int totalDays = daysInMonth(year, month);

  for (int day = 1; day <= totalDays; day++) {
    total += getStepsForDate(year, month, day);
  }

  return total;
}

long getYearSteps(int year) {
  long total = 0;

  for (int month = 1; month <= 12; month++) {
    total += getMonthSteps(year, month);
  }

  return total;
}

bool getStartOfLastWeek(time_t &startOfLastWeek) {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 10)) {
    return false;
  }

  timeinfo.tm_hour = 12;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;

  time_t todayNoon = mktime(&timeinfo);

  // tm_wday: Sunday = 0, Monday = 1, Tuesday = 2, etc.
  // This makes the week Monday through Sunday.
  int daysSinceMonday = (timeinfo.tm_wday + 6) % 7;

  time_t startOfThisWeek = todayNoon - ((long)daysSinceMonday * 86400L);
  startOfLastWeek = startOfThisWeek - (7L * 86400L);

  return true;
}

long getLastWeekSteps() {
  time_t startOfLastWeek;

  if (!getStartOfLastWeek(startOfLastWeek)) {
    return 0;
  }

  long total = 0;

  for (int i = 0; i < 7; i++) {
    time_t dayTime = startOfLastWeek + ((long)i * 86400L);

    struct tm dayInfo;
    localtime_r(&dayTime, &dayInfo);

    int year = dayInfo.tm_year + 1900;
    int month = dayInfo.tm_mon + 1;
    int day = dayInfo.tm_mday;

    total += getStepsForDate(year, month, day);
  }

  return total;
}

String hourLabel(int hour) {
  if (hour == 0) return "12 AM";
  if (hour < 12) return String(hour) + " AM";
  if (hour == 12) return "12 PM";
  return String(hour - 12) + " PM";
}

String jsString(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  return "\"" + value + "\"";
}

String buildHourLabelsJS() {
  String js = "[";

  for (int hour = 0; hour < 24; hour++) {
    if (hour > 0) js += ",";
    js += jsString(hourLabel(hour));
  }

  js += "]";
  return js;
}

String buildHourValuesJS(int year, int month, int day) {
  String js = "[";

  for (int hour = 0; hour < 24; hour++) {
    if (hour > 0) js += ",";
    js += String(getStepsForHour(year, month, day, hour));
  }

  js += "]";
  return js;
}

String buildLastWeekLabelsJS() {
  String js = "[";

  for (int i = 0; i < 7; i++) {
    if (i > 0) js += ",";
    js += jsString(String(mondayFirstDayNames[i]));
  }

  js += "]";
  return js;
}

String buildLastWeekValuesJS() {
  time_t startOfLastWeek;

  if (!getStartOfLastWeek(startOfLastWeek)) {
    return "[0,0,0,0,0,0,0]";
  }

  String js = "[";

  for (int i = 0; i < 7; i++) {
    if (i > 0) js += ",";

    time_t dayTime = startOfLastWeek + ((long)i * 86400L);

    struct tm dayInfo;
    localtime_r(&dayTime, &dayInfo);

    int year = dayInfo.tm_year + 1900;
    int month = dayInfo.tm_mon + 1;
    int day = dayInfo.tm_mday;

    js += String(getStepsForDate(year, month, day));
  }

  js += "]";
  return js;
}

String buildMonthLabelsJS(int year, int month) {
  int totalDays = daysInMonth(year, month);
  String js = "[";

  for (int day = 1; day <= totalDays; day++) {
    if (day > 1) js += ",";
    js += jsString(String(day));
  }

  js += "]";
  return js;
}

String buildMonthValuesJS(int year, int month) {
  int totalDays = daysInMonth(year, month);
  String js = "[";

  for (int day = 1; day <= totalDays; day++) {
    if (day > 1) js += ",";
    js += String(getStepsForDate(year, month, day));
  }

  js += "]";
  return js;
}

String buildYearLabelsJS() {
  String js = "[";

  for (int month = 1; month <= 12; month++) {
    if (month > 1) js += ",";
    js += jsString(String(shortMonthNames[month - 1]));
  }

  js += "]";
  return js;
}

String buildYearValuesJS(int year) {
  String js = "[";

  for (int month = 1; month <= 12; month++) {
    if (month > 1) js += ",";
    js += String(getMonthSteps(year, month));
  }

  js += "]";
  return js;
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
  updateCurrentDateAndHour();
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
  long thisMonthSteps = getMonthSteps(displayYear, displayMonth);
  long thisYearSteps = getYearSteps(displayYear);

  int todayYear = currentYear;
  int todayMonth = currentMonth;
  int todayDay = currentDay;

  int yesterdayYear = 0;
  int yesterdayMonth = 0;
  int yesterdayDay = 0;
  getDateOffsetFromToday(-1, yesterdayYear, yesterdayMonth, yesterdayDay);

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
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
  align-items: stretch;
  gap: 20px;
  margin-bottom: 30px;
}

.stat-card {
  background: white;
  padding: 20px;
  border-radius: 15px;
  width: 200px;
  min-height: 120px;
  box-shadow: 0px 0px 12px rgba(0, 80, 20, 0.25);
  border: 2px solid #4caf50;
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.clickable {
  cursor: pointer;
  transition: transform 0.15s, box-shadow 0.15s;
}

.clickable:hover {
  transform: translateY(-3px);
  box-shadow: 0px 4px 16px rgba(0, 80, 20, 0.35);
}

.stat-label {
  font-size: 17px;
  color: #176b2c;
}

.stat-number {
  font-size: 38px;
  font-weight: bold;
  color: #0b8f36;
  margin-top: 10px;
}

.chart-card {
  background: white;
  border: 2px solid #4caf50;
  border-radius: 15px;
  box-shadow: 0px 0px 12px rgba(0, 80, 20, 0.25);
  width: 95%;
  max-width: 1300px;
  margin: 0 auto 35px auto;
  padding: 20px;
  box-sizing: border-box;
}

.chart-card h2 {
  color: #0b6b2b;
  margin-top: 0;
}

.chart-total {
  font-size: 18px;
  color: #176b2c;
  margin-bottom: 15px;
  font-weight: bold;
}

.chart-scroll {
  width: 100%;
  overflow-x: auto;
  overflow-y: hidden;
  padding-bottom: 12px;
  box-sizing: border-box;
}

.bar-chart {
  display: flex;
  align-items: flex-end;
  min-height: 330px;
  padding: 20px;
  box-sizing: border-box;
  margin: 0 auto;
}

.bar-group {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-end;
  flex-shrink: 0;
}

.bar-value {
  font-size: 12px;
  font-weight: bold;
  color: #064d19;
  margin-bottom: 6px;
}

.bar {
  background: #0b8f36;
  border-radius: 8px 8px 0 0;
  min-height: 3px;
}

.bar-label {
  font-size: 11px;
  color: #176b2c;
  margin-top: 8px;
  white-space: nowrap;
}

/* Today and Yesterday: all 24 hours, 12 AM through 11 PM */
.hour-chart {
  justify-content: flex-start;
  gap: 4px;
  width: max-content;
  min-width: 100%;
}

.hour-chart .bar-group {
  width: 42px;
}

.hour-chart .bar {
  width: 24px;
}

/* This Month: all 28, 29, 30, or 31 days */
.month-chart {
  justify-content: flex-start;
  gap: 4px;
  width: max-content;
  min-width: 100%;
}

.month-chart .bar-group {
  width: 32px;
}

.month-chart .bar {
  width: 22px;
}

/* Last Week: bigger and centered */
.week-chart {
  justify-content: center;
  gap: 30px;
  width: 100%;
  min-width: 100%;
  min-height: 400px;
}

.week-chart .bar-group {
  width: 85px;
}

.week-chart .bar {
  width: 58px;
}

.week-chart .bar-value {
  font-size: 15px;
}

.week-chart .bar-label {
  font-size: 15px;
}

/* This Year: bigger and centered */
.year-chart {
  justify-content: center;
  gap: 16px;
  width: 100%;
  min-width: 100%;
  min-height: 400px;
}

.year-chart .bar-group {
  width: 68px;
}

.year-chart .bar {
  width: 48px;
}

.year-chart .bar-value {
  font-size: 15px;
}

.year-chart .bar-label {
  font-size: 15px;
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

<div class="stat-card clickable" onclick="showChart('today')">
  <div class="stat-label">Steps Today</div>
  <div class="stat-number">
)rawliteral";

  page += String(stepsToday);

  page += R"rawliteral(
  </div>
</div>

<div class="stat-card clickable" onclick="showChart('yesterday')">
  <div class="stat-label">Steps Yesterday</div>
  <div class="stat-number">
)rawliteral";

  page += String(yesterdaySteps);

  page += R"rawliteral(
  </div>
</div>

<div class="stat-card clickable" onclick="showChart('lastWeek')">
  <div class="stat-label">Steps Last Week</div>
  <div class="stat-number">
)rawliteral";

  page += String(lastWeekSteps);

  page += R"rawliteral(
  </div>
</div>

<div class="stat-card clickable" onclick="showChart('thisMonth')">
  <div class="stat-label">Steps This Month</div>
  <div class="stat-number">
)rawliteral";

  page += String(thisMonthSteps);

  page += R"rawliteral(
  </div>
</div>

<div class="stat-card clickable" onclick="showChart('thisYear')">
  <div class="stat-label">Steps This Year</div>
  <div class="stat-number">
)rawliteral";

  page += String(thisYearSteps);

  page += R"rawliteral(
  </div>
</div>

</div>

<div class="chart-card">
  <h2 id="chartTitle">Steps Today by Hour</h2>
  <div class="chart-total" id="chartTotal"></div>
  <div class="chart-scroll">
    <div class="bar-chart" id="barChart"></div>
  </div>
</div>
)rawliteral";

  page += makeCalendarHTML(displayYear, displayMonth);

  page += R"rawliteral(

<script>
const charts = {
  today: {
    title: "Steps Today by Hour",
    chartClass: "hour-chart",
    maxBarHeight: 230,
    labels: )rawliteral";

  page += buildHourLabelsJS();

  page += R"rawliteral(,
    values: )rawliteral";

  page += buildHourValuesJS(todayYear, todayMonth, todayDay);

  page += R"rawliteral(
  },
  yesterday: {
    title: "Steps Yesterday by Hour",
    chartClass: "hour-chart",
    maxBarHeight: 230,
    labels: )rawliteral";

  page += buildHourLabelsJS();

  page += R"rawliteral(,
    values: )rawliteral";

  page += buildHourValuesJS(yesterdayYear, yesterdayMonth, yesterdayDay);

  page += R"rawliteral(
  },
  lastWeek: {
    title: "Steps Last Week by Day",
    chartClass: "week-chart",
    maxBarHeight: 300,
    labels: )rawliteral";

  page += buildLastWeekLabelsJS();

  page += R"rawliteral(,
    values: )rawliteral";

  page += buildLastWeekValuesJS();

  page += R"rawliteral(
  },
  thisMonth: {
    title: "Steps This Month by Day - )rawliteral";

  page += String(monthNames[displayMonth - 1]) + " " + String(displayYear);

  page += R"rawliteral(",
    chartClass: "month-chart",
    maxBarHeight: 230,
    labels: )rawliteral";

  page += buildMonthLabelsJS(displayYear, displayMonth);

  page += R"rawliteral(,
    values: )rawliteral";

  page += buildMonthValuesJS(displayYear, displayMonth);

  page += R"rawliteral(
  },
  thisYear: {
    title: "Steps This Year by Month - )rawliteral";

  page += String(displayYear);

  page += R"rawliteral(",
    chartClass: "year-chart",
    maxBarHeight: 300,
    labels: )rawliteral";

  page += buildYearLabelsJS();

  page += R"rawliteral(,
    values: )rawliteral";

  page += buildYearValuesJS(displayYear);

  page += R"rawliteral(
  }
};

function showChart(type) {
  const chart = charts[type];
  if (!chart) return;

  const chartBox = document.getElementById("barChart");
  const title = document.getElementById("chartTitle");
  const total = document.getElementById("chartTotal");

  title.textContent = chart.title;

  const totalSteps = chart.values.reduce((sum, value) => sum + value, 0);
  total.textContent = "Total: " + totalSteps + " steps";

  const maxValue = Math.max(...chart.values, 1);
  const maxBarHeight = chart.maxBarHeight || 230;

  chartBox.innerHTML = "";
  chartBox.className = "bar-chart " + chart.chartClass;

  chart.labels.forEach((label, index) => {
    const value = chart.values[index];

    const height = value === 0
      ? 3
      : Math.max(8, Math.round((value / maxValue) * maxBarHeight));

    const group = document.createElement("div");
    group.className = "bar-group";

    const valueText = document.createElement("div");
    valueText.className = "bar-value";
    valueText.textContent = value;

    const bar = document.createElement("div");
    bar.className = "bar";
    bar.style.height = height + "px";

    const labelText = document.createElement("div");
    labelText.className = "bar-label";
    labelText.textContent = label;

    group.appendChild(valueText);
    group.appendChild(bar);
    group.appendChild(labelText);
    chartBox.appendChild(group);
  });
}

showChart("today");
</script>

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
  updateCurrentDateAndHour();

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  Serial.println("Starting server...");
  server.begin();
  Serial.println("Server started");
}

void loop() {
  server.handleClient();

  updateCurrentDateAndHour();

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
      currentHourSteps++;

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