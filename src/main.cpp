/*
 * Brisbane 2032 Counter
 * Copyright (C) 2026 Sam Greadly
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <RTClib.h>
#include "secrets.h"
#include "quotes.h"

// -------- E-PAPER PINS --------
#define EPD_CS   D7
#define EPD_DC   D5
#define EPD_RST  D4
#define EPD_BUSY D3 // Moved from D6 - https://github.com/Seeed-Studio/platform-seeedboards/issues/46
#define EPD_SCK  D8
#define EPD_MOSI D10

// -------- RTC PINS --------
#define RTC_SDA  D0
#define RTC_SCL  D1
#define RTC_INT  D9 

// -------- TARGET DATE --------
#define TARGET_YEAR  2032
#define TARGET_MONTH 7
#define TARGET_DAY   23

// -------- BATTERY ADC --------
#define BAT_ADC  A2





// -------- WEATHER API --------
#define WEATHER_URL "https://api.open-meteo.com/v1/forecast" \
  "?latitude=-27.4698&longitude=153.0251" \
  "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code" \
  "&timezone=Australia%2FBrisbane"

// -------- DISPLAY --------
#define DRIVER_CLASS GxEPD2_290_GDEY029T94
GxEPD2_BW<DRIVER_CLASS, DRIVER_CLASS::HEIGHT>
  display(DRIVER_CLASS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

RTC_DS3231 rtc;

// -------- WEATHER STATE --------
struct WeatherData {
  float temperature;
  float feelsLike;
  int   humidity;
  int   weatherCode;
  bool  valid;
};

// -------- SYNC RTC TO NTP --------
void syncRTCfromNTP() {
  configTime(10 * 3600, 0, "au.pool.ntp.org", "time.google.com");
  // AEST = UTC+10, no DST (adjust to 11*3600 in summer for AEDT)

  Serial.print("Waiting for NTP...");
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo, 1000) && attempts < 15) {
    Serial.print(".");
    attempts++;
  }

  if (attempts >= 15) {
    Serial.println("NTP failed, keeping RTC time.");
    return;
  }

  // Set RTC from NTP
  DateTime ntpTime(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  );
  rtc.adjust(ntpTime);
  Serial.printf("\nRTC synced: %02d/%02d/%04d %02d:%02d:%02d\n",
    ntpTime.day(), ntpTime.month(), ntpTime.year(),
    ntpTime.hour(), ntpTime.minute(), ntpTime.second());
}

// -------- READ BATTERY VOLTAGE --------
// Two 100k resistors = 1:2 divider, so multiply ADC voltage by 2
// ADC is 12-bit (0-4095) at 3.3V reference
// Average 16 samples to reduce noise
int readBatteryPercent() {
  analogSetAttenuation(ADC_11db);
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogReadMilliVolts(BAT_ADC);
    delay(5);
  }
  float adcMv = sum / 16.0;

  // Two calibration factors depending on power source
  // USB+battery: ADC overcounts (4.20 vs real 4.12) → factor 0.981
  // Battery only: ADC undercounts (3.78 vs real 4.10) → factor 1.085
  // Detect by checking if 5V USB is present on the 5V pin
  // When on USB, analogRead of the 5V pin will be high
  // Simpler: use supply voltage — on USB it's ~4.5V+, on battery ~3.7-4.2V
  // We can detect via the ADC reading itself — if uncorrected voltage > 4.25V, USB is present
  float rawVoltage = (adcMv / 1000.0) * 2.0;
  
  float CAL_FACTOR;
  
  // USB+battery
  CAL_FACTOR = 0.983;

  float voltage = rawVoltage * CAL_FACTOR;
  int percent = (int)((voltage - 3.0) / (4.2 - 3.0) * 100.0);
  percent = constrain(percent, 0, 100);

  Serial.printf("Battery ADC: %.0fmV, raw: %.2fV, actual: %.2fV = %d%%\n",
    adcMv, rawVoltage, voltage, percent);
  return percent;
}

// -------- WMO WEATHER CODE HELPERS --------
const char* weatherDescription(int code) {
  if (code == 0)  return "Clear";
  if (code == 1)  return "Mostly clear";
  if (code == 2)  return "Part cloudy";
  if (code == 3)  return "Overcast";
  if (code <= 49) return "Foggy";
  if (code <= 59) return "Drizzle";
  if (code <= 69) return "Rainy";
  if (code <= 79) return "Snowy";
  if (code <= 84) return "Showers";
  if (code <= 99) return "Stormy";
  return "Unknown";
}

int weatherIcon(int code) {
  if (code == 0 || code == 1) return 0;
  if (code == 2)              return 1;
  if (code == 3)              return 2;
  if (code <= 69)             return 3;
  if (code <= 79)             return 2;
  if (code <= 84)             return 3;
  return 4;
}

// -------- DRAW WEATHER ICON --------
void drawWeatherIcon(int type, int cx, int cy) {
  switch (type) {
    case 0: // sun
      display.fillCircle(cx, cy, 4, GxEPD_WHITE);
      for (int i = 0; i < 8; i++) {
        float a = i * PI / 4;
        display.drawLine(cx+cos(a)*6, cy+sin(a)*6,
                         cx+cos(a)*8, cy+sin(a)*8, GxEPD_WHITE);
      }
      break;
    case 1: // partly cloudy
      display.fillCircle(cx-2, cy-1, 3, GxEPD_WHITE);
      for (int i = 0; i < 6; i++) {
        float a = i * PI / 3 - PI/6;
        display.drawLine(cx-2+cos(a)*5, cy-1+sin(a)*5,
                         cx-2+cos(a)*7, cy-1+sin(a)*7, GxEPD_WHITE);
      }
      display.fillCircle(cx+2, cy+2, 3, GxEPD_WHITE);
      display.fillCircle(cx+5, cy+3, 2, GxEPD_WHITE);
      display.fillRect(cx+2, cy+3, 4, 3, GxEPD_WHITE);
      break;
    case 2: // cloud
      display.fillCircle(cx-2, cy,   4, GxEPD_WHITE);
      display.fillCircle(cx+2, cy-1, 3, GxEPD_WHITE);
      display.fillCircle(cx+5, cy+1, 3, GxEPD_WHITE);
      display.fillRect(cx-2, cy, 8, 4,  GxEPD_WHITE);
      break;
    case 3: // rain
      display.fillCircle(cx-2, cy-1, 4, GxEPD_WHITE);
      display.fillCircle(cx+2, cy-2, 3, GxEPD_WHITE);
      display.fillCircle(cx+5, cy,   3, GxEPD_WHITE);
      display.fillRect(cx-2, cy-1, 8, 4, GxEPD_WHITE);
      display.drawLine(cx,   cy+4, cx-1, cy+7, GxEPD_WHITE);
      display.drawLine(cx+3, cy+4, cx+2, cy+7, GxEPD_WHITE);
      break;
    case 4: // storm
      display.fillCircle(cx-2, cy-1, 4, GxEPD_WHITE);
      display.fillCircle(cx+2, cy-2, 3, GxEPD_WHITE);
      display.fillCircle(cx+5, cy,   3, GxEPD_WHITE);
      display.fillRect(cx-2, cy-1, 8, 4, GxEPD_WHITE);
      display.drawLine(cx+1, cy+3, cx-1, cy+6, GxEPD_WHITE);
      display.drawLine(cx-1, cy+6, cx+1, cy+6, GxEPD_WHITE);
      display.drawLine(cx+1, cy+6, cx-1, cy+9, GxEPD_WHITE);
      break;
  }
}

// -------- WIFI + WEATHER FETCH --------
WeatherData fetchWeather() {
  WeatherData result = {0, 0, 0, 0, false};

  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return result;
  }
  Serial.println("\nWiFi connected.");

  // Retry up to 3 times
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Weather fetch attempt %d...\n", attempt);

    HTTPClient http;
    http.begin(WEATHER_URL);
    http.setTimeout(10000);  // 10 second timeout
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload);
      if (!err) {
        result.temperature = doc["current"]["temperature_2m"].as<float>();
        result.feelsLike   = doc["current"]["apparent_temperature"].as<float>();
        result.humidity    = doc["current"]["relative_humidity_2m"].as<int>();
        result.weatherCode = doc["current"]["weather_code"].as<int>();
        result.valid = true;
        Serial.printf("Weather: %.1f°C (feels %.1f°C), humidity %d%%, code %d (%s)\n",
          result.temperature, result.feelsLike, result.humidity,
          result.weatherCode, weatherDescription(result.weatherCode));
        http.end();
        break;  // success — stop retrying
      } else {
        Serial.println("JSON parse error.");
      }
    } else {
      Serial.printf("HTTP error: %d\n", httpCode);
    }

    http.end();

    if (attempt < 3) {
      Serial.println("Retrying in 5 seconds...");
      delay(5000);
    }
  }

  return result;
}

// -------- HELPERS --------
void printCentred(const char* text, int regionX, int regionW, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(regionX + (regionW - w) / 2, y);
  display.print(text);
}

long daysUntilTarget(DateTime now) {
  DateTime target(TARGET_YEAR, TARGET_MONTH, TARGET_DAY, 0, 0, 0);
  TimeSpan diff = target - now;
  return diff.days();
}

// -------- DRAW BATTERY ICON --------
// Draws a small battery icon at position x,y
// percent: 0-100
void drawBatteryIcon(int x, int y, int percent, bool inverted = false) {
  uint16_t fg = inverted ? GxEPD_WHITE : GxEPD_BLACK;
  uint16_t bg = inverted ? GxEPD_BLACK : GxEPD_WHITE;

  // Tip at top
  display.fillRect(x + 1, y, 4, 2, fg);
  // Body outline
  display.drawRect(x, y + 2, 6, 9, fg);
  // Clear inside first
  display.fillRect(x + 1, y + 3, 4, 7, bg);
  // Fill from bottom up relative to percent
  int fillHeight = (int)(7.0 * percent / 100.0);
  if (fillHeight > 0) {
    display.fillRect(x + 1, y + 2 + (8 - fillHeight), 4, fillHeight, fg);
  }
}

// -------- WORD WRAP QUOTE --------
// Draws text word-wrapped into the right panel
// Returns the Y position after the last line drawn
int drawWrapped(const char* text, int x, int maxWidth, int startY, int lineHeight) {
  char buf[128];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  int y = startY;
  char line[64] = "";
  char* word = strtok(buf, " ");

  while (word != nullptr) {
    char test[64];
    snprintf(test, sizeof(test), "%s%s%s",
      line, (strlen(line) > 0 ? " " : ""), word);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(test, x, y, &x1, &y1, &w, &h);

    if (w > maxWidth && strlen(line) > 0) {
      // Current line is full — draw it and start a new one
      display.setCursor(x, y);
      display.print(line);
      y += lineHeight;
      snprintf(line, sizeof(line), "%s", word);
    } else {
      snprintf(line, sizeof(line), "%s", test);
    }
    word = strtok(nullptr, " ");
  }

  // Draw last line
  if (strlen(line) > 0) {
    display.setCursor(x, y);
    display.print(line);
    y += lineHeight;
  }

  return y;
}


// Calculate seconds until next wake time
void sleepUntilNextWake(DateTime now) {
  // Determine next wake — 7:00am or 1:00pm
  int wakeHour, wakeMinute = 0;
  
  if (now.hour() < 7) {
    wakeHour = 7;
  } else if (now.hour() < 13) {
    wakeHour = 13;
  } else {
    // After 1pm — sleep until 7am tomorrow
    wakeHour = 7;
  }

  // Calculate seconds until wake time
  int nowSeconds  = now.hour() * 3600 + now.minute() * 60 + now.second();
  int wakeSeconds = wakeHour * 3600 + wakeMinute * 60;

  long sleepSeconds;
  if (wakeSeconds > nowSeconds) {
    sleepSeconds = wakeSeconds - nowSeconds;
  } else {
    // Tomorrow
    sleepSeconds = (86400 - nowSeconds) + wakeSeconds;
  }

  Serial.printf("Sleeping for %ld seconds until %02d:%02d\n",
    sleepSeconds, wakeHour, wakeMinute);
  Serial.flush();
  delay(100);

  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}

// -------- NEXT WAKE TIME --------
void getNextWakeTime(DateTime now, int &nextHour, int &nextMinute) {

  nextMinute = 0;
  if (now.hour() < 7) {
    nextHour = 7;
  } else if (now.hour() < 13) {
    nextHour = 13;
  } else {
    nextHour = 7;  // tomorrow
  }
}

// -------- SET NEXT ALARM --------
void setNextAlarm(DateTime now) {
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.disableAlarm(2);

  int nextHour, nextMinute;
  getNextWakeTime(now, nextHour, nextMinute);

  rtc.setAlarm1(
    DateTime(now.year(), now.month(), now.day(), nextHour, nextMinute, 0),
    DS3231_A1_Hour
  );

  Serial.printf("Next wake alarm set: %02d:%02d\n", nextHour, nextMinute);
}

// -------- DRAW SCREEN --------
void drawScreen(DateTime now, long daysLeft, int quoteIndex, WeatherData weather, int batteryPercent) {
  long weeksLeft = daysLeft / 7;

  int hour12 = now.hour() % 12;
  if (hour12 == 0) hour12 = 12;
  const char* ampm = now.hour() < 12 ? "am" : "pm";

  char updatedBuf[20];
  snprintf(updatedBuf, sizeof(updatedBuf), "%02d/%02d/%02d %d:%02d%s",
    now.day(), now.month(), now.year() % 100,
    hour12, now.minute(), ampm);

  char daysBuf[10];
  if (daysLeft >= 1000) {
    snprintf(daysBuf, sizeof(daysBuf), "%d,%03d",
      (int)(daysLeft / 1000), (int)(daysLeft % 1000));
  } else {
    snprintf(daysBuf, sizeof(daysBuf), "%ld", daysLeft);
  }

  char weeksBuf[20];
  snprintf(weeksBuf, sizeof(weeksBuf), "%ld weeks", weeksLeft);

  // Next wake time for footer
  int nextHour, nextMinute;
  getNextWakeTime(now, nextHour, nextMinute);
  int next12 = nextHour % 12;
  if (next12 == 0) next12 = 12;
  const char* nextAmpm = nextHour < 12 ? "am" : "pm";
  char nextBuf[12];
  snprintf(nextBuf, sizeof(nextBuf), "%d:%02d%s", next12, nextMinute, nextAmpm);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // ============================================================
    // TOPBAR
    // ============================================================
    display.fillRect(0, 0, 296, 18, GxEPD_BLACK);
    int iconType = weather.valid ? weatherIcon(weather.weatherCode) : 0;
    drawWeatherIcon(iconType, 9, 9);
    display.setFont(NULL);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(20, 6);
    if (weather.valid) {
      display.print(weatherDescription(weather.weatherCode));
      display.print(" ");
      display.print((int)round(weather.temperature));
      display.write(0xF8);
      display.print("C FL:");
      display.print((int)round(weather.feelsLike));
      display.write(0xF8);
      display.print("C H:");
      display.print(weather.humidity);
      display.print("%");
    } else {
      display.print("Weather unavailable");
    }
    
    // Battery voltage + icon + percent (right side of topbar, white on black)
    float rawV = (batteryPercent / 100.0) * (4.2 - 3.0) + 3.0;
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%.2fV", rawV);
    
    // Voltage text first
    display.setCursor(220, 6);
    display.print(batBuf);
    
    // Then battery icon
    drawBatteryIcon(258, 5, batteryPercent, true);
    
    // Then percent
    char pctBuf[6];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", batteryPercent);
    display.setCursor(266, 6);
    display.print(pctBuf);



    // ============================================================
    // VERTICAL DIVIDER
    // ============================================================
    display.setTextColor(GxEPD_BLACK);
    display.drawLine(148, 18, 148, 116, GxEPD_BLACK);

    // ============================================================
    // LEFT PANEL
    // ============================================================
    display.setFont(&FreeSansBold24pt7b);
    display.setTextColor(GxEPD_BLACK);
    printCentred(daysBuf, 0, 148, 60);

    display.setFont(&FreeSans9pt7b);
    printCentred("days to Olympics", 0, 148, 81);
    printCentred(weeksBuf, 0, 148, 96);

    display.setFont(NULL);
    printCentred("23/07/2032", 0, 148, 102);

    // ============================================================
    // RIGHT PANEL
    // ============================================================
    int rX = 152;
    int rW = 140;  // available width (296 - 152 - 4px margin)
    const Quote& q = quotes[quoteIndex % QUOTE_COUNT];

    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);

    int afterQuote = drawWrapped(q.text, rX, rW, 32, 16);

    display.setFont(NULL);
    display.setCursor(rX, afterQuote + 2);
    display.print("- ");
    display.print(q.attribution);

    // ============================================================
    // FOOTER
    // ============================================================
    display.drawLine(0, 116, 296, 116, GxEPD_BLACK);
    display.setFont(NULL);
    display.setTextColor(GxEPD_BLACK);

    // Updated - left
    display.setCursor(2, 120);
    display.print("Updated:");
    display.print(updatedBuf);

    // Next update - right
    display.setCursor(196, 120);
    display.print("Next Update:");
    display.print(nextBuf);

  } while (display.nextPage());
}

// -------- SETUP --------
void setup() {

  Serial.begin(115200);
  
  // 10 second window on every boot for flashing/serial
  // Allows upload without needing reset button
  delay(10000);

  Serial.println("=== WAKING UP ===");

  pinMode(EPD_DC,  OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_CS,  OUTPUT);
  pinMode(RTC_INT, INPUT_PULLUP);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(2000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 50, false);
  display.setRotation(1);

  Wire.begin(RTC_SDA, RTC_SCL);
  if (!rtc.begin(&Wire)) {
    Serial.println("RTC not found!");
    while (1) delay(100);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  rtc.clearAlarm(1);
  rtc.clearAlarm(2);

  // Read battery voltage
  int batteryPercent = readBatteryPercent();

  DateTime now = rtc.now();
  long daysLeft = daysUntilTarget(now);

  const int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int dayOfYear = now.day();
  for (int m = 1; m < now.month(); m++) dayOfYear += daysInMonth[m-1];
  if (now.month() > 2 && (now.year() % 4 == 0 &&
      (now.year() % 100 != 0 || now.year() % 400 == 0))) dayOfYear++;

  Serial.printf("Time: %02d/%02d/%04d %02d:%02d\n",
    now.day(), now.month(), now.year(), now.hour(), now.minute());
  Serial.printf("Days to Brisbane 2032: %ld\n", daysLeft);
  Serial.printf("Quote index: %d\n", dayOfYear % QUOTE_COUNT);

  WeatherData weather = fetchWeather();

  syncRTCfromNTP();  // sync RTC while WiFi is still up
  
  // Clean up WiFi before sleeping
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Re-read RTC now that it's been NTP-synced
  now = rtc.now();

  //drawScreen(now, daysLeft, dayOfYear, weather);
  drawScreen(now, daysLeft, dayOfYear, weather, batteryPercent);
  
  display.hibernate();


  // On battery — use deep sleep
  // On USB — use wait loop (deep sleep causes reset loop on USB)
  // Detect USB by checking if serial is active

  int nextHour, nextMinute;
  getNextWakeTime(now, nextHour, nextMinute);

  bool onUSB = (millis() < 10000 && Serial);

  if (onUSB) {
    // Wait loop for USB development
    int nextHour, nextMinute;
    getNextWakeTime(now, nextHour, nextMinute);
    Serial.printf("USB mode — waiting until %02d:%02d\n", nextHour, nextMinute);
    Serial.println("Send any key to reset immediately.");
    while (true) {
      if (Serial.available()) {
        Serial.println("Manual reset triggered.");
        ESP.restart();
      }
      DateTime t = rtc.now();
      if (t.hour() == nextHour && t.minute() == nextMinute) break;
      delay(10000);
    }
    ESP.restart();
  } else {
    // Battery — proper deep sleep
    sleepUntilNextWake(now);
  }

   Serial.println("Waiting until next update... (send any key to reset)");
  while (true) {
    if (Serial.available()) {
      Serial.println("Manual reset triggered.");
      ESP.restart();
    }
    DateTime t = rtc.now();
    if (t.hour() == nextHour && t.minute() == nextMinute) break;
    delay(10000);
  }

  Serial.println("Time's up — restarting!");
  delay(100);
  ESP.restart();
}

void loop() {}
