#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL343.h>
#include <MAX30105.h>
#include <NHB_AD7124.h>
#include <SPI.h>

// --- MAX30102 I2C Address and Registers ---
const uint8_t MAX30102_ADDR    = 0x57;
const uint8_t REG_INTR_STATUS_1= 0x00;
const uint8_t REG_FIFO_WR_PTR  = 0x04;
const uint8_t REG_FIFO_OVF_CTR = 0x05;
const uint8_t REG_FIFO_RD_PTR  = 0x06;
const uint8_t REG_FIFO_DATA    = 0x07;
const uint8_t REG_FIFO_CONFIG  = 0x08;
const uint8_t REG_MODE_CONFIG  = 0x09;
const uint8_t REG_SPO2_CONFIG  = 0x0A;
const uint8_t REG_LED1_PA      = 0x0C; // RED LED
const uint8_t REG_LED2_PA      = 0x0D; // IR LED
const uint8_t REG_PART_ID      = 0xFF;

// ---  PIN DEFINITIONS and communication setup --- 
static const uint8_t SDA_PIN = 21; // SDA pin
static const uint8_t SCL_PIN = 22; // SCL pin
const uint8_t csPin = 15;
Ad7124 adc(csPin, 4000000);  // CS pin and 4 MHz SPI clock
const uint16_t filterSelectBits = 320;

// --- SENSOR OBJECTS --- //
Adafruit_ADXL343 accel = Adafruit_ADXL343(12345); // Give sensor ID

// Helper to write a single byte to an I2C register.
bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MAX30102_ADDR);
    Wire.write(reg);
    Wire.write(value);
    // endTransmission returns 0 on success
    return (Wire.endTransmission() == 0);
}

uint8_t readRegister(uint8_t reg) {
    Wire.beginTransmission(MAX30102_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false); // Use false to keep the connection active
    Wire.requestFrom(MAX30102_ADDR, (uint8_t)1);
    return Wire.read();
}

// === Function: setupOximeter (Rebuilt and Robust) ===
// Initializes the MAX30102 sensor.
// Returns true on success, false on failure.
bool setupOximeter() {
    Serial.println("--- Initializing MAX30102 ---");

    // 1. Check for basic I2C connection.
    // This is the most fundamental check. It sees if ANY device is acknowledging the address.
    Wire.beginTransmission(MAX30102_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("ERROR: MAX30102 not found on I2C bus. Check wiring and I2C address.");
        return false;
    }
    Serial.println("Device acknowledged I2C address... OK.");

    // 2. Check the Part ID.
    // This confirms we are talking to the correct chip.
    uint8_t partID = readRegister(REG_PART_ID);
    if (partID != 0x15) {
        Serial.print("ERROR: Incorrect Part ID. Expected 0x15, but got 0x");
        Serial.println(partID, HEX);
        Serial.println("This is not a MAX30102 or communication is corrupted.");
        return false;
    }
    Serial.println("Part ID verified (0x15)... OK.");

    // 3. Perform a software reset.
    if (!writeRegister(REG_MODE_CONFIG, 0x40)) {
        Serial.println("ERROR: Failed to send reset command.");
        return false;
    }
    delay(200); // Wait for reset to complete.
    Serial.println("Sensor reset successfully.");

    // 4. Clear FIFO pointers for a clean start.
    writeRegister(REG_FIFO_WR_PTR, 0x00);
    writeRegister(REG_FIFO_OVF_CTR, 0x00);
    writeRegister(REG_FIFO_RD_PTR, 0x00);

    // 5. Configure the sensor.
    writeRegister(REG_FIFO_CONFIG, 0x4F);   // SMP_AVE=4, no rollover, 17 samples to full
    writeRegister(REG_SPO2_CONFIG, 0x27);   // SPO2_ADC_RGE=4096nA, SR=100Hz, PW=411us
    writeRegister(REG_LED1_PA, 0x24);       // LED1 (Red) current ~7.6mA
    writeRegister(REG_LED2_PA, 0x24);       // LED2 (IR) current ~7.6mA
    writeRegister(REG_MODE_CONFIG, 0x03);   // Mode = SpO2 (This starts the measurements)

    Serial.println("MAX30102 configured and running.");
    return true;
}

