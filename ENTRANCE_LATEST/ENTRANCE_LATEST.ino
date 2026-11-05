#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <esp_now.h>
#include <vl53l8cx.h>

// MAC Address of the EXIT node ESP32
uint8_t peerMacAddress[] = {0x7C, 0xD5, 0x44, 0x35, 0x40, 0x1C}; 

// Pin Definitions
#define PIN_LED_GREEN    14
#define PIN_LED_YELLOW   27
#define PIN_LED_RED      26
#define PIN_BUZZER       25

// Thresholds
#define MAX_CAPACITY     300
#define WARNING_LEVEL    240

LiquidCrystal_I2C lcd(0x27, 16, 2);
VL53L8CX mySensor(&Wire, -1); 
VL53L8CX_ResultsData sensorResults;

int totalIn = 0;
int totalOut = 0;
int currentTotal = 0;

// Instantaneous counters for display (reset to 0 after inactivity)
int instantIn = 0;
int instantOut = 0;
unsigned long lastActivityTime = 0;
const unsigned long DISPLAY_RESET_DELAY = 1500; // 1.5 seconds inactivity resets instant view

typedef struct struct_message {
    int inCount;
    int outCount;
    int instantIn;
    int instantOut;
} struct_message;

struct_message incomingData;
struct_message outgoingData;

void selfTest();
void updateDisplay();
void updateOutputs();
int processSensorGrid();
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataBytes, int len);
void sendData();

void setup() {
    Serial.begin(115200);

    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_YELLOW, OUTPUT);
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    Wire.begin(21, 22);
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Entrance Booting");

    selfTest();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("ESP-NOW Error!");
        while (1);
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerMacAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    esp_now_register_recv_cb(OnDataRecv);

    mySensor.begin();
    if (mySensor.init() != 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("VL53L8CX Fail!");
        digitalWrite(PIN_LED_RED, HIGH);
        digitalWrite(PIN_BUZZER, HIGH);
        while (1);
    }
    mySensor.set_resolution(VL53L8CX_RESOLUTION_8X8);
    mySensor.set_ranging_frequency_hz(15);
    mySensor.start_ranging();

    lcd.clear();
    updateDisplay();
}

void loop() {
    uint8_t newDataReady = 0;
    mySensor.check_data_ready(&newDataReady);
    
    if (newDataReady) {
        if (mySensor.get_ranging_data(&sensorResults) == 0) {
            int detectedCount = processSensorGrid();
            if (detectedCount > 0) {
                totalIn += detectedCount;
                instantIn = detectedCount;
                instantOut = 0;
                lastActivityTime = millis();
                
                currentTotal = totalIn - totalOut;
                if (currentTotal < 0) currentTotal = 0;

                sendData();
                updateDisplay();
                updateOutputs();
            }
        }
    }

    // Reset instantaneous display counts back to 0 if no motion is detected
    if ((instantIn > 0 || instantOut > 0) && (millis() - lastActivityTime > DISPLAY_RESET_DELAY)) {
        instantIn = 0;
        instantOut = 0;
        updateDisplay();
    }
}

void selfTest() {
    digitalWrite(PIN_LED_GREEN, HIGH); delay(300); digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_YELLOW, HIGH); delay(300); digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_RED, HIGH); delay(300); digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Status: OK");
    delay(1000);
}

int processSensorGrid() {
    int frontActiveZones = 0;
    int backActiveZones = 0;

    // Check Front Zones (Rows 0 to 3)
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            int zoneIndex = (i * 8) + j;
            int status = sensorResults.target_status[zoneIndex];
            int distance = sensorResults.distance_mm[zoneIndex];

            // Only accept valid target status codes (5, 6, 9) and filter distance limits
            if ((status == 5 || status == 6 || status == 9) && distance > 400 && distance < 2100) {
                frontActiveZones++;
            }
        }
    }

    // Check Back Zones (Rows 4 to 7)
    for (int i = 4; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            int zoneIndex = (i * 8) + j;
            int status = sensorResults.target_status[zoneIndex];
            int distance = sensorResults.distance_mm[zoneIndex];

            if ((status == 5 || status == 6 || status == 9) && distance > 400 && distance < 2100) {
                backActiveZones++;
            }
        }
    }

    static unsigned long lastTrigger = 0;
    if (millis() - lastTrigger > 500) {
        if (frontActiveZones >= 2 && backActiveZones >= 2) {
            lastTrigger = millis();
            int estimatedPeople = max(1, (frontActiveZones + backActiveZones) / 45);
            return estimatedPeople; 
        }
    }
    return 0;
}

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataBytes, int len) {
    memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
    totalOut = incomingData.outCount;
    instantOut = incomingData.instantOut;
    instantIn = incomingData.instantIn;
    if (incomingData.instantOut > 0) {
        lastActivityTime = millis();
    }
    
    currentTotal = totalIn - totalOut;
    if (currentTotal < 0) currentTotal = 0;

    updateDisplay();
    updateOutputs();
}

void sendData() {
    outgoingData.inCount = totalIn;
    outgoingData.outCount = totalOut;
    outgoingData.instantIn = instantIn;
    outgoingData.instantOut = instantOut;
    esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
}

void updateDisplay() {
    lcd.setCursor(0, 0);
    char topBuffer[17];
    snprintf(topBuffer, sizeof(topBuffer), "In:%-3d Out:%-3d  ", instantIn, instantOut);
    lcd.print(topBuffer);

    lcd.setCursor(0, 1);
    char bottomBuffer[17];
    snprintf(bottomBuffer, sizeof(bottomBuffer), "Count:%-3d/%-3d      ", currentTotal, MAX_CAPACITY);
    lcd.print(bottomBuffer);
}

void updateOutputs() {
    if (currentTotal >= MAX_CAPACITY) {
        digitalWrite(PIN_LED_GREEN, LOW);
        digitalWrite(PIN_LED_YELLOW, LOW);
        digitalWrite(PIN_LED_RED, HIGH);
        digitalWrite(PIN_BUZZER, HIGH);
    } else if (currentTotal >= WARNING_LEVEL) {
        digitalWrite(PIN_LED_GREEN, LOW);
        digitalWrite(PIN_LED_YELLOW, HIGH);
        digitalWrite(PIN_LED_RED, LOW);
        digitalWrite(PIN_BUZZER, HIGH);
        delay(150);
        digitalWrite(PIN_BUZZER, LOW);
    } else {
        digitalWrite(PIN_LED_GREEN, HIGH);
        digitalWrite(PIN_LED_YELLOW, LOW);
        digitalWrite(PIN_LED_RED, LOW);
        digitalWrite(PIN_BUZZER, LOW);
    }
}