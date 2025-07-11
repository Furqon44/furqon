#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <time.h>
#include <RTClib.h>  
#include <Wire.h>    
#include <LiquidCrystal_I2C.h> 
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
#define LCD_UPDATE_INTERVAL 1000
#define WIFI_TIMEOUT 20000  // 20 seconds timeout
#define FIREBASE_RETRY_DELAY 5000
#define DHT_READ_INTERVAL 2000
#define DHT_RETRY_DELAY 500

// Global Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Global Variables
float soilMoisture, lightIntensity, temperature, humidity;
float lastSoilMoisture, lastLightIntensity, lastTemperature, lastHumidity;
int soilRawValue;
String currentTime;
bool signupOK = false;
bool initialReading = true;

// Pump Control Variables
unsigned long lastSensorTime = 0;
unsigned long lastPumpCheckTime = 0;
unsigned long lastLCDUpdateTime = 0;
unsigned long lastTimeDisplayUpdate = 0;
bool waterPumpStatus = false;
bool fertilizerPumpStatus = false;
float minSoilMoisture = 60.0;
float maxSoilMoisture = 80.0;

// Non-blocking WiFi 
enum WiFiState {
  WIFI_DISCONNECTED,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED
};
WiFiState wifiState = WIFI_DISCONNECTED;
unsigned long wifiConnectStartTime = 0;
unsigned long lastWiFiRetryTime = 0;

// Non-blocking Firebase 
enum FirebaseState {
  FIREBASE_NOT_INITIALIZED,
  FIREBASE_INITIALIZING,
  FIREBASE_READY,
  FIREBASE_FAILED
};
FirebaseState firebaseState = FIREBASE_NOT_INITIALIZED;
unsigned long lastFirebaseRetryTime = 0;

// Non-blocking fertilizer pump 
unsigned long fertilizerPumpStartTime = 0;
bool fertilizerPumpRunning = false;
static bool fertilizerPumpScheduled = false;

// Non-blocking DHT 
unsigned long lastDHTReadTime = 0;
unsigned long dhtReadAttemptTime = 0;
int dhtAttemptCount = 0;
bool dhtReading = false;

// Non-blocking RTC 
bool rtcSyncPending = false;
unsigned long lastRTCSyncAttempt = 0;
bool rtcSynced = false;  // Track if RTC has been synchronized

// Setup state tracking
enum SetupState {
  SETUP_START,
  SETUP_PINS,
  SETUP_DHT,
  SETUP_WIFI,
  SETUP_RTC,
  SETUP_LCD,
  SETUP_FIREBASE,
  SETUP_RELAY_TEST,
  SETUP_COMPLETE
};
SetupState setupState = SETUP_START;
unsigned long setupStateStartTime = 0;

// LCD Display Functions
void setupLCD() {
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Smart Planting Pot");
    lcd.setCursor(0, 1);
    lcd.print("By: Ahmad Furqon S");
}

void updateLCD() {
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

// Non-blocking WiFi Connection
void handleWiFiConnection() {
    unsigned long currentMillis = millis();
    
    switch (wifiState) {
        case WIFI_DISCONNECTED:
            if (currentMillis - lastWiFiRetryTime >= 5000) {  // Retry every 5 seconds
                Serial.println("Starting WiFi connection...");
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                wifiConnectStartTime = currentMillis;
                wifiState = WIFI_CONNECTING;
                lastWiFiRetryTime = currentMillis;
            }
            break;
            
        case WIFI_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                wifiState = WIFI_CONNECTED;
                Serial.println("WiFi Connected Successfully");
                Serial.print("IP Address: ");
                Serial.println(WiFi.localIP());
                configTime(7 * 3600, 0, "pool.ntp.org");
                rtcSyncPending = true;
            } else if (currentMillis - wifiConnectStartTime >= WIFI_TIMEOUT) {
                wifiState = WIFI_FAILED;
                Serial.println("WiFi Connection Timeout");
            }
            break;
            
        case WIFI_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                wifiState = WIFI_DISCONNECTED;
                Serial.println("WiFi Disconnected");
                firebaseState = FIREBASE_NOT_INITIALIZED;
                signupOK = false;
                rtcSynced = false;  // Reset RTC sync status when WiFi disconnects
            }
            break;
            
        case WIFI_FAILED:
            if (currentMillis - lastWiFiRetryTime >= 10000) {  // Retry after 10 seconds
                wifiState = WIFI_DISCONNECTED;
            }
            break;
    }
}

