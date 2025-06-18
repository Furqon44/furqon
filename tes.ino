#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <time.h>
#include <RTClib.h>  
#include <Wire.h>    
#include <LiquidCrystal_I2C.h>  // LCD library that works with ESP32
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// Pin Definitions
#define SOIL_MOISTURE_PIN 34
#define LDR_PIN 35
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define RELAY_WATER_PUMP 26
#define RELAY_FERTILIZER_PUMP 27
#define RTC_SDA 21
#define RTC_SCL 22

// Network & Firebase Configuration
#define API_KEY "AIzaSyCY_b7OHt64p1z_Ytna1YsFIIJ1jPRc2r4"
#define DATABASE_URL "tess-2d281-default-rtdb.firebaseio.com"
#define WIFI_SSID "Redmi"
#define WIFI_PASSWORD "qwerasdf"

// System Settings
#define SENSING_INTERVAL 5000
#define CHECK_PUMP_INTERVAL 1000
#define SOIL_THRESHOLD 2.0
#define LIGHT_THRESHOLD 5.0
#define TEMP_THRESHOLD 0.5
#define HUMID_THRESHOLD 1.0
#define LCD_UPDATE_INTERVAL 1000  // LCD refresh interval

// Global Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD object (address, columns, rows)

// Global Variables
float soilMoisture, lightIntensity, temperature, humidity;
float lastSoilMoisture, lastLightIntensity, lastTemperature, lastHumidity;
int soilRawValue;  // Added variable to store raw ADC value
String currentTime;
bool signupOK = false;
bool initialReading = true;

// Pump Control Variables
unsigned long lastSensorTime = 0;
unsigned long lastPumpCheckTime = 0;
unsigned long lastLCDUpdateTime = 0;  // For LCD updates
bool waterPumpStatus = false;
bool fertilizerPumpStatus = false;
float minSoilMoisture = 60.0;
float maxSoilMoisture = 80.0;

// LCD Display Functions
void setupLCD() {
    lcd.init();       // Initialize LCD
    lcd.backlight();  // Turn on backlight
    
    // Display welcome message
    lcd.setCursor(0, 0);
    lcd.print("Smart Planting Pot");
    lcd.setCursor(0, 1);
    lcd.print("By: Ahmad Furqon S");
}

void updateLCD() {
    // Only show title and creator
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Smart Planting Pot");
    lcd.setCursor(0, 1);
    lcd.print("By: Ahmad Furqon S");
}

// Control Pump Functions
void controlWaterPump(bool turnOn) {
    if (turnOn) {
        digitalWrite(RELAY_WATER_PUMP, LOW);
        Serial.println("Water Pump: ON (pin=LOW)");
    } else {
        digitalWrite(RELAY_WATER_PUMP, HIGH);
        Serial.println("Water Pump: OFF (pin=HIGH)");
    }
    waterPumpStatus = turnOn;
}

void controlFertilizerPump(bool turnOn) {
    if (turnOn) {
        digitalWrite(RELAY_FERTILIZER_PUMP, LOW);
        Serial.println("Fertilizer Pump: ON (pin=LOW)");
    } else {
        digitalWrite(RELAY_FERTILIZER_PUMP, HIGH);
        Serial.println("Fertilizer Pump: OFF (pin=HIGH)");
    }
    fertilizerPumpStatus = turnOn;
}

// Sensor Reading Function
void readSensors() {
    // Save last values
    lastSoilMoisture = soilMoisture;
    lastLightIntensity = lightIntensity;
    lastTemperature = temperature;
    lastHumidity = humidity;
    
    // Soil moisture - Read raw ADC value first
    soilRawValue = analogRead(SOIL_MOISTURE_PIN);
    soilMoisture = map(soilRawValue, 2700, 2250, 0, 100);
    soilMoisture = constrain(soilMoisture, 0, 100);
    
    // Display soil sensor values in serial monitor
    Serial.println("===== SOIL MOISTURE SENSOR =====");
    Serial.print("Raw ADC Value: ");
    Serial.println(soilRawValue);
    Serial.print("Soil Moisture: ");
    Serial.print(soilMoisture);
    Serial.println("%");
    Serial.println("================================");
    
    // Light intensity
    int ldrValue = analogRead(LDR_PIN);
    lightIntensity = map(ldrValue, 4095, 0, 0, 100);
    
    // Temperature & humidity
    int attempts = 0;
    float h, t;
    do {
        h = dht.readHumidity();
        t = dht.readTemperature();
        
        if (!isnan(h) && !isnan(t)) {
            humidity = h;
            temperature = t;
            break;
        }
        delay(500);
        attempts++;
    } while (attempts < 3 && (isnan(h) || isnan(t)));
    
    if (isnan(h) || isnan(t)) {
        Serial.println("DHT reading failed");
    }
}

