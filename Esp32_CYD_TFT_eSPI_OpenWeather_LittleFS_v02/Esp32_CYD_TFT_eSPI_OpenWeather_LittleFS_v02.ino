/*
  The sketch is displaying a nice Internet Weather Station with 3 4 days forecast on an
  ESP32-Cheap Yellow Display (CYD) with 2.8-inch TFT display and 320x240 pixels resolution.

  The weather data is retrieved from OpenWeatherMap.org service and you need to get your own 
  (free) API key before running the sketch.

  Please add the key, your Wi-Fi credentials, the location coordinates for the weather information and 
  additional data about your Timezone in the settings file: 'All_Settings.h'.

  The code is not written by me, but from Daniel Eichhorn (https://blog.squix.ch) and was 
  adapted by Bodmer as an example for his OpenWeather library (https://github.com/Bodmer/OpenWeather/).

  I changed lines of code to crrect some issues, for more information see: https://github.com/Bodmer/OpenWeather/issues/26
  1):
  Search for: String date = "Updated: " + strDate(local_time); 
  Change to:  String date = "Updated: " + strDate(now());
  2)
  Search for: if ( today && id/100 == 8 && (forecast->dt[0] < forecast->sunrise || forecast->dt[0] > forecast->sunset)) id += 1000;
  Change to:  if ( today && id/100 == 8 && (now() < forecast->sunrise || now() > forecast->sunset)) id += 1000;

  The code is optimized to run on the Cheap Yellow Display ('CYD') with a screen resolution of 320 x 240 pixels in Landscape
  orientation, that is the 'ESP32-2432S028R' variant.

  The weather icons and fonts are stored in ESP32's LittleFS filesystem, so you need to upload the files in the 'data' subfolder
  first before uploading the code and starting the device.
  See 'Upload_To_LittleFS.md' for more details about this.

              >>>       IMPORTANT TO PREVENT CRASHES      <<<
  >>>>>>  Set LittleFS to at least 1.5Mbytes before uploading files  <<<<<<  

  The sketch is using the TFT_eSPI library by Bodmer (https://github.com/Bodmer/TFT_eSPI), so please select the correct
  'User_Setups' file in the library folder. I prepare two files (see my GitHub Repository) that should work:
  For older device with one USB connector (chip driver ILI9341) use: 
    Setup801_ESP32_CYD_ILI9341_240x320.h
  New devices with two USB connectors (chip driver ST7789) require:
    Setup805_ESP32_CYD_ST7789_240x320.h

  Original by Daniel Eichhorn, see license at end of file.

*/

/*
  This sketch uses font files created from the Noto family of fonts as bitmaps
  generated from these fonts may be freely distributed:
  https://www.google.com/get/noto/

  A processing sketch to create new fonts can be found in the Tools folder of TFT_eSPI
  https://github.com/Bodmer/TFT_eSPI/tree/master/Tools/Create_Smooth_Font/Create_font
  New fonts can be generated to include language specific characters. The Noto family
  of fonts has an extensive character set coverage.

  Json streaming parser (do not use IDE library manager version) to use is here:
  https://github.com/Bodmer/JSON_Decoder
*/

#define SERIAL_MESSAGES  // For serial output weather reports
//#define SCREEN_SERVER   // For dumping screen shots from TFT
//#define RANDOM_LOCATION // Test only, selects random weather location every refresh
//#define FORMAT_LittleFS   // Wipe LittleFS and all files!

const char* PROGRAM_VERSION = "ESP32 CYD OpenWeatherMap LittleFS V02";

#include <FS.h>
#include <LittleFS.h>

#define AA_FONT_SMALL "fonts/NSBold15"  // 15 point Noto sans serif bold
#define AA_FONT_LARGE "fonts/NSBold36"  // 36 point Noto sans serif bold
/***************************************************************************************
**                          Load the libraries and settings
***************************************************************************************/
#include <Arduino.h>

#include <SPI.h>
#include <TFT_eSPI.h>  // https://github.com/Bodmer/TFT_eSPI

// Additional functions
#include "GfxUi.h"  // Attached to this sketch

// Choose library to load
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_ARCH_RP2040)
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#else
#include <WiFiNINA.h>
#endif
#else  // ESP32
#include <WiFi.h>
#endif


