#include <SPI.h>
#include <Arduino.h>
// ADC Settings
const int adc_max_sampling_rate = 1000000;
uint8_t adcSPI_MODE = SPI_MODE3;
const int adc_CS = 15;
uint8_t adcReadOrder = SPI_MSBFIRST;
const byte ADDR_CHANNEL_0_REG = 0x09;
const byte ADDR_FILTER_0_REG = 0x21;
const byte ADDR_ADC_CONTROL_REG = 0x01;
const byte ADDR_STATUS_REG = 0x00;
const byte  ADDR_DATA_REG = 0x02;