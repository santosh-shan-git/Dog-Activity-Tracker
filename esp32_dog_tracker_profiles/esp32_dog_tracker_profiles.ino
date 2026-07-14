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
Preferences profilePrefs;

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

// Sleep/activity estimate settings
// A quiet hour is only counted as sleep if the ESP32 actually tracked that hour
// AND it belongs to a long quiet block. This prevents missing data or 0 saved
// steps from being treated as 24 hours of sleep.
const int SLEEP_STEP_THRESHOLD = 5;
const int MIN_CONSECUTIVE_LOW_HOURS_FOR_SLEEP = 3;

// Activity intensity is based on average steps per active tracked hour.
const int LOW_INTENSITY_MAX_STEPS_PER_ACTIVE_HOUR = 50;
const int MEDIUM_INTENSITY_MAX_STEPS_PER_ACTIVE_HOUR = 150;

// Dog profile settings
const int MAX_DOG_PROFILES = 6;
int activeProfile = 0;

const char* dogBreedOptions[] = {
  "Mixed Breed",
  "Labrador Retriever",
  "Golden Retriever",
  "German Shepherd",
  "French Bulldog",
  "Bulldog",
  "Poodle",
  "Goldendoodle",
  "Labradoodle",
  "Beagle",
  "Rottweiler",
  "Dachshund",
  "Pembroke Welsh Corgi",
  "Australian Shepherd",
  "Yorkshire Terrier",
  "Boxer",
  "Great Dane",
  "Siberian Husky",
  "Border Collie",
  "Shih Tzu",
  "Pomeranian",
  "Chihuahua",
  "Cavalier King Charles Spaniel",
  "Doberman Pinscher",
  "Bernese Mountain Dog",
  "Miniature Schnauzer",
  "Boston Terrier",
  "Havanese",
  "Cocker Spaniel",
  "Cane Corso",
  "Pit Bull Terrier",
  "Maltese",
  "Bichon Frise",
  "Samoyed",
  "Akita",
  "Shiba Inu",
  "Vizsla",
  "Weimaraner",
  "Australian Cattle Dog",
  "Jack Russell Terrier",
  "Other"
};

const int DOG_BREED_COUNT = sizeof(dogBreedOptions) / sizeof(dogBreedOptions[0]);

String profileNamespace(int index) {
  if (index == 0) return "dogtrack";
  return "dogp" + String(index);
}

String profileKey(String base, int index) {
  return base + String(index);
}

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", "\\n");
  value.replace("\r", "");
  return value;
}

void initProfiles() {
  profilePrefs.begin("dogprofiles", false);

  if (!profilePrefs.getBool("ready", false)) {
    profilePrefs.putBool("ready", true);
    profilePrefs.putBool("used0", true);
    profilePrefs.putString("name0", "Test");
    profilePrefs.putString("breed0", "Test Profile");
    profilePrefs.putInt("active", 0);
    profilePrefs.putBool("startDone", false);
  } else {
    if (!profilePrefs.getBool("used0", false)) {
      profilePrefs.putBool("used0", true);
    }

    if (profilePrefs.getString("name0", "").length() == 0) {
      profilePrefs.putString("name0", "Test");
    }

    if (profilePrefs.getString("breed0", "").length() == 0) {
      profilePrefs.putString("breed0", "Test Profile");
    }
  }

  int savedActiveProfile = profilePrefs.getInt("active", 0);

  if (savedActiveProfile < 0 || savedActiveProfile >= MAX_DOG_PROFILES ||
      !profilePrefs.getBool(profileKey("used", savedActiveProfile).c_str(), false)) {
    savedActiveProfile = 0;
    profilePrefs.putInt("active", 0);
  }

  activeProfile = savedActiveProfile;

  profilePrefs.end();
}

bool profileExists(int index) {
  if (index < 0 || index >= MAX_DOG_PROFILES) return false;

  profilePrefs.begin("dogprofiles", true);
  bool exists = profilePrefs.getBool(profileKey("used", index).c_str(), false);
  profilePrefs.end();

  return exists;
}

bool profileStartDone() {
  profilePrefs.begin("dogprofiles", true);
  bool done = profilePrefs.getBool("startDone", false);
  profilePrefs.end();

  return done;
}

void markProfileStartDone() {
  profilePrefs.begin("dogprofiles", false);
  profilePrefs.putBool("startDone", true);
  profilePrefs.end();
}

String getProfileName(int index) {
  profilePrefs.begin("dogprofiles", true);
  String value = profilePrefs.getString(profileKey("name", index).c_str(), index == 0 ? "Test" : "Dog");
  profilePrefs.end();

  if (value.length() == 0) value = index == 0 ? "Test" : "Dog";
  return value;
}

String getProfileBreed(int index) {
  profilePrefs.begin("dogprofiles", true);
  String value = profilePrefs.getString(profileKey("breed", index).c_str(), index == 0 ? "Test Profile" : "Unknown Breed");
  profilePrefs.end();

  if (value.length() == 0) value = index == 0 ? "Test Profile" : "Unknown Breed";
  return value;
}

int getProfileCount() {
  int total = 0;

  profilePrefs.begin("dogprofiles", true);

  for (int i = 0; i < MAX_DOG_PROFILES; i++) {
    if (profilePrefs.getBool(profileKey("used", i).c_str(), false)) {
      total++;
    }
  }

  profilePrefs.end();

  return total;
}

void openActiveProfilePreferences() {
  prefs.end();
  prefs.begin(profileNamespace(activeProfile).c_str(), false);
}

void loadCountsFromCurrentProfile() {
  stepsToday = prefs.getLong("today", 0);
  currentHourSteps = prefs.getLong("hourSteps", 0);

  currentYear = prefs.getInt("curYear", 0);
  currentMonth = prefs.getInt("curMonth", 0);
  currentDay = prefs.getInt("curDay", 0);
  currentHour = prefs.getInt("curHour", -1);
}

void resetRuntimeCountersForProfile() {
  stepsToday = 0;
  currentHourSteps = 0;
  currentYear = 0;
  currentMonth = 0;
  currentDay = 0;
  currentHour = -1;
  lastSaveTime = 0;
}

bool setActiveProfile(int index) {
  if (!profileExists(index)) return false;

  profilePrefs.begin("dogprofiles", false);
  profilePrefs.putInt("active", index);
  profilePrefs.putBool("startDone", true);
  profilePrefs.end();

  activeProfile = index;

  return true;
}

void switchToProfile(int index) {
  if (!profileExists(index)) return;

  saveCounts();

  if (!setActiveProfile(index)) return;

  openActiveProfilePreferences();
  resetRuntimeCountersForProfile();
  loadCountsFromCurrentProfile();
}