// === Function: getOximeterData (Rebuilt and Clean) ===
// Reads a single RED + IR sample from the MAX30102 FIFO.
// Returns true if a sample was read, false if the FIFO was empty.
bool getOximeterData(uint32_t &red, uint32_t &ir) {
    // First, check if there are any samples available by comparing the pointers.
    uint8_t writePointer = readRegister(REG_FIFO_WR_PTR);
    uint8_t readPointer = readRegister(REG_FIFO_RD_PTR);

    if (writePointer == readPointer) {
        // No new data to read.
        return false;
    }

    // Read 6 bytes (one full Red+IR sample) from the FIFO data register.
    Wire.beginTransmission(MAX30102_ADDR);
    Wire.write(REG_FIFO_DATA);
    Wire.endTransmission(false);
    Wire.requestFrom(MAX30102_ADDR, (uint8_t)6);

    if (Wire.available() >= 6) {
        uint8_t red_msb = Wire.read();
        uint8_t red_mid = Wire.read();
        uint8_t red_lsb = Wire.read();
        uint8_t ir_msb = Wire.read();
        uint8_t ir_mid = Wire.read();
        uint8_t ir_lsb = Wire.read();

        red = ((uint32_t)(red_msb & 0x03) << 16) | ((uint32_t)red_mid << 8) | red_lsb;
        ir  = ((uint32_t)(ir_msb  & 0x03) << 16) | ((uint32_t)ir_mid  << 8) | ir_lsb;

        return true; // Success!
    }

    // If we get here, it means we expected data but couldn't read 6 bytes.
    return false;
}

void setup_adc() {
    Serial.println("Initializing ADC...");
    if (!adc.begin()) {
     Serial.println("ADC initialization failed!");
    while (1);  // Halt on failure
    }
    delay(500);  // Allow AD7124 to stabilize
    Serial.println("ADC initialized.");

    // Configure ADC in Full Power Mode
    adc.setAdcControl(AD7124_OpMode_SingleConv, AD7124_FullPower, true);
    Serial.println("ADC configured.");

    // Configure setup 0: Internal 2.5V reference, gain=1, bipolar
    adc.setup[0].setConfig(AD7124_Ref_Internal, AD7124_Gain_1, true);
    adc.setup[0].setFilter(AD7124_Filter_SINC3, filterSelectBits);  // ~20 SPS
    Serial.println("Setup configured.");

    // Set channel 0: AIN0(+) and AIN1(-)
    adc.setChannel(0, 0, AD7124_Input_AIN0, AD7124_Input_AIN1, true);
    Serial.println("Channel 0 configured.");

    // Set channel 1: AIN2(+) and AIN3(-)
    adc.setChannel(1, 0, AD7124_Input_AIN2, AD7124_Input_AIN3, true);  // Using setup 0
    Serial.println("Channel 1 configured.");
}

