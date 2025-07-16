#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "driver/twai.h"
#include <string>
#include <vector>

// Config

float interp_volt[6] = {2.17,1.92,1.68,1.59,1.51,1.48};
float interp_eq[14] = {-148.15, 321.48, -80, 173.6, -83.33, 180, -111.11, 226.67, -125, 248.75, -166.67, 311.67, -275, 472}; // m1,c1,m2,c2 etc

// CAN Driver Configuration

#define CAN_TX_GPIO GPIO_NUM_5  // Use `gpio_num_t` instead of int
#define CAN_RX_GPIO GPIO_NUM_4

twai_general_config_t normalConfig = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
  
twai_general_config_t loopbackConfig = TWAI_GENERAL_CONFIG_DEFAULT(
    CAN_TX_GPIO, 
    CAN_RX_GPIO, 
    TWAI_MODE_NO_ACK  // Enables internal loopback mode
);

// Therm Classes

class Thermistor {
private:
    float voltage;
    int temperature;
    int pinNo;

public:
    int getTemp() {
        return this->temperature;
    }

    void readPin() {
        // Test 1: Just create the AnalogIn object, don't read from it
        this->voltage = analogRead(this->pinNo)*5;
    }

    void volt2temp() {
        // CONVERSION LOGIC
        for (int j= 0; j < 6; j++) {
            if (this->voltage > interp_volt[j]) {
                float m = interp_eq[2*j];
                float c = interp_eq[(2*j) + 1];

                this->temperature = m*(this->voltage) + c;
                break;
            }
        }
    }

    void filter() {
        // Do filter stuff
    }

    Thermistor(int pin) {
        pinNo = pin;
        temperature = 0;
        voltage = 0.0f;

        pinMode(pin, OUTPUT);
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
    void calcInfo() {
        this->min = therms[0].getTemp();
        this->max = therms[0].getTemp();

        int runningTotal = 0;

        for (int i=0; i<this->therms.size(); i++) {
            int currTemp = this->therms[i].getTemp();
            runningTotal += currTemp;
            if (currTemp < this->min){this->min = currTemp;}
            if (currTemp > this->max){this->max = currTemp;}
            this->avg = currTemp/this->therms.size();
        }
    }

    void bmsCAN() {
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
        bms.data[5] = (this->segNo-1)*80 + 11; // Highest thermistor ID on module (zero-based)
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
            this->therms[i].volt2temp();
            this->therms[i].filter();
            this->calcInfo();
        }
    }

    ThermSegment(int num, std::initializer_list<int> pins) : segNo(num), thermCount(num) {
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

  // Initialise thermistors and add to therms

  segVec.push_back(ThermSegment(1, {6}));

}

std::vector<Thermistor> therms;

void loop() {
  
  // twai_message_t msgcheck = checkForMessages();

  // if (msgcheck.identifier < 0xFFFFFFFF) {
  //   // Handle incoming messages
  // }
  
  // uint8_t data[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};  // Example data
  // // identifier = 0x1839F380;

  // // messageTx(identifier, data_length_code, data);

  // twai_message_t tx_message;
  // tx_message.identifier = 0x1839F380;
  // tx_message.extd = 1;  // Standard CAN frame (not extended)
  // tx_message.rtr = 0;  // Ensure it's a normal data frame
  // tx_message.data_length_code = 0x8;  // Number of data bytes
  // memset(tx_message.data, 0, 8);  // Ensure no residual values
  // memcpy(tx_message.data, data, 8);

  // // Transmit message
  // if (twai_transmit(&tx_message, pdMS_TO_TICKS(1000)) == ESP_OK) {
  //     Serial.println("Message Sent");
  // } else {
  //     Serial.println("Failed to send message");
  // }

  for (ThermSegment ts : segVec) {
      ts.update();
      ts.bmsCAN();
      delay(100/segVec.size());
  }

  if (segVec.size() <= 0) {delay(100);} // In case segvec is empty for whatever reason
}