int createDogProfile(String dogName, String dogBreed) {
  dogName.trim();
  dogBreed.trim();

  if (dogName.length() == 0) return -1;
  if (dogBreed.length() == 0) dogBreed = "Unknown Breed";

  profilePrefs.begin("dogprofiles", false);

  int newIndex = -1;

  for (int i = 1; i < MAX_DOG_PROFILES; i++) {
    if (!profilePrefs.getBool(profileKey("used", i).c_str(), false)) {
      newIndex = i;
      break;
    }
  }

  if (newIndex >= 0) {
    profilePrefs.putBool(profileKey("used", newIndex).c_str(), true);
    profilePrefs.putString(profileKey("name", newIndex).c_str(), dogName);
    profilePrefs.putString(profileKey("breed", newIndex).c_str(), dogBreed);
    profilePrefs.putInt("active", newIndex);
    profilePrefs.putBool("startDone", true);
  }

  profilePrefs.end();

  if (newIndex >= 0) {
    activeProfile = newIndex;
  }

  return newIndex;
}

bool createAndSwitchToProfile(String dogName, String dogBreed) {
  saveCounts();

  int newIndex = createDogProfile(dogName, dogBreed);

  if (newIndex < 0) {
    return false;
  }

  openActiveProfilePreferences();
  resetRuntimeCountersForProfile();
  loadCountsFromCurrentProfile();

  return true;
}

String buildBreedDatalistHTML() {
  String html = "<datalist id='dogBreedList'>";

  for (int i = 0; i < DOG_BREED_COUNT; i++) {
    html += "<option value='";
    html += htmlEscape(String(dogBreedOptions[i]));
    html += "'>";
  }

  html += "</datalist>";
  return html;
}

String buildProfileCardsHTML() {
  String html = "";

  for (int i = 0; i < MAX_DOG_PROFILES; i++) {
    if (!profileExists(i)) continue;

    String activeClass = i == activeProfile ? " profile-card-active" : "";

    html += "<a class='profile-card";
    html += activeClass;
    html += "' href='/?selectProfile=";
    html += String(i);
    html += "'>";
    html += "<div class='profile-avatar'>";
    html += String(getProfileName(i).charAt(0));
    html += "</div>";
    html += "<div class='profile-card-body'>";
    html += "<div class='profile-card-name'>";
    html += htmlEscape(getProfileName(i));
    html += "</div>";
    html += "<div class='profile-card-breed'>";
    html += htmlEscape(getProfileBreed(i));
    html += "</div>";
    html += "</div>";
    html += "<div class='profile-card-action'>";
    html += i == activeProfile ? "Current" : "Open";
    html += "</div>";
    html += "</a>";
  }

  return html;
}

void redirectToDashboard() {
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}


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

String makeHourTrackedKey(int year, int month, int day, int hour) {
  char key[14];
  snprintf(key, sizeof(key), "t%04d%02d%02d%02d", year, month, day, hour);
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

void markHourTracked(int year, int month, int day, int hour) {
  if (year <= 0 || month <= 0 || day <= 0 || hour < 0 || hour > 23) return;

  String key = makeHourTrackedKey(year, month, day, hour);
  prefs.putBool(key.c_str(), true);
}

bool wasHourTracked(int year, int month, int day, int hour) {
  if (year <= 0 || month <= 0 || day <= 0 || hour < 0 || hour > 23) return false;

  String key = makeHourTrackedKey(year, month, day, hour);

  // Old saved data did not have the tracked marker. If the hour has steps,
  // it definitely had data, so count it as tracked.
  return prefs.getBool(key.c_str(), false) || getStepsForHour(year, month, day, hour) > 0;
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
    markHourTracked(currentYear, currentMonth, currentDay, currentHour);
  }

  lastSaveTime = millis();
}

void loadCounts() {
  initProfiles();
  openActiveProfilePreferences();
  loadCountsFromCurrentProfile();
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
    markHourTracked(currentYear, currentMonth, currentDay, currentHour);

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
    markHourTracked(currentYear, currentMonth, currentDay, currentHour);

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

int getHoursToCheckForDate(int year, int month, int day) {
  if (currentYear <= 0 || currentMonth <= 0 || currentDay <= 0) {
    return 0;
  }

  long requestedDate = ((long)year * 10000L) + ((long)month * 100L) + day;
  long currentDate = ((long)currentYear * 10000L) + ((long)currentMonth * 100L) + currentDay;

  // Future dates should not count as sleep or activity.
  if (requestedDate > currentDate) {
    return 0;
  }

  // For today, only check the hours that have happened so far.
  if (requestedDate == currentDate) {
    if (currentHour < 0) return 0;
    return currentHour + 1;
  }

  // For past days, check all 24 hours, but only hours marked as tracked
  // are allowed to count toward sleep/activity.
  return 24;
}

int getTrackedHoursForDate(int year, int month, int day) {
  int hoursToCheck = getHoursToCheckForDate(year, month, day);
  int trackedHours = 0;

  for (int hour = 0; hour < hoursToCheck; hour++) {
    if (wasHourTracked(year, month, day, hour)) {
      trackedHours++;
    }
  }

  return trackedHours;
}

int getSleepHoursForDate(int year, int month, int day) {
  int hoursToCheck = getHoursToCheckForDate(year, month, day);
  int sleepHours = 0;
  int quietBlockLength = 0;

  for (int hour = 0; hour < hoursToCheck; hour++) {
    bool tracked = wasHourTracked(year, month, day, hour);
    long stepsThisHour = getStepsForHour(year, month, day, hour);
    bool quietHour = tracked && stepsThisHour <= SLEEP_STEP_THRESHOLD;

    if (quietHour) {
      quietBlockLength++;
    } else {
      if (quietBlockLength >= MIN_CONSECUTIVE_LOW_HOURS_FOR_SLEEP) {
        sleepHours += quietBlockLength;
      }
      quietBlockLength = 0;
    }
  }

  if (quietBlockLength >= MIN_CONSECUTIVE_LOW_HOURS_FOR_SLEEP) {
    sleepHours += quietBlockLength;
  }

  return sleepHours;
}

int getActiveHoursForDate(int year, int month, int day) {
  int hoursToCheck = getHoursToCheckForDate(year, month, day);
  int activeHours = 0;

  for (int hour = 0; hour < hoursToCheck; hour++) {
    if (!wasHourTracked(year, month, day, hour)) {
      continue;
    }

    long stepsThisHour = getStepsForHour(year, month, day, hour);

    if (stepsThisHour > SLEEP_STEP_THRESHOLD) {
      activeHours++;
    }
  }

  return activeHours;
}

String getSleepDisplayForDate(int year, int month, int day) {
  int trackedHours = getTrackedHoursForDate(year, month, day);

  if (trackedHours == 0) {
    return "--";
  }

  return String(getSleepHoursForDate(year, month, day)) + "h";
}

String getActivityIntensityForDate(int year, int month, int day) {
  int trackedHours = getTrackedHoursForDate(year, month, day);

  if (trackedHours == 0) {
    return "No data";
  }

  long totalSteps = getStepsForDate(year, month, day);
  int sleepHours = getSleepHoursForDate(year, month, day);
  int activeHours = getActiveHoursForDate(year, month, day);

  if (totalSteps == 0) {
    return "Quiet / no steps";
  }

  if (activeHours == 0) {
    return "Very low";
  }

  long stepsPerActiveHour = totalSteps / activeHours;
  String intensity;

  if (stepsPerActiveHour <= LOW_INTENSITY_MAX_STEPS_PER_ACTIVE_HOUR) {
    intensity = "Low";
  } else if (stepsPerActiveHour <= MEDIUM_INTENSITY_MAX_STEPS_PER_ACTIVE_HOUR) {
    intensity = "Medium";
  } else {
    intensity = "High";
  }

  if (sleepHours > 0) {
    return intensity + " (" + String(stepsPerActiveHour) + "/hr, " + String(sleepHours) + "h sleep)";
  }

  return intensity + " (" + String(stepsPerActiveHour) + "/hr)";
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
    String sleepDisplay = getSleepDisplayForDate(year, month, day);
    String intensityDisplay = getActivityIntensityForDate(year, month, day);

    String cellClass = "day";

    if (year == currentYear && month == currentMonth && day == currentDay) {
      cellClass += " today-cell";
    }

    calendar += "<td class='" + cellClass + "'>";
    calendar += "<div class='day-number'>" + String(day) + "</div>";
    calendar += "<div class='step-number'>" + String(steps) + "</div>";
    calendar += "<div class='step-label'>steps</div>";
    calendar += "<div class='sleep-number'>Sleep: " + sleepDisplay + "</div>";
    calendar += "<div class='intensity-label'>" + intensityDisplay + "</div>";
    calendar += "</td>";

    if (weekday == 6 && day != totalDays) {
      calendar += "</tr><tr>";
    }
  }

  calendar += "</tr>";
  calendar += "</table>";

  return calendar;
}