// check All_Settings.h for adapting to your needs
#include "All_Settings.h"

#include <JSON_Decoder.h>  // https://github.com/Bodmer/JSON_Decoder

#include <OpenWeather.h>  // Latest here: https://github.com/Bodmer/OpenWeather

#include "NTP_Time.h"  // Attached to this sketch, see that tab for library needs
// Time zone correction library: https://github.com/JChristensen/Timezone

#include <ArduinoJson.h> // Ensure this is installed via Library Manager
#include <HTTPClient.h>

/***************************************************************************************
**                          Define the globals and class instances
***************************************************************************************/

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library

OW_Weather ow;  // Weather forecast library instance

OW_forecast* forecast;

boolean booted = true;

GfxUi ui = GfxUi(&tft);  // Jpeg and bmpDraw functions

long lastDownloadUpdate = millis();
float lastPressure = 0;

/***************************************************************************************
**                          Declare prototypes
***************************************************************************************/
void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime();
void drawCurrentWeather();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
const char* getMeteoconIcon(uint16_t id, bool today);
void drawAstronomy();
void drawSeparator(uint16_t y);
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour);
String strDate(time_t unixTime);
String strTime(time_t unixTime);
void printWeather(void);
int leftOffset(String text, String sub);
int rightOffset(String text, String sub);
int splitIndex(String text);
int getNextDayIndex(void);

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Stop further decoding as image is running off bottom of screen
  if (y >= tft.height()) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);

  // Return 1 to decode next block
  return 1;
}

/***************************************************************************************
**                          Setup
***************************************************************************************/
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(PROGRAM_VERSION);

  tft.begin();
  tft.setRotation(0);  // For 320x480 screen
  tft.fillScreen(TFT_BLACK);

  if (!LittleFS.begin()) {
    Serial.println("Flash FS initialisation failed!");
    while (1) yield();  // Stay here twiddling thumbs waiting
  }
  Serial.println("\nFlash FS available!");

// Enable if you want to erase LittleFS, this takes some time!
// then disable and reload sketch to avoid reformatting on every boot!
#ifdef FORMAT_LittleFS
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.drawString("Formatting LittleFS, so wait!", 120, 195);
  LittleFS.format();
#endif

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);
  TJpgDec.setSwapBytes(true);  // May need to swap the jpg colour bytes (endianess)

  // Draw splash screen
  if (LittleFS.exists("/splash/OpenWeather.jpg") == true) {
    TJpgDec.drawFsJpg(0, 40, "/splash/OpenWeather.jpg", LittleFS);
  }

  delay(2000);

  // Clear bottom section of screen
  tft.fillRect(0, 206, 240, 320 - 206, TFT_BLACK);

  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.drawString("Original by: blog.squix.org", 120, 260);
  tft.drawString("Adapted by: Bodmer", 120, 280);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

  delay(2000);

  tft.fillRect(0, 206, 240, 320 - 206, TFT_BLACK);

  tft.drawString("Connecting to WiFi", 120, 240);
  tft.setTextPadding(240);  // Pad next drawString() text to full width to over-write old text

// Call once for ESP32 and ESP8266
#if !defined(ARDUINO_ARCH_MBED)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
#if defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_ARCH_RP2040)
    if (WiFi.status() != WL_CONNECTED) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
    delay(500);
  }
  Serial.println();

  tft.setTextDatum(BC_DATUM);
  tft.setTextPadding(240);        // Pad next drawString() text to full width to over-write old text
  tft.drawString(" ", 120, 220);  // Clear line above using set padding width
  tft.drawString("Fetching weather data...", 120, 240);

  // Fetch the time
  udp.begin(localPort);
  syncTime();

  tft.unloadFont();
}

/***************************************************************************************
**                          Loop
***************************************************************************************/
void loop() {

  // Check if we should update weather information
  if (booted || (millis() - lastDownloadUpdate > 1000UL * UPDATE_INTERVAL_SECS)) {
    updateData();
    lastDownloadUpdate = millis();
  }

  // If minute has changed then request new time from NTP server
  if (booted || minute() != lastMinute) {
    // Update displayed time first as we may have to wait for a response
    drawTime();
    lastMinute = minute();

    // Request and synchronise the local clock
    syncTime();

#ifdef SCREEN_SERVER
    screenServer();
#endif
  }

  booted = false;
}

