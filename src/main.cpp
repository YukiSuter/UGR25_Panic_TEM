#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "driver/twai.h"
#include <string>
#include <vector>
#include <Arduino_JSON.h>
#include <ElegantOTA.h>
#include <AsyncTCP.h>

// secrets
#include "secrets.h"

// Config

float interp_volt[6] = {2.17,1.92,1.68,1.59,1.51,1.48};
float interp_eq[14] = {-148.15, 321.48, -80, 173.6, -83.33, 180, -111.11, 226.67, -125, 248.75, -166.67, 311.67, -275, 472}; // m1,c1,m2,c2 etc

int therm_module_no = 2;

AsyncWebServer server(80);

// CAN Driver Configuration

#define CAN_TX_GPIO GPIO_NUM_5  // Use `gpio_num_t` instead of int
#define CAN_RX_GPIO GPIO_NUM_4

twai_general_config_t normalConfig = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
  
twai_general_config_t loopbackConfig = TWAI_GENERAL_CONFIG_DEFAULT(
    CAN_TX_GPIO, 
    CAN_RX_GPIO, 
    TWAI_MODE_NO_ACK  // Enables internal loopback mode
);

// Global fault variable

bool trigger_fault = false;

// Therm Classes

class Thermistor {
private:
    float voltage;
    float temperature;
    float old_temperature;
    int delta_count;
    int fault_count;
    int temp_err_count;
    int pinNo;


public:
    float getVoltage() {return this->voltage;}
    int getTemp() {
        return this->temperature;
    }

    int getPin() {
        return this->pinNo;
    }

    void readPin() {
        // Test 1: Just create the AnalogIn object, don't read from it
        this->voltage = ((analogRead(this->pinNo))*(3.1f/4095));
    }

    void volt2temp() {
        // CONVERSION LOGIC
        bool tempCalculated = false;
    
        for (int j = 0; j < 6; j++) {
            if (this->voltage > interp_volt[j]) {
                float m = interp_eq[2*j];
                float c = interp_eq[(2*j) + 1];
                
                this->temperature = (m * (this->voltage)) + c;
                tempCalculated = true;
                break;
            }
        }
        
        // Handle case where voltage is lower than all thresholds
        // Use the last segment (lowest voltage range)
        if (!tempCalculated) {
            float m = interp_eq[2*5];  // Use j=5 (last valid index)
            float c = interp_eq[(2*5) + 1];
            this->temperature = m * (this->voltage) + c;
        }
    }

    void filter() {
        bool fault_counted;
        // Delta handler
        float delta = this->temperature - this->old_temperature;
        
        if (abs(delta) >= 10) { this->delta_count += 1; } 
        else { this->delta_count = 0; }

        // Temp err handler

        if (this->temperature < 60 || this->temperature < 5) {this->temp_err_count += 1;}
        else {this->temp_err_count = 0;}

        // Fault count handler
        
        if (this->temp_err_count > 3 || this->delta_count > 5) {fault_count += 1;}
        else {fault_count = 0;}

        if (fault_count > 3) {
            trigger_fault = true;
            Serial.println("Triggering module fault state");
        }
    }

    bool getFaulted() {
        if (this->fault_count > 0) {return true;}
        else {return false;}
    }

    Thermistor(int pin) {
        pinNo = pin;
        temperature = 0;
        voltage = 0.0f;

        pinMode(pin, INPUT);
    }
};

class ThermSegment {
private:
    int segNo;
    int thermCount;

    int max;
    int min;
    int avg;

    std::vector<Thermistor> therms;

public:
    int getSegNo(){
        return this->segNo;
    }

    const std::vector<Thermistor>& getThermistors() const {
        return this->therms;
    }
    
    // Add this method to get a specific thermistor by index
    const Thermistor& getThermistor(int index) const {
        return this->therms[index];
    }


    void calcInfo() {
        if (therms.empty()) return;
        
        // Initialize with first valid sensor
        bool initialized = false;
        int runningTotal = 0;
        int validSensorCount = 0;

        for (int i = 0; i < this->therms.size(); i++) {
            if (this->therms[i].getFaulted() == false) {
                int currTemp = this->therms[i].getTemp();
                runningTotal += currTemp;
                validSensorCount++;
                
                if (!initialized) {
                    this->min = currTemp;
                    this->max = currTemp;
                    initialized = true;
                } else {
                    if (currTemp < this->min) { this->min = currTemp; }
                    if (currTemp > this->max) { this->max = currTemp; }
                }
            } else {
                // Serial.println("Skipping faulted sensor");
            }
        };

        if (validSensorCount > 0) {
            this->avg = runningTotal / validSensorCount;
        } else {
            this->avg = 0; // No valid sensors
        }
    }