// JSON endpoint for a mobile app.
// Open http://<ESP32-IP>/api/dashboard to view the current tracker data.
void handleDashboardAPI() {
  updateCurrentDateAndHour();
  saveCounts();

  int yesterdayYear = 0;
  int yesterdayMonth = 0;
  int yesterdayDay = 0;

  bool hasYesterdayDate = getDateOffsetFromToday(
    -1,
    yesterdayYear,
    yesterdayMonth,
    yesterdayDay
  );

  long yesterdaySteps = 0;
  String sleepYesterdayDisplay = "--";

  if (hasYesterdayDate) {
    yesterdaySteps = getStepsForDate(
      yesterdayYear,
      yesterdayMonth,
      yesterdayDay
    );

    sleepYesterdayDisplay = getSleepDisplayForDate(
      yesterdayYear,
      yesterdayMonth,
      yesterdayDay
    );
  }

  String sleepTodayDisplay = getSleepDisplayForDate(
    currentYear,
    currentMonth,
    currentDay
  );

  String activityIntensityDisplay = getActivityIntensityForDate(
    currentYear,
    currentMonth,
    currentDay
  );

  String json = "{";
  json += "\"stepsToday\":" + String(stepsToday) + ",";
  json += "\"stepsYesterday\":" + String(yesterdaySteps) + ",";
  json += "\"sleepToday\":\"" + sleepTodayDisplay + "\",";
  json += "\"sleepYesterday\":\"" + sleepYesterdayDisplay + "\",";
  json += "\"activityIntensity\":\"" + activityIntensityDisplay + "\",";
  json += "\"year\":" + String(currentYear) + ",";
  json += "\"month\":" + String(currentMonth) + ",";
  json += "\"day\":" + String(currentDay) + ",";
  json += "\"hour\":" + String(currentHour) + ",";
  json += "\"activeProfile\":" + String(activeProfile) + ",";
  json += "\"profileName\":\"" + jsonEscape(getProfileName(activeProfile)) + "\",";
  json += "\"dogBreed\":\"" + jsonEscape(getProfileBreed(activeProfile)) + "\",";
  json += "\"deviceIp\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";

  // Allows a phone app or Expo development server to request the data.
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}