/***************************************************************************************
**                          Fetch the weather data  and update screen
***************************************************************************************/

void updateData() {
  if (booted) drawProgress(20, "Updating time...");
  //else fillSegment(22, 22, 0, (int)(20 * 3.6), 16, TFT_NAVY);

  if (booted) drawProgress(50, "Updating conditions...");
  //else fillSegment(22, 22, 0, (int)(50 * 3.6), 16, TFT_NAVY);

  forecast = new OW_forecast;

  // 1. Get City/Country name dynamically
  forecast->city_name = getLocationName(latitude, longitude);

  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + latitude + 
               "&longitude=" + longitude + 
               "&current=temperature_2m,relative_humidity_2m,surface_pressure,weather_code,wind_speed_10m,wind_direction_10m" + 
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset" + 
               "&timezone=auto&timeformat=unixtime&wind_speed_unit=ms";

  http.begin(url);
  int httpCode = http.GET();
  bool parsed = false;

  if (httpCode == 200) {
    DynamicJsonDocument doc(20000); 
    deserializeJson(doc, http.getString());
    parsed = true;

    forecast->temp[0]         = doc["current"]["temperature_2m"];
    forecast->humidity[0]     = doc["current"]["relative_humidity_2m"];
    forecast->clouds_all[0] = doc["current"]["cloud_cover"]; // Add this line!
    // Save old pressure for the trend arrow
    if (forecast->pressure[0] > 0) lastPressure = forecast->pressure[0];
    forecast->pressure[0]     = doc["current"]["surface_pressure"];
    forecast->wind_speed[0]   = doc["current"]["wind_speed_10m"];
    forecast->wind_deg[0] = doc["current"]["wind_direction_10m"].as<int>();
    forecast->id[0]           = doc["current"]["weather_code"];
    forecast->sunrise         = doc["daily"]["sunrise"][0];
    forecast->sunset          = doc["daily"]["sunset"][0];

    for (int i = 1; i <= 4; i++) {
      int idx = i * 8;
      forecast->temp_max[idx] = doc["daily"]["temperature_2m_max"][i];
      forecast->temp_min[idx] = doc["daily"]["temperature_2m_min"][i];
      forecast->id[idx]       = doc["daily"]["weather_code"][i]; 
      forecast->dt[idx]       = doc["daily"]["sunrise"][i].as<uint32_t>(); 
    }
  }
  http.end();

  if (parsed) {
    // ONE full screen clear is enough to remove all stains
    tft.fillScreen(TFT_BLACK); 

    // --- STEP 2: DRAW CITY & COUNTRY (TOP LEFT) ---
    tft.setTextFont(2); // Use built-in Font 2 (Compact and reliable)
    tft.setTextColor(TFT_CYAN, TFT_BLACK); 
    tft.setTextDatum(BL_DATUM); 
    // Draw slightly away from the corner to avoid bezel clipping
    int cityCountrySize= tft.textWidth(forecast->city_name + " | ");
    tft.drawString(forecast->city_name + " | ", 5, 16); 

    // --- STEP 3: DRAW DATE (TOP RIGHT) ---
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(BL_DATUM); 
    
    time_t local_time = TIMEZONE.toLocal(now(), &tz1_Code);
    // Shortened year ('26) to save space for Today, Jan 3, 2026
    String dateStr = monthShortStr(month(local_time));
    dateStr += " " + String(day(local_time)) + " '" + String(year(local_time)).substring(2);
    
    tft.drawString(dateStr, cityCountrySize + 5 , 16);//draws the date based on the city and country text size .

    // Draw sub-elements
    drawCurrentWeather(); 
    drawForecast();
    drawAstronomy();
    tft.unloadFont(); // IMPORTANT: Unload before loading the next font

    // --- DRAW LARGE TEMPERATURE ---
    tft.loadFont(AA_FONT_LARGE, LittleFS);
    tft.setTextDatum(TR_DATUM); // Changed to TC_DATUM to center the big number
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    // Font ASCII code 0xB0 is a degree symbol, but o used instead in small font
    tft.setTextPadding(tft.textWidth(" -88"));  // Max width of values
    
    String weatherText = "";
    weatherText = String(forecast->temp[0], 0);  // Make it integer temperature
    tft.drawString(weatherText, 215, 95);        //  + "°" symbol is big... use o in small font
    tft.unloadFont();
  }else {
    Serial.println("Failed to get weather");
  }
  
  if (booted) booted = false;
  delete forecast;
}

