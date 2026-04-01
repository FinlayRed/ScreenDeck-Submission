#ifndef __SD_CARD_H
#define __SD_CARD_H
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <ESP_IOExpander_Library.h>

// Extension IO pin definition
#define TP_RST 1      // Touch screen reset pin
#define LCD_BL 2      // LCD backlight pinout
#define LCD_RST 3     // LCD reset pin
#define SD_CS 4       // SD card select pin

// I2C pin definitions
#define I2C_MASTER_NUM I2C_NUM_0    // I2C Master Number
#define I2C_MASTER_SDA_IO 8         // I2C data line pins
#define I2C_MASTER_SCL_IO 9         // I2C clock line pin

#define SD_MOSI 11    // SD card master output slave input pin
#define SD_CLK  12    // SD card clock pin
#define SD_MISO 13    // SD card master input slave output pin
// Real SD CS is on CH422G (SD_CS). Arduino SD still requires a valid MCU pin.
#define SD_SS 6

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
void createDir(fs::FS &fs, const char * path);
void removeDir(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void readFile(fs::FS &fs, const char * path);
void deleteFile(fs::FS &fs, const char * path);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void testFileIO(fs::FS &fs, const char * path);

#endif