void handleProfilesPage(String message) {
  String profileCards = buildProfileCardsHTML();

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Dog Profiles</title>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">

<style>
:root {
  --bg: #eef8f1;
  --card: #ffffff;
  --text: #102318;
  --muted: #607267;
  --green: #148a47;
  --green-dark: #0c6232;
  --green-soft: #dff4e7;
  --green-line: #b8e3c8;
  --shadow: 0 18px 45px rgba(14, 85, 46, 0.12);
  --radius-lg: 26px;
  --radius-md: 18px;
}

* {
  box-sizing: border-box;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif;
  background:
    radial-gradient(circle at top left, rgba(20, 138, 71, 0.16), transparent 34%),
    radial-gradient(circle at top right, rgba(108, 198, 141, 0.20), transparent 30%),
    linear-gradient(180deg, #f8fcf9 0%, var(--bg) 100%);
  color: var(--text);
  margin: 0;
  padding: 20px;
  min-height: 100vh;
}

.profile-shell {
  width: min(980px, 100%);
  margin: 0 auto;
}

.profile-hero {
  background: linear-gradient(135deg, #0b5e33 0%, #148a47 55%, #56b870 100%);
  color: white;
  border-radius: 30px;
  padding: 30px;
  box-shadow: var(--shadow);
  margin-bottom: 22px;
  position: relative;
  overflow: hidden;
}

.profile-hero::after {
  content: "";
  position: absolute;
  width: 240px;
  height: 240px;
  border-radius: 999px;
  right: -88px;
  top: -88px;
  background: rgba(255, 255, 255, 0.16);
}

.profile-hero-content {
  position: relative;
  z-index: 1;
}

.profile-eyebrow {
  display: inline-flex;
  font-size: 13px;
  font-weight: 800;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  background: rgba(255, 255, 255, 0.16);
  border: 1px solid rgba(255, 255, 255, 0.22);
  color: rgba(255, 255, 255, 0.94);
  border-radius: 999px;
  padding: 7px 12px;
  margin-bottom: 14px;
}

h1 {
  font-size: clamp(34px, 7vw, 56px);
  line-height: 0.95;
  letter-spacing: -0.05em;
  margin: 0;
}

.profile-subtitle {
  max-width: 680px;
  margin: 16px 0 0 0;
  color: rgba(255, 255, 255, 0.84);
  font-size: 16px;
  line-height: 1.5;
}

.profile-grid {
  display: grid;
  grid-template-columns: 1fr 1.05fr;
  gap: 18px;
}

.panel {
  background: rgba(255, 255, 255, 0.92);
  border: 1px solid rgba(20, 138, 71, 0.14);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow);
  padding: 24px;
}

.panel h2 {
  margin: 0 0 6px 0;
  font-size: clamp(23px, 4vw, 32px);
  letter-spacing: -0.04em;
}

.panel p {
  margin: 0 0 18px 0;
  color: var(--muted);
  line-height: 1.45;
}

.profile-list {
  display: grid;
  gap: 12px;
}

.profile-card {
  display: grid;
  grid-template-columns: 52px 1fr auto;
  align-items: center;
  gap: 12px;
  text-decoration: none;
  color: var(--text);
  background: linear-gradient(180deg, #ffffff 0%, #f4fff7 100%);
  border: 1px solid rgba(20, 138, 71, 0.16);
  border-radius: 18px;
  padding: 14px;
  box-shadow: 0 10px 22px rgba(12, 98, 50, 0.08);
  transition: transform 0.18s ease, box-shadow 0.18s ease, border-color 0.18s ease;
}

.profile-card:hover {
  transform: translateY(-4px);
  box-shadow: 0 16px 35px rgba(12, 98, 50, 0.15);
  border-color: rgba(20, 138, 71, 0.34);
}

.profile-card-active {
  box-shadow: 0 16px 35px rgba(12, 98, 50, 0.16), 0 0 0 4px rgba(20, 138, 71, 0.10);
  border-color: rgba(20, 138, 71, 0.38);
}

.profile-avatar {
  width: 52px;
  height: 52px;
  border-radius: 16px;
  display: grid;
  place-items: center;
  background: linear-gradient(135deg, var(--green-dark), var(--green));
  color: white;
  font-size: 24px;
  font-weight: 900;
  text-transform: uppercase;
}

.profile-card-name {
  font-size: 19px;
  font-weight: 900;
}

.profile-card-breed {
  margin-top: 3px;
  color: var(--muted);
  font-weight: 650;
  font-size: 13px;
}

.profile-card-action {
  background: var(--green-soft);
  color: var(--green-dark);
  border: 1px solid var(--green-line);
  border-radius: 999px;
  padding: 8px 12px;
  font-weight: 900;
  font-size: 12px;
}

.profile-form {
  display: grid;
  gap: 14px;
}

.form-field label {
  display: block;
  font-size: 13px;
  font-weight: 900;
  color: var(--green-dark);
  text-transform: uppercase;
  letter-spacing: 0.07em;
  margin-bottom: 8px;
}

.form-field input {
  width: 100%;
  border: 1px solid rgba(20, 138, 71, 0.22);
  border-radius: 16px;
  padding: 14px 15px;
  font-size: 16px;
  background: #fbfffc;
  outline: none;
  color: var(--text);
}

.form-field input:focus {
  border-color: var(--green);
  box-shadow: 0 0 0 4px rgba(20, 138, 71, 0.12);
}

.confirm-btn,
.dashboard-link {
  appearance: none;
  border: 0;
  border-radius: 16px;
  padding: 15px 18px;
  font-size: 16px;
  font-weight: 900;
  cursor: pointer;
  text-decoration: none;
  text-align: center;
  transition: transform 0.16s ease, box-shadow 0.16s ease;
}

.confirm-btn {
  background: linear-gradient(135deg, var(--green-dark), var(--green));
  color: white;
  box-shadow: 0 14px 28px rgba(20, 138, 71, 0.22);
}

.confirm-btn:hover,
.dashboard-link:hover {
  transform: translateY(-3px);
}

.dashboard-link {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: white;
  color: var(--green-dark);
  border: 1px solid var(--green-line);
  margin-top: 14px;
}

.message {
  background: #fff9e6;
  border: 1px solid #f1d37d;
  color: #644700;
  padding: 12px 14px;
  border-radius: 14px;
  font-weight: 750;
  margin-bottom: 16px;
}

.small-note {
  font-size: 13px;
  color: var(--muted);
  margin-top: 10px;
}

@media (max-width: 800px) {
  .profile-grid {
    grid-template-columns: 1fr;
  }

  .profile-card {
    grid-template-columns: 46px 1fr;
  }

  .profile-card-action {
    grid-column: 1 / -1;
    width: fit-content;
  }
}
</style>
</head>

<body>
<div class="profile-shell">

<header class="profile-hero">
  <div class="profile-hero-content">
    <div class="profile-eyebrow">Dog Profiles</div>
    <h1>Choose or add your dog</h1>
    <p class="profile-subtitle">Each dog profile saves its own steps, hourly history, sleep estimate, activity intensity, charts, and calendar even after the ESP32 is turned off.</p>
  </div>
</header>
)rawliteral";

  if (message.length() > 0) {
    page += "<div class='message'>";
    page += htmlEscape(message);
    page += "</div>";
  }

  page += R"rawliteral(
<div class="profile-grid">

<section class="panel">
  <h2>Saved Profiles</h2>
  <p>Open an existing profile. Your current saved step data is stored under the Test profile.</p>
  <div class="profile-list">
)rawliteral";

  page += profileCards;

  page += R"rawliteral(
  </div>
  <a class="dashboard-link" href="/">Back to dashboard</a>
</section>

<section class="panel">
  <h2>Add New Profile</h2>
  <p>Enter your dog’s name and search/select the breed. Press confirm to make the profile.</p>

  <form class="profile-form" action="/" method="GET">
    <input type="hidden" name="createProfile" value="1">

    <div class="form-field">
      <label for="dogName">Dog Name</label>
      <input id="dogName" name="dogName" type="text" placeholder="Example: Buddy" required maxlength="24">
    </div>

    <div class="form-field">
      <label for="dogBreed">Dog Breed</label>
      <input id="dogBreed" name="dogBreed" list="dogBreedList" type="text" placeholder="Search breed..." required maxlength="36">
)rawliteral";

  page += buildBreedDatalistHTML();

  page += R"rawliteral(
    </div>

    <button class="confirm-btn" type="submit">Confirm Profile</button>
    <div class="small-note">You can save up to 6 profiles total, including Test.</div>
  </form>
</section>

</div>
</div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", page);
}

void handleNotFound() {
  Serial.print("NOT FOUND: ");
  Serial.println(server.uri());
  server.send(404, "text/plain", "Not found");
}