/***************************************************************************************
**                          Update progress bar
***************************************************************************************/
void drawProgress(uint8_t percentage, String text) {
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(240);
  tft.drawString(text, 120, 260);

  ui.drawProgressBar(10, 269, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);

  tft.setTextPadding(0);
  tft.unloadFont();
}

/***************************************************************************************
**                          Draw the clock digits
***************************************************************************************/
void drawTime() {
  tft.loadFont(AA_FONT_LARGE, LittleFS);

  // Convert UTC to local time, returns zone code in tz1_Code, e.g "GMT"
  time_t local_time = TIMEZONE.toLocal(now(), &tz1_Code);

  String timeNow = "";

  if (hour(local_time) < 10) timeNow += "0";
  timeNow += hour(local_time);
  timeNow += ":";
  if (minute(local_time) < 10) timeNow += "0";
  timeNow += minute(local_time);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 44:44 "));  // String width + margin
  tft.drawString(timeNow, 120, 53);

  drawSeparator(51);

  tft.setTextPadding(0);

  tft.unloadFont();
}

/***************************************************************************************
**                          Draw the current weather
***************************************************************************************/
void drawCurrentWeather() {
  // 1. Get the Icon
  String weatherIcon = getMeteoconIcon(forecast->id[0], true);
  ui.drawBmp("/icon/" + weatherIcon + ".bmp", 0, 53);

  // 2. Map Weather Code to a Word (Rain, Clear, etc.)
  String legend = "Clouds";
  int code = forecast->id[0];
  if (code == 0) legend = "Clear";
  else if (code <= 3) legend = "Cloudy";
  else if (code <= 67) legend = "Rain";
  else if (code <= 99) legend = "Storm";

  // Draw the Legend
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.loadFont(AA_FONT_SMALL, LittleFS); // Ensure font is loaded
  tft.drawString(legend, 230, 75); 

  // 3. Temperature Unit Label (oC / oF)
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  if (units == "metric") tft.drawString("oC", 237, 95);
  else tft.drawString("oF", 237, 95);

  // 4. Wind Speed
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  String windSpeedText = String(forecast->wind_speed[0], 0) + (units == "metric" ? " m/s" : " mph");
  tft.setTextDatum(TC_DATUM);
  tft.drawString(windSpeedText, 124, 136);

  // 5. Pressure
  String pressureText = String(forecast->pressure[0], 0) + " hPa";
  tft.setTextDatum(TR_DATUM);
  tft.drawString(pressureText, 230, 136);
  // Draw Trend Arrow
  if (lastPressure > 0 && lastPressure != forecast->pressure[0]) {
    if (forecast->pressure[0] > lastPressure) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("↑", 230 - tft.textWidth(pressureText) - 5, 136); // Needs a small BMP or char
    } else {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("↓", 230 - tft.textWidth(pressureText) - 5, 136);
    }
  }

  
  // 6. Wind Direction Arrow
  // The original code uses 8 bitmaps: N, NE, E, SE, S, SW, W, NW
  int windAngle = (int)(forecast->wind_deg[0] + 22.5) / 45;
  if (windAngle > 7 || windAngle < 0) windAngle = 0; // Force to North if invalid
  String windDirs[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  String path = "/wind/" + windDirs[windAngle] + ".bmp";
  if (LittleFS.exists(path)) {
    Serial.println("File found: " + path);
    ui.drawBmp(path, 101, 86);
  } else {
    Serial.println("FILE NOT FOUND: " + path);
  }

  ui.drawBmp("/wind/" + windDirs[windAngle] + ".bmp", 101, 86);

  drawSeparator(153);
  tft.setTextDatum(TL_DATUM);  // Reset datum to normal
  tft.setTextPadding(0);       // Reset padding width to none
}

/***************************************************************************************
**                          Draw the 4 forecast columns
***************************************************************************************/
// draws the three forecast columns
void drawForecast() {
  // We bypass getNextDayIndex() because Open-Meteo is already sorted daily
  
  drawForecastDetail(8, 171, 8);   // Tomorrow
  drawForecastDetail(66, 171, 16);  // Day 2
  drawForecastDetail(124, 171, 24); // Day 3
  drawForecastDetail(182, 171, 32); // Day 4
  
  drawSeparator(171 + 69);
}