// Time Update Function
void updateCurrentTime() {
    DateTime now = rtc.now();
    
    char timeString[25];
    snprintf(timeString, sizeof(timeString), 
             "%04d-%02d-%02dT%02d:%02d:%02dZ", 
             now.year(), now.month(), now.day(), 
             now.hour(), now.minute(), now.second());
    
    currentTime = String(timeString);
}

// Firebase Upload Function
bool uploadToFirebase() {
    if (!Firebase.ready()) return false;
    
    updateCurrentTime();
    
    String path = "/sensors";
    bool success = true;
    
    success &= Firebase.RTDB.setFloat(&fbdo, path + "/humidity", humidity);
    success &= Firebase.RTDB.setFloat(&fbdo, path + "/light", lightIntensity);
    success &= Firebase.RTDB.setFloat(&fbdo, path + "/soilMoisture", soilMoisture);
    success &= Firebase.RTDB.setFloat(&fbdo, path + "/temperature", temperature);
    success &= Firebase.RTDB.setString(&fbdo, path + "/time", currentTime);
    
    return success;
}

// Pump Settings Reading Function
void readPumpSettings() {
    if (!Firebase.ready() || !signupOK) return;

    if (Firebase.RTDB.getFloat(&fbdo, "/pump_settings/waterPumpRange")) {
        String rangeStr = fbdo.stringData();
        int delimiterPos = rangeStr.indexOf('-');
        
        if (delimiterPos != -1) {
            minSoilMoisture = rangeStr.substring(0, delimiterPos).toFloat();
            maxSoilMoisture = rangeStr.substring(delimiterPos + 1).toFloat();
        }
    }
}

// Automatic Watering Function - MODIFIED: Removed debug messages
void checkAutomaticWatering() {
    // Jika kelembapan tanah di bawah batas minimum, nyalakan pompa
    if (soilMoisture <= minSoilMoisture) {
        if (!waterPumpStatus) {
            controlWaterPump(true);
            // Debug message removed
        }
    } 
    // Jika kelembapan tanah sudah mencapai batas maksimum, matikan pompa
    else if (soilMoisture >= maxSoilMoisture) {
        if (waterPumpStatus) {
            controlWaterPump(false);
            // Debug message removed
        }
    }
    // Jika kelembapan tanah berada di antara min dan max, pertahankan status pompa
    // (tetap menyiram jika sedang menyiram sampai mencapai batas maksimum)
}

// Fertilizer Schedule Check Function
void checkFertilizerSchedule() {
    if (!Firebase.ready() || !signupOK) return;

    String scheduleDate = "";
    String scheduleTime = "";
    static bool fertilizerPumpScheduled = false;

    if (Firebase.RTDB.getString(&fbdo, "/pump_settings/fertilizerPumpDate")) {
        scheduleDate = fbdo.stringData();
        scheduleDate.trim();
    } else {
        return;
    }

    if (Firebase.RTDB.getString(&fbdo, "/pump_settings/fertilizerPumpTime")) {
        scheduleTime = fbdo.stringData();
        scheduleTime.trim();
    } else {
        return;
    }

    DateTime now = rtc.now();
    
    char currentDateStr[11];
    snprintf(currentDateStr, sizeof(currentDateStr), 
             "%02d-%02d-%04d", 
             now.day(), now.month(), now.year());
    
    char currentTimeStr[6];
    snprintf(currentTimeStr, sizeof(currentTimeStr), 
             "%02d:%02d", 
             now.hour(), now.minute());

    // Cek apakah waktu sesuai dan belum pernah dijalankan hari ini
    if (scheduleDate == currentDateStr && 
        scheduleTime == currentTimeStr && 
        !fertilizerPumpScheduled) {
        
        // Nyalakan pompa selama 6 detik
        controlFertilizerPump(true);
        delay(6000);  // Tunggu 6 detik
        controlFertilizerPump(false);
        
        // Tandai bahwa pompa sudah dijalankan untuk hari ini
        fertilizerPumpScheduled = true;
        
        // Reset flag pada pergantian menit berikutnya
        Firebase.RTDB.setTimestamp(&fbdo, "/lastFertilizerPumpTime");
    }
    
    // Reset flag jika sudah berganti menit
    if (now.minute() != atoi(scheduleTime.substring(3, 5).c_str())) {
        fertilizerPumpScheduled = false;
    }
}

// Pump Status Check Function
void checkPumpStatus() {
    if (!Firebase.ready() || WiFi.status() != WL_CONNECTED) {
        if (waterPumpStatus || fertilizerPumpStatus) {
            controlWaterPump(false);
            controlFertilizerPump(false);
        }
        return;
    }
    
    bool success = Firebase.RTDB.getBool(&fbdo, "/pumps/waterPump");
    if (success) {
        bool newWaterPumpStatus = fbdo.boolData();
        if (newWaterPumpStatus != waterPumpStatus) {
            controlWaterPump(newWaterPumpStatus);
        }
    }
    
    delay(200);
    
    success = Firebase.RTDB.getBool(&fbdo, "/pumps/fertilizerPump");
    if (success) {
        bool newFertilizerPumpStatus = fbdo.boolData();
        if (newFertilizerPumpStatus != fertilizerPumpStatus) {
            controlFertilizerPump(newFertilizerPumpStatus);
        }
    }
    
    Firebase.RTDB.setBool(&fbdo, "/pumps/waterPump", waterPumpStatus);
    Firebase.RTDB.setBool(&fbdo, "/pumps/fertilizerPump", fertilizerPumpStatus);
}