void handleRoot() {
  if (server.hasArg("createProfile")) {
    String dogName = server.arg("dogName");
    String dogBreed = server.arg("dogBreed");

    if (createAndSwitchToProfile(dogName, dogBreed)) {
      redirectToDashboard();
      return;
    }

    handleProfilesPage("Profile could not be created. You may have reached the maximum number of dog profiles.");
    return;
  }

  if (server.hasArg("selectProfile")) {
    int selectedProfile = server.arg("selectProfile").toInt();

    if (profileExists(selectedProfile)) {
      switchToProfile(selectedProfile);
      redirectToDashboard();
      return;
    }

    handleProfilesPage("That profile could not be found.");
    return;
  }

  if (server.hasArg("profiles") || !profileStartDone()) {
    handleProfilesPage("");
    return;
  }

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

  String sleepTodayDisplay = getSleepDisplayForDate(todayYear, todayMonth, todayDay);
  String sleepYesterdayDisplay = getSleepDisplayForDate(yesterdayYear, yesterdayMonth, yesterdayDay);
  String activityIntensityDisplay = getActivityIntensityForDate(todayYear, todayMonth, todayDay);

  String activeProfileName = htmlEscape(getProfileName(activeProfile));
  String activeProfileBreed = htmlEscape(getProfileBreed(activeProfile));

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Dog Tracker by Santosh Shanmugam</title>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">

<style>
:root {
  --bg: #eef8f1;
  --bg-2: #f8fcf9;
  --card: #ffffff;
  --text: #102318;
  --muted: #607267;
  --green: #148a47;
  --green-dark: #0c6232;
  --green-soft: #dff4e7;
  --green-line: #b8e3c8;
  --shadow: 0 18px 45px rgba(14, 85, 46, 0.12);
  --shadow-small: 0 8px 24px rgba(14, 85, 46, 0.10);
  --radius-lg: 26px;
  --radius-md: 18px;
  --radius-sm: 12px;
}

* {
  box-sizing: border-box;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif;
  background:
    radial-gradient(circle at top left, rgba(20, 138, 71, 0.16), transparent 34%),
    radial-gradient(circle at top right, rgba(108, 198, 141, 0.20), transparent 30%),
    linear-gradient(180deg, #f8fcf9 0%, var(--bg) 100%);
  color: var(--text);
  margin: 0;
  padding: 20px;
  min-height: 100vh;
}

.app-shell {
  width: min(1180px, 100%);
  margin: 0 auto;
}

.hero {
  background: linear-gradient(135deg, #0b5e33 0%, #148a47 55%, #56b870 100%);
  color: white;
  border-radius: 30px;
  padding: 28px;
  box-shadow: var(--shadow);
  margin-bottom: 22px;
  position: relative;
  overflow: hidden;
}

.hero::after {
  content: "";
  position: absolute;
  width: 230px;
  height: 230px;
  border-radius: 999px;
  right: -80px;
  top: -80px;
  background: rgba(255, 255, 255, 0.16);
}

.hero-content {
  position: relative;
  z-index: 1;
}

.eyebrow {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  background: rgba(255, 255, 255, 0.16);
  border: 1px solid rgba(255, 255, 255, 0.22);
  color: rgba(255, 255, 255, 0.94);
  border-radius: 999px;
  padding: 7px 12px;
  margin-bottom: 14px;
}

h1 {
  font-size: clamp(34px, 7vw, 58px);
  line-height: 0.95;
  letter-spacing: -0.05em;
  margin: 0;
  color: white;
}

.creator {
  font-size: clamp(15px, 2.6vw, 19px);
  margin-top: 12px;
  color: rgba(255, 255, 255, 0.88);
  font-weight: 650;
}

.hero-subtitle {
  max-width: 720px;
  margin: 16px 0 0 0;
  color: rgba(255, 255, 255, 0.82);
  font-size: 16px;
  line-height: 1.5;
}

.section-heading {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 12px;
  margin: 4px 0 14px 0;
}

.section-heading h2 {
  margin: 0;
  color: var(--text);
  font-size: clamp(22px, 4vw, 30px);
  letter-spacing: -0.03em;
}

.section-heading p {
  margin: 4px 0 0 0;
  color: var(--muted);
  font-size: 14px;
}

.stats-container {
  display: grid;
  grid-template-columns: repeat(5, minmax(0, 1fr));
  gap: 16px;
  margin-bottom: 22px;
}

.stat-card {
  position: relative;
  overflow: hidden;
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.98) 0%, rgba(246, 255, 249, 0.94) 100%);
  backdrop-filter: blur(10px);
  padding: 18px;
  border-radius: var(--radius-md);
  min-height: 132px;
  box-shadow: 0 10px 22px rgba(12, 98, 50, 0.10), 0 1px 0 rgba(255, 255, 255, 0.95) inset;
  border: 1px solid rgba(20, 138, 71, 0.18);
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  text-align: left;
}

.stat-card::before {
  content: "";
  width: 42px;
  height: 6px;
  border-radius: 999px;
  background: linear-gradient(90deg, var(--green-dark), var(--green), #7ee29b);
  display: block;
  margin-bottom: 12px;
  box-shadow: 0 4px 12px rgba(20, 138, 71, 0.28);
}

.stat-card.clickable::after {
  content: "Tap";
  position: absolute;
  top: 14px;
  right: 14px;
  background: var(--green-soft);
  color: var(--green-dark);
  border: 1px solid var(--green-line);
  border-radius: 999px;
  padding: 5px 9px;
  font-size: 11px;
  font-weight: 900;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  box-shadow: 0 5px 12px rgba(20, 138, 71, 0.10);
}

.clickable {
  cursor: pointer;
  transition: transform 0.2s ease, box-shadow 0.2s ease, border-color 0.2s ease, background 0.2s ease;
}

.clickable:hover {
  transform: translateY(-7px) scale(1.018);
  box-shadow: 0 22px 42px rgba(12, 98, 50, 0.19), 0 0 0 4px rgba(42, 173, 92, 0.10);
  border-color: rgba(20, 138, 71, 0.42);
  background: linear-gradient(180deg, #ffffff 0%, #effcf3 100%);
}

.clickable:active {
  transform: translateY(-2px) scale(0.985);
  box-shadow: 0 10px 20px rgba(12, 98, 50, 0.16);
}

.clickable.active {
  border-color: var(--green);
  background: linear-gradient(180deg, #ffffff 0%, #e8f9ee 100%);
  box-shadow: 0 18px 42px rgba(20, 138, 71, 0.24), 0 0 0 5px rgba(42, 173, 92, 0.12);
}

.clickable.active::after {
  content: "Open";
  background: var(--green);
  color: white;
  border-color: var(--green);
}

.stat-label {
  font-size: 13px;
  font-weight: 800;
  color: var(--muted);
  text-transform: uppercase;
  letter-spacing: 0.06em;
}

.stat-number {
  font-size: clamp(30px, 5vw, 42px);
  line-height: 1;
  font-weight: 850;
  color: var(--green-dark);
  margin-top: 10px;
  letter-spacing: -0.04em;
  transition: color 0.2s ease, transform 0.2s ease;
}

.stat-card.clickable:hover .stat-number {
  color: var(--green);
  transform: translateX(2px);
}

.stat-text {
  font-size: clamp(22px, 4vw, 31px);
  line-height: 1.08;
  font-weight: 850;
  color: var(--green-dark);
  margin-top: 10px;
  letter-spacing: -0.03em;
}

.panel,
.chart-card {
  background: rgba(255, 255, 255, 0.92);
  border: 1px solid rgba(20, 138, 71, 0.13);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow);
  padding: 22px;
  margin: 0 auto 22px auto;
}

.chart-card {
  width: 100%;
  max-width: none;
}

.chart-card h2 {
  color: var(--text);
  margin: 0;
  font-size: clamp(21px, 4vw, 30px);
  letter-spacing: -0.03em;
}

.chart-total {
  display: inline-flex;
  margin: 10px 0 12px 0;
  color: var(--green-dark);
  background: var(--green-soft);
  border: 1px solid var(--green-line);
  border-radius: 999px;
  padding: 8px 13px;
  font-size: 14px;
  font-weight: 800;
}

.chart-scroll {
  width: 100%;
  overflow-x: auto;
  overflow-y: hidden;
  padding: 8px 0 14px 0;
  box-sizing: border-box;
}

.chart-scroll::-webkit-scrollbar,
.calendar-section::-webkit-scrollbar {
  height: 8px;
}

.chart-scroll::-webkit-scrollbar-thumb,
.calendar-section::-webkit-scrollbar-thumb {
  background: #b8d9c4;
  border-radius: 999px;
}

.bar-chart {
  display: flex;
  align-items: flex-end;
  min-height: 330px;
  padding: 22px 10px 12px 10px;
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
  font-weight: 800;
  color: var(--green-dark);
  margin-bottom: 7px;
}

.bar {
  background: linear-gradient(180deg, #45bf68 0%, var(--green) 100%);
  border-radius: 10px 10px 5px 5px;
  min-height: 3px;
  box-shadow: inset 0 1px 0 rgba(255,255,255,0.35);
}

.bar-label {
  font-size: 11px;
  font-weight: 700;
  color: var(--muted);
  margin-top: 9px;
  white-space: nowrap;
}

.hour-chart {
  justify-content: center;
  gap: 5px;
  width: max-content;
  min-width: 100%;
  margin-left: auto;
  margin-right: auto;
}

.hour-chart .bar-group {
  width: 42px;
}

.hour-chart .bar {
  width: 25px;
}

.month-chart {
  justify-content: center;
  gap: 4px;
  width: max-content;
  min-width: 100%;
  margin-left: auto;
  margin-right: auto;
}

.month-chart .bar-group {
  width: 33px;
}

.month-chart .bar {
  width: 22px;
}

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

.week-chart .bar-value,
.year-chart .bar-value {
  font-size: 15px;
}

.week-chart .bar-label,
.year-chart .bar-label {
  font-size: 15px;
}

.calendar-section {
  width: 100%;
  overflow-x: auto;
}

.calendar-nav {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 14px;
  margin-bottom: 18px;
  min-width: 720px;
}

.calendar-nav h2 {
  margin: 0;
  font-size: 25px;
  letter-spacing: -0.03em;
  color: var(--text);
}

.calendar-nav a {
  position: relative;
  background: linear-gradient(135deg, var(--green-dark) 0%, var(--green) 100%);
  color: white;
  padding: 12px 17px;
  border-radius: 999px;
  text-decoration: none;
  font-weight: 900;
  font-size: 14px;
  box-shadow: 0 10px 22px rgba(12, 98, 50, 0.24), 0 1px 0 rgba(255, 255, 255, 0.35) inset;
  border: 1px solid rgba(255, 255, 255, 0.28);
  transition: transform 0.2s ease, box-shadow 0.2s ease, filter 0.2s ease;
}

.calendar-nav a:hover {
  transform: translateY(-3px) scale(1.03);
  filter: brightness(1.05);
  box-shadow: 0 16px 30px rgba(12, 98, 50, 0.30), 0 0 0 5px rgba(42, 173, 92, 0.12);
}

.calendar-nav a:active {
  transform: translateY(0) scale(0.98);
}

.calendar {
  border-collapse: separate;
  border-spacing: 0;
  width: 100%;
  min-width: 720px;
  background: white;
  border: 1px solid var(--green-line);
  border-radius: 20px;
  overflow: hidden;
}

.calendar th {
  background: #f0f8f3;
  color: var(--green-dark);
  padding: 13px 10px;
  font-size: 13px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  border-bottom: 1px solid var(--green-line);
}

.calendar td {
  border-right: 1px solid #d8efdf;
  border-bottom: 1px solid #d8efdf;
  height: 132px;
  width: 14%;
  vertical-align: top;
  padding: 10px;
}

.calendar tr:last-child td {
  border-bottom: 0;
}

.calendar td:last-child {
  border-right: 0;
}

.empty {
  background: #f7fbf8;
}

.day {
  background: #ffffff;
}

.today-cell {
  background: linear-gradient(180deg, #e7f8ed 0%, #ffffff 100%);
  box-shadow: inset 0 0 0 2px var(--green);
}

.day-number {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 28px;
  height: 28px;
  border-radius: 999px;
  font-weight: 850;
  font-size: 15px;
  color: var(--text);
  background: #eef8f1;
}

.step-number {
  font-size: 22px;
  font-weight: 850;
  color: var(--green-dark);
  margin-top: 12px;
  letter-spacing: -0.03em;
}

.step-label {
  font-size: 12px;
  font-weight: 700;
  color: var(--muted);
}

.sleep-number {
  font-size: 13px;
  font-weight: 800;
  color: var(--text);
  margin-top: 8px;
}

.intensity-label {
  font-size: 12px;
  color: var(--muted);
  margin-top: 4px;
  font-weight: 650;
}

.bottom-metrics {
  grid-template-columns: repeat(3, minmax(0, 1fr));
  margin-top: 0;
  margin-bottom: 16px;
}

.note {
  background: rgba(223, 244, 231, 0.75);
  border: 1px solid var(--green-line);
  color: var(--muted);
  border-radius: 16px;
  padding: 14px 16px;
  font-size: 14px;
  line-height: 1.45;
  margin-bottom: 26px;
}

@media (max-width: 980px) {
  body {
    padding: 14px;
  }

  .stats-container {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .bottom-metrics {
    grid-template-columns: repeat(3, minmax(0, 1fr));
  }

  .hero {
    padding: 24px;
    border-radius: 24px;
  }

  .panel,
  .chart-card {
    padding: 18px;
    border-radius: 22px;
  }

  .year-chart {
    justify-content: flex-start;
    width: max-content;
  }
}

@media (max-width: 640px) {
  body {
    padding: 10px;
  }

  .hero {
    padding: 22px;
    border-radius: 22px;
  }

  .hero-subtitle {
    font-size: 14px;
  }

  .stats-container,
  .bottom-metrics {
    grid-template-columns: 1fr;
    gap: 12px;
  }

  .stat-card {
    min-height: 104px;
    padding: 16px;
  }

  .stat-card::before {
    margin-bottom: 8px;
  }

  .stat-card.clickable::after {
    top: 12px;
    right: 12px;
    padding: 4px 8px;
    font-size: 10px;
  }

  .chart-card {
    padding: 16px 10px;
  }

  .hour-chart,
  .month-chart,
  .week-chart,
  .year-chart {
    justify-content: flex-start;
    width: max-content;
  }

  .week-chart {
    gap: 18px;
  }

  .year-chart {
    gap: 10px;
  }

  .section-heading {
    align-items: flex-start;
    flex-direction: column;
  }

  .calendar-nav {
    min-width: 680px;
  }

  .calendar {
    min-width: 680px;
  }
}

/* Special app-style effects */
.paw-bg {
  position: fixed;
  inset: 0;
  pointer-events: none;
  overflow: hidden;
  z-index: 0;
}

.paw {
  position: absolute;
  font-size: 42px;
  opacity: 0.08;
  animation: floatPaw 9s ease-in-out infinite;
}

.paw.one {
  top: 14%;
  left: 7%;
}

.paw.two {
  top: 45%;
  right: 8%;
  animation-delay: 2s;
}

.paw.three {
  bottom: 10%;
  left: 18%;
  animation-delay: 4s;
}

@keyframes floatPaw {
  0%, 100% {
    transform: translateY(0px) rotate(-8deg);
  }
  50% {
    transform: translateY(-18px) rotate(8deg);
  }
}

.app-shell {
  position: relative;
  z-index: 1;
}

.live-pill {
  display: inline-flex;
  align-items: center;
  gap: 9px;
  margin-top: 18px;
  background: rgba(255, 255, 255, 0.18);
  color: white;
  border: 1px solid rgba(255, 255, 255, 0.28);
  border-radius: 999px;
  padding: 9px 14px;
  font-weight: 800;
  font-size: 13px;
  backdrop-filter: blur(10px);
}

.live-dot {
  width: 10px;
  height: 10px;
  background: #7dff9f;
  border-radius: 50%;
  box-shadow: 0 0 0 0 rgba(125, 255, 159, 0.75);
  animation: pulseDot 1.5s infinite;
}

@keyframes pulseDot {
  0% {
    box-shadow: 0 0 0 0 rgba(125, 255, 159, 0.75);
  }
  70% {
    box-shadow: 0 0 0 10px rgba(125, 255, 159, 0);
  }
  100% {
    box-shadow: 0 0 0 0 rgba(125, 255, 159, 0);
  }
}

.smart-panel {
  margin: 0 auto 24px auto;
}

.smart-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 14px;
}

.smart-card {
  position: relative;
  overflow: hidden;
  background: linear-gradient(180deg, #ffffff 0%, #f3fff6 100%);
  border: 1px solid rgba(20, 138, 71, 0.16);
  border-radius: var(--radius-md);
  box-shadow: 0 12px 26px rgba(12, 98, 50, 0.10);
  padding: 18px;
  text-align: left;
  transition: transform 0.2s ease, box-shadow 0.2s ease;
}

.smart-card:hover {
  transform: translateY(-4px);
  box-shadow: 0 18px 36px rgba(12, 98, 50, 0.16);
}

.smart-card::after {
  content: "";
  position: absolute;
  width: 110px;
  height: 110px;
  border-radius: 50%;
  right: -42px;
  top: -48px;
  background: radial-gradient(circle, rgba(42, 173, 92, 0.18), rgba(42, 173, 92, 0));
}

.smart-label {
  font-size: 12px;
  font-weight: 900;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--muted);
  margin-bottom: 8px;
}

.smart-value {
  font-size: clamp(21px, 4vw, 30px);
  font-weight: 900;
  color: var(--green-dark);
  letter-spacing: -0.04em;
}

.smart-note {
  font-size: 13px;
  color: var(--muted);
  margin-top: 6px;
  line-height: 1.35;
}

.progress-track {
  height: 12px;
  background: #dff3e5;
  border-radius: 999px;
  overflow: hidden;
  margin-top: 12px;
  border: 1px solid rgba(20, 138, 71, 0.12);
}

.progress-fill {
  height: 100%;
  width: 0%;
  background: linear-gradient(90deg, var(--green-dark), var(--green), #7ee29b);
  border-radius: 999px;
  transition: width 0.45s ease;
}

.stat-card.clickable {
  isolation: isolate;
}

.stat-card.clickable::before {
  transition: transform 0.25s ease, box-shadow 0.25s ease;
}

.stat-card.clickable:hover::before {
  transform: scaleX(1.45);
  box-shadow: 0 4px 18px rgba(20, 138, 71, 0.40);
}

.stat-card.clickable.active {
  animation: selectedGlow 1.8s ease-in-out infinite;
}

@keyframes selectedGlow {
  0%, 100% {
    box-shadow: 0 18px 42px rgba(20, 138, 71, 0.22), 0 0 0 5px rgba(42, 173, 92, 0.10);
  }
  50% {
    box-shadow: 0 22px 52px rgba(20, 138, 71, 0.30), 0 0 0 7px rgba(42, 173, 92, 0.16);
  }
}

@media (max-width: 900px) {
  .smart-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 560px) {
  .smart-grid {
    grid-template-columns: 1fr;
  }

  .paw {
    font-size: 34px;
  }
}


.profile-actions-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px;
  margin-top: 16px;
}

.active-profile-pill {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  background: rgba(255, 255, 255, 0.17);
  color: white;
  border: 1px solid rgba(255, 255, 255, 0.26);
  border-radius: 999px;
  padding: 10px 14px;
  font-size: 14px;
  font-weight: 800;
  backdrop-filter: blur(10px);
}

.active-profile-pill span {
  opacity: 0.82;
  font-weight: 700;
}

.manage-profile-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: var(--green-dark);
  background: rgba(255, 255, 255, 0.94);
  border: 1px solid rgba(255, 255, 255, 0.62);
  border-radius: 999px;
  padding: 10px 14px;
  text-decoration: none;
  font-weight: 900;
  font-size: 14px;
  box-shadow: 0 12px 24px rgba(0, 0, 0, 0.12);
  transition: transform 0.16s ease, box-shadow 0.16s ease;
}

.manage-profile-btn:hover {
  transform: translateY(-3px);
  box-shadow: 0 18px 30px rgba(0, 0, 0, 0.16);
}

@media (max-width: 560px) {
  .profile-actions-row {
    align-items: stretch;
  }

  .active-profile-pill,
  .manage-profile-btn {
    width: 100%;
  }
}

</style>
</head>

<body>
<div class="app-shell">

<div class="paw-bg">
  <div class="paw one">🐾</div>
  <div class="paw two">🐾</div>
  <div class="paw three">🐾</div>
</div>


<header class="hero">
  <div class="hero-content">
    <div class="eyebrow">Live ESP32 Dashboard</div>
    <h1>Dog Activity Tracker</h1>
    <div class="creator">by Santosh Shanmugam</div>
    <p class="hero-subtitle">Track steps, view activity patterns, compare days, and estimate rest using your dog tracker data.</p>

    <div class="live-pill">
      <span class="live-dot"></span>
      Live tracker connected
    </div>

    <div class="profile-actions-row">
      <div class="active-profile-pill">
        Viewing: <strong>)rawliteral";

  page += activeProfileName;

  page += R"rawliteral(</strong>
        <span>)rawliteral";

  page += activeProfileBreed;

  page += R"rawliteral(</span>
      </div>
      <a class="manage-profile-btn" href="/?profiles=1">Switch / Add Dog</a>
    </div>
  </div>
</header>

<div class="section-heading">
  <div>
    <h2>Activity Overview</h2>
    <p>Tap any step card to change the chart below.</p>
  </div>
</div>

<div class="stats-container">

<div class="stat-card clickable" data-chart="today" onclick="showChart('today')">
  <div>
    <div class="stat-label">Steps Today</div>
    <div class="stat-number">
)rawliteral";

  page += String(stepsToday);

  page += R"rawliteral(
    </div>
  </div>
</div>

<div class="stat-card clickable" data-chart="yesterday" onclick="showChart('yesterday')">
  <div>
    <div class="stat-label">Steps Yesterday</div>
    <div class="stat-number">
)rawliteral";

  page += String(yesterdaySteps);

  page += R"rawliteral(
    </div>
  </div>