/***************************************************************************************
**                          Draw 1 forecast column at x, y
***************************************************************************************/
// helper for the forecast columns
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {
  // 1. Safety check
  if (dayIndex >= 40) return; 

  // 2. Fix Day Name: Use the timestamp we stored in updateData
  // We use dayIndex directly because our Open-Meteo loop fills 8, 16, 24, 32
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("WWW"));
  
  // weekday() gets the day number, dayShortStr() converts it to "Mon", "Tue", etc.
  String dayName = dayShortStr(weekday(forecast->dt[dayIndex]));
  dayName.toUpperCase();
  tft.drawString(dayName, x + 25, y);

  // 3. Fix Temperatures
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("-88   -88"));

  // Open-Meteo already gives us the daily Max/Min at the specific index
  String highTemp = String(forecast->temp_max[dayIndex], 0);
  String lowTemp = String(forecast->temp_min[dayIndex], 0);
  tft.drawString(highTemp + "  " + lowTemp, x + 25, y + 17);

  // 4. Fix Icon: False means "Daytime" for forecast icons
  String weatherIcon = getMeteoconIcon(forecast->id[dayIndex], false);
  ui.drawBmp("/icon50/" + weatherIcon + ".bmp", x, y + 18);

  tft.setTextPadding(0); 
}

/***************************************************************************************
**                          Draw Sun rise/set, Moon, cloud cover and humidity
***************************************************************************************/
void drawAstronomy() {

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Last qtr "));

  // FIX: Use current time (now()) instead of the empty forecast->dt[0]
  time_t local_time = now(); 
  uint16_t y = year(local_time);
  uint8_t m = month(local_time);
  uint8_t d = day(local_time);
  uint8_t h = hour(local_time);
  
  int ip; // Moon phase percentage
  uint8_t icon = moon_phase(y, m, d, h, &ip);

  // Draw Moon Phase Name and Icon
  tft.drawString(moonPhase[ip], 120, 319);
  ui.drawBmp("/moon/moonphase_L" + String(icon) + ".bmp", 120 - 30, 318 - 16 - 60);

  // --- SUNRISE / SUNSET SECTION ---
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0);
  
  // If "sunStr" is showing "SUN", let's force it to say "Sun" or "Solar"
  tft.drawString("Solar", 40, 270); 

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 88:88 "));

  // These come from the sunrise/sunset we saved in updateData
  String rising = strTime(forecast->sunrise) + " ";
  int dt = rightOffset(rising, ":"); 
  tft.drawString(rising, 40 + dt, 290);

  String setting = strTime(forecast->sunset) + " ";
  dt = rightOffset(setting, ":");
  tft.drawString(setting, 40 + dt, 305);

  // --- CLOUD COVER SECTION ---
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Clouds", 195, 260); 

  // Since Open-Meteo current clouds might not be in clouds_all[0], 
  // we check if we need to map that in updateData too.
  String cloudCover = String(forecast->clouds_all[0]) + "%";

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 100%"));
  tft.drawString(cloudCover, 210, 277);

  // --- HUMIDITY SECTION ---
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Humidity", 195, 298); 

  String humidity = String(forecast->humidity[0]) + "%";

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("100%"));
  tft.drawString(humidity, 210, 315);

  tft.setTextPadding(0); 
}

/***************************************************************************************
**                          Get the icon file name from the index number
***************************************************************************************/
const char* getMeteoconIcon(uint16_t id, bool today) {
  // 1. Handle Day/Night logic (same as your original reference)
  // If it's "Clear" (0) or "Partly Cloudy" (1,2,3) and it's night time, 
  // we add 1000 to the ID to pick the "night" version of the icon.
  if (today && id <= 3 && (now() < forecast->sunrise || now() > forecast->sunset)) {
    id += 1000;
  }

  // 2. Map Open-Meteo WMO Codes to your specific filenames
  // Clear Sky
  if (id == 0)    return "clear-day";
  if (id == 1000) return "clear-night";

  // Partly Cloudy
  if (id >= 1 && id <= 3)    return "partly-cloudy-day";
  if (id >= 1001 && id <= 1003) return "partly-cloudy-night";

  // Fog
  if (id == 45 || id == 48) return "fog";

  // Drizzle / Light Rain
  if (id >= 51 && id <= 55) return "drizzle";

  // Rain
  if (id >= 61 && id <= 67) return "rain";
  if (id >= 80 && id <= 82) return "rain";

  // Snow / Sleet
  if (id >= 71 && id <= 77) return "snow";
  if (id == 85 || id == 86) return "snow";

  // Thunderstorm
  if (id >= 95 && id <= 99) return "thunderstorm";

  return "unknown"; // Fallback if no code matches
}
/***************************************************************************************
**                          Draw screen section separator line
***************************************************************************************/
// if you don't want separators, comment out the tft-line
void drawSeparator(uint16_t y) {
  tft.drawFastHLine(10, y, 240 - 2 * 10, 0x4228);
}