// Non-blocking Firebase Setup
void handleFirebaseConnection() {
    unsigned long currentMillis = millis();
    
    if (wifiState != WIFI_CONNECTED) return;
    
    switch (firebaseState) {
        case FIREBASE_NOT_INITIALIZED:
            if (currentMillis - lastFirebaseRetryTime >= FIREBASE_RETRY_DELAY) {
                Serial.println("Initializing Firebase...");
                config.api_key = API_KEY;
                config.database_url = DATABASE_URL;
                firebaseState = FIREBASE_INITIALIZING;
                lastFirebaseRetryTime = currentMillis;
            }
            break;
            
        case FIREBASE_INITIALIZING:
            if (Firebase.signUp(&config, &auth, "", "")) {
                firebaseState = FIREBASE_READY;
                signupOK = true;
                Serial.println("Firebase Authentication Successful");
                Firebase.begin(&config, &auth);
                Firebase.reconnectWiFi(true);
                fbdo.setResponseSize(4096);
                
                // Initialize pump states
                Firebase.RTDB.setBool(&fbdo, "/pumps/waterPump", false);
                Firebase.RTDB.setBool(&fbdo, "/pumps/fertilizerPump", false);
            } else {
                firebaseState = FIREBASE_FAILED;
                Serial.println("Firebase Authentication Failed");
            }
            break;
            
        case FIREBASE_FAILED:
            if (currentMillis - lastFirebaseRetryTime >= FIREBASE_RETRY_DELAY) {
                firebaseState = FIREBASE_NOT_INITIALIZED;
            }
            break;
            
        case FIREBASE_READY:
            if (!Firebase.ready()) {
                firebaseState = FIREBASE_NOT_INITIALIZED;
                signupOK = false;
            }
            break;
    }
}

// Non-blocking RTC Sync
void handleRTCSync() {
    unsigned long currentMillis = millis();
    
    if (!rtcSyncPending || wifiState != WIFI_CONNECTED || rtcSynced) return;
    
    if (currentMillis - lastRTCSyncAttempt >= 2000) {  // Try every 2 seconds
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, 
                                timeinfo.tm_mday, timeinfo.tm_hour, 
                                timeinfo.tm_min, timeinfo.tm_sec));
            Serial.println("===== RTC SYNCHRONIZATION =====");
            Serial.println("RTC Successfully Synchronized with NTP Server");
            Serial.println("Time has been updated from internet");
            Serial.println("===============================");
            rtcSyncPending = false;
            rtcSynced = true;
        } else {
            lastRTCSyncAttempt = currentMillis;
        }
    }
}

// Non-blocking DHT Reading
void handleDHTReading() {
    unsigned long currentMillis = millis();
    
    if (!dhtReading && (currentMillis - lastDHTReadTime >= DHT_READ_INTERVAL)) {
        dhtReading = true;
        dhtAttemptCount = 0;
        dhtReadAttemptTime = currentMillis;
        lastDHTReadTime = currentMillis;
    }
    
    if (dhtReading && (currentMillis - dhtReadAttemptTime >= DHT_RETRY_DELAY)) {
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        
        if (!isnan(h) && !isnan(t)) {
            humidity = h;
            temperature = t;
            dhtReading = false;
        } else {
            dhtAttemptCount++;
            if (dhtAttemptCount >= 3) {
                Serial.println("DHT reading failed after 3 attempts");
                dhtReading = false;
            } else {
                dhtReadAttemptTime = currentMillis;
            }
        }
    }
}