</div>

<div class="stat-card clickable" data-chart="lastWeek" onclick="showChart('lastWeek')">
  <div>
    <div class="stat-label">Steps Last Week</div>
    <div class="stat-number">
)rawliteral";

  page += String(lastWeekSteps);

  page += R"rawliteral(
    </div>
  </div>
</div>

<div class="stat-card clickable" data-chart="thisMonth" onclick="showChart('thisMonth')">
  <div>
    <div class="stat-label">Steps This Month</div>
    <div class="stat-number">
)rawliteral";

  page += String(thisMonthSteps);

  page += R"rawliteral(
    </div>
  </div>
</div>

<div class="stat-card clickable" data-chart="thisYear" onclick="showChart('thisYear')">
  <div>
    <div class="stat-label">Steps This Year</div>
    <div class="stat-number">
)rawliteral";

  page += String(thisYearSteps);

  page += R"rawliteral(
    </div>
  </div>
</div>

</div>

<section class="chart-card">
  <div class="section-heading">
    <div>
      <h2 id="chartTitle">Steps Today by Hour</h2>
      <div class="chart-total" id="chartTotal"></div>
    </div>
  </div>
  <div class="chart-scroll">
    <div class="bar-chart" id="barChart"></div>
  </div>
</section>

<section class="panel smart-panel">
  <div class="section-heading">
    <div>
      <h2>Smart Activity Insights</h2>
      <p>This updates based on the chart you select.</p>
    </div>
  </div>

  <div class="smart-grid">
    <div class="smart-card">
      <div class="smart-label">Dog Mode</div>
      <div class="smart-value" id="dogMode">Loading</div>
      <div class="smart-note" id="dogModeNote">Checking activity...</div>
    </div>

    <div class="smart-card">
      <div class="smart-label">Best Period</div>
      <div class="smart-value" id="bestPeriod">--</div>
      <div class="smart-note" id="bestPeriodNote">Most active time will appear here.</div>
    </div>

    <div class="smart-card">
      <div class="smart-label">Activity Score</div>
      <div class="smart-value" id="activityScore">0%</div>
      <div class="progress-track">
        <div class="progress-fill" id="activityProgress"></div>
      </div>
      <div class="smart-note">Based on the selected chart.</div>
    </div>

    <div class="smart-card">
      <div class="smart-label">Quick Summary</div>
      <div class="smart-value" id="quickSummary">--</div>
      <div class="smart-note" id="quickSummaryNote">Tap a card above to compare activity.</div>
    </div>
  </div>