/***************************************************************************************
**                          Determine place to split a line line
***************************************************************************************/
// determine the "space" split point in a long string
int splitIndex(String text) {
  uint16_t index = 0;
  while ((text.indexOf(' ', index) >= 0) && (index <= text.length() / 2)) {
    index = text.indexOf(' ', index) + 1;
  }
  if (index) index--;
  return index;
}

/***************************************************************************************
**                          Right side offset to a character
***************************************************************************************/
// Calculate coord delta from end of text String to start of sub String contained within that text
// Can be used to vertically right align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int rightOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(index));
}

/***************************************************************************************
**                          Left side offset to a character
***************************************************************************************/
// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically left align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int leftOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(0, index));
}

/***************************************************************************************
**                          Draw circle segment
***************************************************************************************/
// Draw a segment of a circle, centred on x,y with defined start_angle and subtended sub_angle
// Angles are defined in a clockwise direction with 0 at top
// Segment has radius r and it is plotted in defined colour
// Can be used for pie charts etc, in this sketch it is used for wind direction
#define DEG2RAD 0.0174532925  // Degrees to Radians conversion factor
#define INC 2                 // Minimum segment subtended angle and plotting angle increment (in degrees)
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour) {
  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x1 = sx * r + x;
  uint16_t y1 = sy * r + y;

  // Draw colour blocks every INC degrees
  for (int i = start_angle; i < start_angle + sub_angle; i += INC) {

    // Calculate pair of coordinates for segment end
    int x2 = cos((i + 1 - 90) * DEG2RAD) * r + x;
    int y2 = sin((i + 1 - 90) * DEG2RAD) * r + y;

    tft.fillTriangle(x1, y1, x2, y2, x, y, colour);

    // Copy segment end to segment start for next segment
    x1 = x2;
    y1 = y2;
  }
}

/***************************************************************************************
**                          Get 3 hourly index at start of next day
***************************************************************************************/
int getNextDayIndex(void) {
  int index = 0;
  String today = forecast->dt_txt[0].substring(8, 10);
  for (index = 0; index < 9; index++) {
    if (forecast->dt_txt[index].substring(8, 10) != today) break;
  }
  return index;
}