// Adc performs measurement for mystery resistor //
void perform_measurement() {
    double voltage0, voltage1, resistance;
    voltage0 = adc.readVolts(0);  // Read differential voltage on channel 0 (AIN0-AIN1)
    voltage1 = adc.readVolts(1);  // Read differential voltage on channel 1 (AIN2-AIN3)

  // Calculate resistance: (V_across_AIN0-AIN1 * 1M) / V_across_AIN2-AIN3
  if (voltage1 != 0) {  // Avoid division by zero
    resistance = (voltage0 * 1000000) / voltage1;  // 1M = 1000000 ohms
  } else {
    resistance = 0;  // Default to 0 if denominator is zero
    Serial.println("--- UNDEFINED---");
  }     
  // --- Voltage readouts from adc --- //
    Serial.print("|Voltage across AIN0 and AIN1: ");
    Serial.print(voltage0, 3);
    Serial.print(" V ");
    Serial.print("Voltage across AIN2 and AIN3: ");
    Serial.print(voltage1, 3);
    Serial.print(" V ");
    Serial.print("Calculated Resistance: ");
    if(resistance > 0){
        Serial.print(resistance, 3);
    }
    else if (resistance <= 0)
    {
        Serial.print("0 ");
    }
    Serial.println(" ohms");
}
// Conigures SPI communication for ADC to ESP32 //
void setup_spi() {
    Serial.println("Initializing SPI...");
    SPI.begin(18, 19, 23, csPin);  // HSPI: SCK=18, MISO=19, MOSI=23, SS=15
    SPI.setFrequency(4000000);     // 4 MHz
    SPI.setDataMode(SPI_MODE3);    // AD7124 requires Mode 3
    delay(100);  // Stabilize SPI
    Serial.println("SPI initialized.");
    // Test SPI communication
    digitalWrite(csPin, LOW);
    uint8_t testResponse = SPI.transfer(0xAA);  // Send 0xAA and read response
    digitalWrite(csPin, HIGH);
    Serial.print("SPI Test Response: 0x");
    Serial.println(testResponse, HEX);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    delay(5000);
    
    setup_spi();
    setup_adc();
    Wire.begin(SDA_PIN, SCL_PIN);
   
    // --- Initialize ADXL343 ---
    if (!accel.begin()) {
        Serial.println("Error: Could not find a valid ADXL343 sensor. Check wiring!");
        while (1);
    }
    accel.setRange(ADXL343_RANGE_16_G); // Set the range (+-2g, 4g, 8g, or 16g)
    Serial.println("ADXL343 Initialized.");
    
    // --- Initialize MAX30102 ---
    if (!setupOximeter()) {
    while (1); // Freeze on setup failure.
    }

    Serial.println("\n--- Starting Data Stream (CSV Format) ---");
    Serial.println("X, Y, Z, HeartRate, SpO2, intial voltage, calculated voltage");
}

void loop() {

    // --- 1. Oximeter Data Acquisition ---
    uint32_t latestRawRed = 0;
    uint32_t latestRawIR = 0;
    bool oximeterSampleRead = false;

    while (getOximeterData(latestRawRed, latestRawIR)) {
        oximeterSampleRead = true; // Set a flag to know we got at least one sample.
    }

    // --- 2. accelerometer ---
    sensors_event_t event;
    accel.getEvent(&event);

    // --- 3. ADC Measurement ---
    double voltage0 = adc.readVolts(0);
    double voltage1 = adc.readVolts(1);
    double resistance = 0.0;
    if (voltage1 != 0) {
        resistance = (voltage0 * 1000000) / voltage1;
    }

    // --- 4. Synchronized Serial Output ---

    // Accelerometer Data
    Serial.print("XYZ: ");
    Serial.print(event.acceleration.x, 2); // Print with 2 decimal places
    Serial.print(",");
    Serial.print(event.acceleration.y, 2);
    Serial.print(",");
    Serial.print(event.acceleration.z, 2);

    // Oximeter Data (print the LAST values we read)
    Serial.print(" | Raw Red/IR: ");
    if (oximeterSampleRead) {
        Serial.print(latestRawRed);
        Serial.print(",");
        Serial.print(latestRawIR);
    } else {
        Serial.print("--,--"); // Print placeholders if FIFO was empty this cycle
    }

    // ADC Data
    Serial.print(" | ADC V0,V1: ");
    Serial.print(voltage0, 3);
    Serial.print("V,");
    Serial.print(voltage1, 3);
    Serial.print("V");

    Serial.print(" | Res: ");
    if (resistance > 0) {
        Serial.print(resistance, 0); 
    } else {
        Serial.print("0");
    }
    Serial.println(" Ohms");
    
    delay(100);
}