</section>

<section class="panel calendar-section">
  <div class="section-heading">
    <div>
      <h2>Calendar History</h2>
      <p>Each day shows saved steps, sleep estimate, and intensity.</p>
    </div>
  </div>
)rawliteral";

  page += makeCalendarHTML(displayYear, displayMonth);

  page += R"rawliteral(
</section>

<div class="section-heading">
  <div>
    <h2>Rest & Intensity</h2>
    <p>Sleep estimates ignore hours where the tracker did not record data.</p>
  </div>
</div>

<div class="stats-container bottom-metrics">

<div class="stat-card">
  <div>
    <div class="stat-label">Sleep Today</div>
    <div class="stat-number">
)rawliteral";

  page += sleepTodayDisplay;

  page += R"rawliteral(
    </div>
  </div>
</div>

<div class="stat-card">
  <div>
    <div class="stat-label">Sleep Yesterday</div>
    <div class="stat-number">
)rawliteral";

  page += sleepYesterdayDisplay;

  page += R"rawliteral(
    </div>
  </div>
</div>

<div class="stat-card">
  <div>
    <div class="stat-label">Activity Intensity</div>
    <div class="stat-text">
)rawliteral";

  page += activityIntensityDisplay;

  page += R"rawliteral(
    </div>
  </div>