/***************************************************************************************
**                          Print the weather info to the Serial Monitor
***************************************************************************************/
void printWeather(void) {
#ifdef SERIAL_MESSAGES
  Serial.println("Weather from OpenWeather\n");

  Serial.print("city_name           : ");
  Serial.println(forecast->city_name);
  Serial.print("sunrise             : ");
  Serial.println(strTime(forecast->sunrise));
  Serial.print("sunset              : ");
  Serial.println(strTime(forecast->sunset));
  Serial.print("Latitude            : ");
  Serial.println(ow.lat);
  Serial.print("Longitude           : ");
  Serial.println(ow.lon);
  // We can use the timezone to set the offset eventually...
  Serial.print("Timezone            : ");
  Serial.println(forecast->timezone);
  Serial.println();

  if (forecast) {
    Serial.println("###############  Forecast weather  ###############\n");
    for (int i = 0; i < (MAX_DAYS * 8); i++) {
      Serial.print("3 hourly forecast   ");
      if (i < 10) Serial.print(" ");
      Serial.print(i);
      Serial.println();
      Serial.print("dt (time)        : ");
      Serial.println(strTime(forecast->dt[i]));

      Serial.print("temp             : ");
      Serial.println(forecast->temp[i]);
      Serial.print("temp.min         : ");
      Serial.println(forecast->temp_min[i]);
      Serial.print("temp.max         : ");
      Serial.println(forecast->temp_max[i]);

      Serial.print("pressure         : ");
      Serial.println(forecast->pressure[i]);
      Serial.print("sea_level        : ");
      Serial.println(forecast->sea_level[i]);
      Serial.print("grnd_level       : ");
      Serial.println(forecast->grnd_level[i]);
      Serial.print("humidity         : ");
      Serial.println(forecast->humidity[i]);

      Serial.print("clouds           : ");
      Serial.println(forecast->clouds_all[i]);
      Serial.print("wind_speed       : ");
      Serial.println(forecast->wind_speed[i]);
      Serial.print("wind_deg         : ");
      Serial.println(forecast->wind_deg[i]);
      Serial.print("wind_gust        : ");
      Serial.println(forecast->wind_gust[i]);

      Serial.print("visibility       : ");
      Serial.println(forecast->visibility[i]);
      Serial.print("pop              : ");
      Serial.println(forecast->pop[i]);
      Serial.println();

      Serial.print("dt_txt           : ");
      Serial.println(forecast->dt_txt[i]);
      Serial.print("id               : ");
      Serial.println(forecast->id[i]);
      Serial.print("main             : ");
      Serial.println(forecast->main[i]);
      Serial.print("description      : ");
      Serial.println(forecast->description[i]);
      Serial.print("icon             : ");
      Serial.println(forecast->icon[i]);

      Serial.println();
    }
  }
#endif
}
/***************************************************************************************
**             Convert Unix time to a "local time" time string "12:34"
***************************************************************************************/
String strTime(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);

  String localTime = "";

  if (hour(local_time) < 10) localTime += "0";
  localTime += hour(local_time);
  localTime += ":";
  if (minute(local_time) < 10) localTime += "0";
  localTime += minute(local_time);

  return localTime;
}

/***************************************************************************************
**  Convert Unix time to a local date + time string "Oct 16 17:18", ends with newline
***************************************************************************************/
String strDate(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);

  String localDate = "";

  localDate += monthShortStr(month(local_time));
  localDate += " ";
  localDate += day(local_time);
  localDate += " " + strTime(unixTime);

  return localDate;
}

String getLocationName(String lat, String lon) {
  static String lastValidLocation = "Envigado, Colombia"; // Default fallback
  HTTPClient http;
  String url = "https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=" + lat + "&longitude=" + lon + "&localityLanguage=en";
  
  http.begin(url);
  http.setTimeout(2000); // 2 second timeout to prevent hanging
  int httpCode = http.GET();

  if (httpCode == 200) {
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, http.getString());
    
    String city = doc["city"].as<String>();
    if (city == "null" || city == "") city = doc["locality"].as<String>(); 
    String country = doc["countryName"].as<String>();

    if (city != "null" && country != "null" && city != "") {
      lastValidLocation = city + ", " + country;
    }
  }
  http.end();
  return lastValidLocation; // Returns new location or the last successful one
}

/**The MIT License (MIT)
  Copyright (c) 2015 by Daniel Eichhorn
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYBR_DATUM HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  See more at http://blog.squix.ch
*/

//  Changes made by Bodmer:

//  Minor changes to text placement and auto-blanking out old text with background colour padding
//  Moon phase text added (not provided by OpenWeather)
//  Forecast text lines are automatically split onto two lines at a central space (some are long!)
//  Time is printed with colons aligned to tidy display
//  Min and max forecast temperatures spaced out
//  New smart splash startup screen and updated progress messages
//  Display does not need to be blanked between updates
//  Icons nudged about slightly to add wind direction + speed
//  Barometric pressure added

//  Adapted to use the OpenWeather library: https://github.com/Bodmer/OpenWeather
//  Moon phase/rise/set (not provided by OpenWeather) replace with  and cloud cover humidity
//  Created and added new 100x100 and 50x50 pixel weather icons, these are in the
//  sketch data folder, press Ctrl+K to view
//  Add moon icons, eliminate all downloads of icons (may lose server!)
//  Adapted to use anti-aliased fonts, tweaked coords
//  Added forecast for 4th day
//  Added cloud cover and humidity in lieu of Moon rise/set
//  Adapted to be compatible with ESP32
