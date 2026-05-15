#include "eepromi2c.h"

int currentAddress = 10;

//Escrita
void eepromWriteBytes(uint16_t address, uint8_t* data, uint16_t length) {

    while (length > 0) {

        Wire.beginTransmission(EEPROM_I2C_ADDRESS);

        Wire.write((uint8_t)(address >> 8));     // MSB
        Wire.write((uint8_t)(address & 0xFF));   // LSB

        uint8_t chunk = min(length, (uint16_t)EEPROM_PAGE_SIZE);

        for (uint8_t i = 0; i < chunk; i++) {
            Wire.write(data[i]);
        }

        Wire.endTransmission();
        delay(5);

        address += chunk;
        data += chunk;
        length -= chunk;
    }
}

//Leitura
void eepromReadBytes(uint16_t address, uint8_t* data, uint16_t length) {

    while (length > 0) {

        Wire.beginTransmission(EEPROM_I2C_ADDRESS);

        Wire.write((uint8_t)(address >> 8));
        Wire.write((uint8_t)(address & 0xFF));

        Wire.endTransmission();

        uint8_t chunk = min(length, (uint16_t)28);

        Wire.requestFrom(EEPROM_I2C_ADDRESS, chunk);

        for (uint8_t i = 0; i < chunk && Wire.available(); i++) {
            data[i] = Wire.read();
        }

        address += chunk;
        data += chunk;
        length -= chunk;
    }
}