</div>

</div>

<div class="note">
  Sleep is estimated only from hours the tracker actually recorded. Missing hours are ignored, and quiet hours count as sleep only when they are part of a longer quiet block.
</div>

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

  updateSmartPanel(type, chart, totalSteps, maxValue);

  chartBox.innerHTML = "";
  chartBox.className = "bar-chart " + chart.chartClass;

  document.querySelectorAll(".stat-card.clickable").forEach((card) => {
    if (card.getAttribute("data-chart") === type) {
      card.classList.add("active");
    } else {
      card.classList.remove("active");
    }
  });

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


function updateSmartPanel(type, chart, totalSteps, maxValue) {
  const dogMode = document.getElementById("dogMode");
  const dogModeNote = document.getElementById("dogModeNote");
  const bestPeriod = document.getElementById("bestPeriod");
  const bestPeriodNote = document.getElementById("bestPeriodNote");
  const activityScore = document.getElementById("activityScore");
  const activityProgress = document.getElementById("activityProgress");
  const quickSummary = document.getElementById("quickSummary");
  const quickSummaryNote = document.getElementById("quickSummaryNote");

  if (!dogMode || !dogModeNote || !bestPeriod || !bestPeriodNote || !activityScore || !activityProgress || !quickSummary || !quickSummaryNote) {
    return;
  }

  let bestIndex = 0;

  chart.values.forEach((value, index) => {
    if (value > chart.values[bestIndex]) {
      bestIndex = index;
    }
  });

  const activePeriods = chart.values.filter(value => value > 0).length;
  const average = chart.values.length > 0 ? Math.round(totalSteps / chart.values.length) : 0;

  bestPeriod.textContent = chart.labels[bestIndex];
  bestPeriodNote.textContent = maxValue + " steps during this period.";

  let score = 0;

  if (type === "today" || type === "yesterday") {
    score = Math.min(100, Math.round((totalSteps / 2000) * 100));
  } else if (type === "lastWeek") {
    score = Math.min(100, Math.round((totalSteps / 12000) * 100));
  } else if (type === "thisMonth") {
    score = Math.min(100, Math.round((totalSteps / 40000) * 100));
  } else if (type === "thisYear") {
    score = Math.min(100, Math.round((totalSteps / 300000) * 100));
  }

  activityScore.textContent = score + "%";
  activityProgress.style.width = score + "%";

  if (totalSteps === 0) {
    dogMode.textContent = "No Data";
    dogModeNote.textContent = "The tracker has not saved steps for this view yet.";
    quickSummary.textContent = "Waiting";
    quickSummaryNote.textContent = "Once steps are recorded, this panel will update.";
  } else if (score < 25) {
    dogMode.textContent = "Chill";
    dogModeNote.textContent = "Low activity for this selected period.";
    quickSummary.textContent = "Light Day";
    quickSummaryNote.textContent = "Activity is present, but not very high.";
  } else if (score < 60) {
    dogMode.textContent = "Active";
    dogModeNote.textContent = "Good movement pattern for this period.";
    quickSummary.textContent = "Solid";
    quickSummaryNote.textContent = activePeriods + " active periods detected.";
  } else {
    dogMode.textContent = "Zoomies";
    dogModeNote.textContent = "High movement detected. Your dog was busy.";
    quickSummary.textContent = "High Energy";
    quickSummaryNote.textContent = "Average of " + average + " steps per period.";
  }
}

showChart("today");
</script>

</div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", page);
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

  server.on("/api/dashboard", HTTP_GET, handleDashboardAPI);
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