// Display Current Time from RTC (Real-time per second)
void displayCurrentTimeRealTime() {
    DateTime now = rtc.now();
    
    Serial.println("===== CURRENT TIME FROM RTC =====");
    Serial.printf("Date: %02d-%02d-%04d\n", now.day(), now.month(), now.year());
    Serial.printf("Time: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
    Serial.println("=================================");
}

// Sensor Reading Function
void readSensors() {
    // Save last values
    lastSoilMoisture = soilMoisture;
    lastLightIntensity = lightIntensity;
    lastTemperature = temperature;
    lastHumidity = humidity;
    
    // Soil moisture
    soilRawValue = analogRead(SOIL_MOISTURE_PIN);
    soilMoisture = map(soilRawValue, 2700, 2250, 0, 100);
    soilMoisture = constrain(soilMoisture, 0, 100);
    
    // Light intensity
    int ldrValue = analogRead(LDR_PIN);
    lightIntensity = map(ldrValue, 4095, 0, 0, 100);
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
    if (firebaseState != FIREBASE_READY) return false;
    
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
    if (firebaseState != FIREBASE_READY) return;

    if (Firebase.RTDB.getString(&fbdo, "/pump_settings/waterPumpRange")) {
        String rangeStr = fbdo.stringData();
        int delimiterPos = rangeStr.indexOf('-');
        
        if (delimiterPos != -1) {
            minSoilMoisture = rangeStr.substring(0, delimiterPos).toFloat();
            maxSoilMoisture = rangeStr.substring(delimiterPos + 1).toFloat();
        }
    }
}

// Automatic Watering Function
void checkAutomaticWatering() {
    if (soilMoisture <= minSoilMoisture) {
        if (!waterPumpStatus) {
            controlWaterPump(true);
        }
    } 
    else if (soilMoisture >= maxSoilMoisture) {
        if (waterPumpStatus) {
            controlWaterPump(false);
        }
    }
}

// Fertilizer Schedule Check Function
void checkFertilizerSchedule() {
    if (firebaseState != FIREBASE_READY) return;

    String scheduleDate = "";
    String scheduleTime = "";

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

    if (scheduleDate == currentDateStr && 
        scheduleTime == currentTimeStr && 
        !fertilizerPumpScheduled && 
        !fertilizerPumpRunning) {
        
        controlFertilizerPump(true);
        fertilizerPumpStartTime = millis();
        fertilizerPumpRunning = true;
        fertilizerPumpScheduled = true;
        
        Firebase.RTDB.setTimestamp(&fbdo, "/lastFertilizerPumpTime");
    }
    
    if (now.minute() != atoi(scheduleTime.substring(3, 5).c_str())) {
        fertilizerPumpScheduled = false;
    }
}

// Handle Fertilizer Pump Timer
void handleFertilizerPumpTimer() {
    if (fertilizerPumpRunning && (millis() - fertilizerPumpStartTime >= 6000)) {
        controlFertilizerPump(false);
        fertilizerPumpRunning = false;
    }
}

// Pump Status Check Function
void checkPumpStatus() {
    if (firebaseState != FIREBASE_READY) {
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
    
    success = Firebase.RTDB.getBool(&fbdo, "/pumps/fertilizerPump");
    if (success) {
        bool newFertilizerPumpStatus = fbdo.boolData();
        if (newFertilizerPumpStatus != fertilizerPumpStatus && !fertilizerPumpRunning) {
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

// Non-blocking Setup State Machine
void handleSetupStateMachine() {
    unsigned long currentMillis = millis();
    
    switch (setupState) {
        case SETUP_START:
            Serial.begin(115200);
            Serial.println("\nESP32 Garden Monitoring System Initializing...");
            setupState = SETUP_PINS;
            setupStateStartTime = currentMillis;
            break;
            
        case SETUP_PINS:
            if (currentMillis - setupStateStartTime >= 100) {
                pinMode(SOIL_MOISTURE_PIN, INPUT);
                pinMode(LDR_PIN, INPUT);
                pinMode(DHT_PIN, INPUT);
                pinMode(RELAY_WATER_PUMP, OUTPUT);
                pinMode(RELAY_FERTILIZER_PUMP, OUTPUT);
                digitalWrite(RELAY_WATER_PUMP, HIGH);
                digitalWrite(RELAY_FERTILIZER_PUMP, HIGH);
                setupState = SETUP_DHT;
                setupStateStartTime = currentMillis;
            }
            break;
            
        case SETUP_DHT:
            if (currentMillis - setupStateStartTime >= 100) {
                dht.begin();
                setupState = SETUP_WIFI;
                setupStateStartTime = currentMillis;
            }
            break;
            
        case SETUP_WIFI:
            if (currentMillis - setupStateStartTime >= 500) {
                // WiFi will be handled by handleWiFiConnection()
                setupState = SETUP_RTC;
                setupStateStartTime = currentMillis;
            }
            break;
            
        case SETUP_RTC:
            if (currentMillis - setupStateStartTime >= 100) {
                Wire.begin(RTC_SDA, RTC_SCL);
                if (rtc.begin()) {
                    if (rtc.lostPower()) {
                        Serial.println("RTC Lost Power, will sync when WiFi connects");
                        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
                    }
                    setupState = SETUP_LCD;
                } else {
                    Serial.println("RTC Not Detected!");
                    // Continue anyway, system can work without RTC
                    setupState = SETUP_LCD;
                }
                setupStateStartTime = currentMillis;
            }
            break;
            
        case SETUP_LCD:
            if (currentMillis - setupStateStartTime >= 100) {
                setupLCD();
                setupState = SETUP_FIREBASE;
                setupStateStartTime = currentMillis;
            }
            break;
            
        case SETUP_FIREBASE:
            if (currentMillis - setupStateStartTime >= 500) {
                // Firebase will be handled by handleFirebaseConnection()
                setupState = SETUP_RELAY_TEST;
                setupStateStartTime = currentMillis;
            }
            break;
            
        case SETUP_RELAY_TEST:
            if (currentMillis - setupStateStartTime >= 1000) {
                Serial.println("Performing Pump Relay Test...");
                controlWaterPump(false);
                controlFertilizerPump(false);
                setupState = SETUP_COMPLETE;
                setupStateStartTime = currentMillis;
            }
            break;
            
        case SETUP_COMPLETE:
            if (currentMillis - setupStateStartTime >= 1000) {
                Serial.println("System Ready - Manual Pump Control via Firebase");
                // Setup is complete, normal operation begins
            }
            break;
    }
}

// Setup Function
void setup() {
    // Setup will be handled by the state machine
    setupState = SETUP_START;
}

// Main Loop Function
void loop() {
    unsigned long currentMillis = millis();
    
    // Handle setup state machine
    if (setupState != SETUP_COMPLETE) {
        handleSetupStateMachine();
    }
    
    // Handle all non-blocking operations
    handleWiFiConnection();
    handleFirebaseConnection();
    handleRTCSync();
    handleDHTReading();
    handleFertilizerPumpTimer();
    
    // Only run main operations after setup is complete
    if (setupState == SETUP_COMPLETE) {
        // Real-time Clock Display (every second)
        if (currentMillis - lastTimeDisplayUpdate >= 1000) {
            lastTimeDisplayUpdate = currentMillis;
            displayCurrentTimeRealTime();
        }
        
        // Sensor Reading and Firebase Upload
        if (currentMillis - lastSensorTime >= SENSING_INTERVAL) {
            lastSensorTime = currentMillis;
            
            readSensors();
            updateCurrentTime();
            
            if (hasSignificantChange() && firebaseState == FIREBASE_READY) {
                uploadToFirebase();
            }
            
            readPumpSettings();
            checkAutomaticWatering();
        }
        
        // Pump Status Check
        if (currentMillis - lastPumpCheckTime >= CHECK_PUMP_INTERVAL) {
            lastPumpCheckTime = currentMillis;
            
            if (firebaseState == FIREBASE_READY) {
                checkPumpStatus();
                checkFertilizerSchedule();
            }
        }
        
        // Update LCD Display
        if (currentMillis - lastLCDUpdateTime >= LCD_UPDATE_INTERVAL) {
            lastLCDUpdateTime = currentMillis;
            updateLCD();
        }
    }
}