// Change Detection Function
bool hasSignificantChange() {
    if (initialReading) {
        initialReading = false;
        return true;
    }
    
    return (abs(soilMoisture - lastSoilMoisture) >= SOIL_THRESHOLD ||
            abs(lightIntensity - lastLightIntensity) >= LIGHT_THRESHOLD ||
            abs(temperature - lastTemperature) >= TEMP_THRESHOLD ||
            abs(humidity - lastHumidity) >= HUMID_THRESHOLD);
}

// WiFi Setup Function
void setupWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi Connected Successfully");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        configTime(7 * 3600, 0, "pool.ntp.org");
    } else {
        Serial.println("WiFi Connection Failed");
    }
}

// Firebase Setup Function
void setupFirebase() {
    if (WiFi.status() != WL_CONNECTED) return;

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    
    if (Firebase.signUp(&config, &auth, "", "")) {
        signupOK = true;
        Serial.println("Firebase Authentication Successful");
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
        fbdo.setResponseSize(4096);
        
        Firebase.RTDB.setBool(&fbdo, "/pumps/waterPump", false);
        Firebase.RTDB.setBool(&fbdo, "/pumps/fertilizerPump", false);
    } else {
        Serial.println("Firebase Authentication Failed");
    }
}

// RTC Setup Function
void setupRTC() {
    Wire.begin(RTC_SDA, RTC_SCL);
    
    if (!rtc.begin()) {
        Serial.println("RTC Not Detected!");
        while (1);
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTime(7 * 3600, 0, "pool.ntp.org");
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, 
                                timeinfo.tm_mday, timeinfo.tm_hour, 
                                timeinfo.tm_min, timeinfo.tm_sec));
            Serial.println("RTC Synchronized with NTP");
        }
    }

    if (rtc.lostPower()) {
        Serial.println("RTC Lost Power, Resetting Time!");
        
        if (WiFi.status() == WL_CONNECTED) {
            configTime(7 * 3600, 0, "pool.ntp.org");
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, 
                                    timeinfo.tm_mday, timeinfo.tm_hour, 
                                    timeinfo.tm_min, timeinfo.tm_sec));
            }
        } else {
            // Fixed: Use correct predefined macros __DATE__ and __TIME__
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
    }
}

// Setup Function
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nESP32 Garden Monitoring System Initializing...");

    // Pin Modes
    pinMode(SOIL_MOISTURE_PIN, INPUT);
    pinMode(LDR_PIN, INPUT);
    pinMode(DHT_PIN, INPUT);
    
    // Relay Initialization
    pinMode(RELAY_WATER_PUMP, OUTPUT);
    pinMode(RELAY_FERTILIZER_PUMP, OUTPUT);
    digitalWrite(RELAY_WATER_PUMP, HIGH);
    digitalWrite(RELAY_FERTILIZER_PUMP, HIGH);
    
    dht.begin();
    
    setupWiFi();
    setupRTC();  // Initialize RTC and I2C before LCD
    setupLCD();  // Initialize LCD
    setupFirebase();

    // Relay Test
    Serial.println("Performing Pump Relay Test...");
    controlWaterPump(false);
    controlFertilizerPump(false);
    delay(1000);
    
    // Show system ready on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Smart Planting Pot");
    lcd.setCursor(0, 1);
    lcd.print("By: Ahmad Furqon S");
    
    Serial.println("System Ready - Manual Pump Control via Firebase");
}

// Main Loop Function
void loop() {
    unsigned long currentMillis = millis();
    
    // Sensor Reading and Firebase Upload
    if (currentMillis - lastSensorTime >= SENSING_INTERVAL) {
        lastSensorTime = currentMillis;
        
        readSensors();
        updateCurrentTime();
        
        if (hasSignificantChange() && WiFi.status() == WL_CONNECTED && signupOK) {
            uploadToFirebase();
        }
        
        readPumpSettings();
        checkAutomaticWatering();
    }
    
    // Pump Status Check
    if (currentMillis - lastPumpCheckTime >= CHECK_PUMP_INTERVAL) {
        lastPumpCheckTime = currentMillis;
        
        if (WiFi.status() == WL_CONNECTED && signupOK) {
            checkPumpStatus();
            checkFertilizerSchedule();
        }
    }
    
    // Update LCD Display
    if (currentMillis - lastLCDUpdateTime >= LCD_UPDATE_INTERVAL) {  // Update every second
        lastLCDUpdateTime = currentMillis;
        updateLCD();
    }
}