    void bmsCAN() {
        int TC_bms = this->thermCount;

        if (trigger_fault) {
            TC_bms = 0x80;
        }

        twai_message_t bms;
        bms.identifier = 0x1839F380;
        bms.extd = 1;  // Standard CAN frame (not extended)
        bms.rtr = 0;  // Ensure it's a normal data frame
        bms.data_length_code = 0x8;  // Number of data bytes
        bms.data[0] = this->segNo; // Module No.
        bms.data[1] = this->min; // Lowest Temp
        bms.data[2] = this->max; // Highest Temp
        bms.data[3] = this->avg; // Average Temp
        bms.data[4] = this->thermCount; // No. of thermistors
        bms.data[5] = (this->segNo-1)*80 + this->thermCount; // Highest thermistor ID on module (zero-based)
        bms.data[6] = (this->segNo-1)*80; // Lowest thermistor ID on module (zero-based)
        bms.data[7] = bms.data[0] + bms.data[1] + bms.data[2] + bms.data[3] + bms.data[4] + bms.data[5] + bms.data[6] + 0x39 + bms.data_length_code; // Checksum


        // Transmit message
        if (twai_transmit(&bms, pdMS_TO_TICKS(1000)) == ESP_OK) {
            Serial.println("Message Sent");
        } else {
            Serial.println("Failed to send message");
        }        
    }

    void update() {
        for (int i=0; i<this->therms.size(); i++) {
            this->therms[i].readPin();
            // Serial.println(this->therms[i].getVoltage());
            this->therms[i].volt2temp();
            // Serial.println(this->therms[i].getTemp());
            // this->therms[i].filter();
        }
        this->calcInfo();
    }

    ThermSegment(int num, std::initializer_list<int> pins) : segNo(num), thermCount(pins.size()) {
        for (int pin : pins) {
            therms.emplace_back(Thermistor(pin));
        }
    }
};

// Vector that will hold all the thermsegments (just 1 of them for now)
std::vector<ThermSegment> segVec;

// Setup ======================================

void setup() {
  Serial.begin(115200);
  analogSetAttenuation(ADC_11db);

  // WiFi Setup
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname("UGRacing_TEM"+(char)(therm_module_no));
  WiFi.begin(ssid, password);

  delay(5000);

  ElegantOTA.begin(&server);    // Start ElegantOTA

  // CAN SETUP ===========================
  Serial.println("Initializing CAN Receiver...");

  // TWAI Configuration for RX only
  twai_general_config_t g_config = normalConfig;
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); // Accept all messages

  // Install and start the TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
      Serial.println("TWAI driver installed");
  } else {
      Serial.println("Failed to install TWAI driver");
      return;
  }

  if (twai_start() == ESP_OK) {
      Serial.println("TWAI driver started");
  } else {
      Serial.println("Failed to start TWAI driver");
      return;
  }

  // FS SETUP ==================

  if (!LittleFS.begin()) {  // Initialise LittleFS
    Serial.println("An Error has occurred whilst mounting LittleFS");
    return;
  }

  // Initialise thermistors and add to therms (comment as necessary)

  // original segVec.push:
//   segVec.push_back(ThermSegment(therm_module_no, {6,7,15,16,17,18,8,9,10,11,12,13,14,1,2}));


  // for module 1:
//   segVec.push_back(ThermSegment(therm_module_no, {7,15,16,17,18,8,9,10,11,12,13,14,1,2}));
  // for module 2:
  segVec.push_back(ThermSegment(therm_module_no, {6,7,15,16,17,18,9,10,11,12,13,14,1,2}));




  // Webserver


  server.serveStatic("/", LittleFS, "/").setDefaultFile("main.html");

  server.on("/api/thermistors", HTTP_GET, [](AsyncWebServerRequest *request){
    JSONVar jsonmessage;
    JSONVar thermistors = JSONVar();
    
    // Loop through all segments
    for (int segmentIndex = 0; segmentIndex < segVec.size(); segmentIndex++) {
        ThermSegment& segment = segVec[segmentIndex];

        segment.update();
        
        // Get all thermistors in this segment
        const std::vector<Thermistor>& segmentTherms = segment.getThermistors();
        
        // Loop through all thermistors in this segment
        for (int thermIndex = 0; thermIndex < segmentTherms.size(); thermIndex++) {
            Thermistor therm = segmentTherms[thermIndex];

            
            // Create JSON object for this thermistor
            JSONVar thermData;
            thermData["segmentNumber"] = segment.getSegNo();
            thermData["thermistorNumber"] = thermIndex + 1; // 1-based numbering
            thermData["temperature"] = therm.getTemp();
            // Serial.println(therm.getVoltage());
            // Serial.println(therm.getTemp());
            // Serial.println(therm.getPin());

            
            // Add to thermistors array
            thermistors[thermistors.length() + 1] = thermData;
        }
    }
    
    // Build final JSON message
    jsonmessage["thermistors"] = thermistors;
    
    String jsonstring = JSON.stringify(jsonmessage);
    
    request->send(200, "application/json", jsonstring);
  });

  server.begin();
}

std::vector<Thermistor> therms;

void loop() {
  for (ThermSegment ts : segVec) {
      ts.update();
      ts.bmsCAN();
      delay(100/segVec.size());
  }

  Serial.println(WiFi.status());

  Serial.println(WiFi.localIP());

  if (segVec.size() <= 0) {delay(100);} // In case segvec is empty for whatever reason
  ElegantOTA.loop();
}