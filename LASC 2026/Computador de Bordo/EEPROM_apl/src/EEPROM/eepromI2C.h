#ifndef EEPROMI2C_H
#define EEPROMI2C_H

#include <Arduino.h>
#include <Wire.h>

// =====================
// CONFIG
// =====================
#define EEPROM_I2C_ADDRESS 0x50
#define EEPROM_PAGE_SIZE 16
#define EEPROM_MAX_SIZE 1484

// =====================
// VARIÁVEL GLOBAL
// =====================


extern int currentAddress;
// =====================
// FUNÇÕES LOW LEVEL
// =====================
void eepromWriteBytes(uint16_t address, uint8_t* data, uint16_t length);
void eepromReadBytes(uint16_t address, uint8_t* data, uint16_t length);

// =====================
// TEMPLATE (igual monolítico)
// =====================
template <class T>
void eepromWrite(uint16_t address, const T &value) {
    eepromWriteBytes(address, (uint8_t*)&value, sizeof(T));
}

template <class T>
void eepromRead(uint16_t address, T &value) {
    eepromReadBytes(address, (uint8_t*)&value, sizeof(T));
}

#endif