// Pin map for ESP32 DevKit V1 (ESP-WROOM-32). See docs/WIRING.md for the picture.
#pragma once

// Arduino IDE has no platformio.ini build flags, so the LCD type is set here: 1 = I2C backpack (4 wires), 0 = plain 16-pin LCD.
#ifndef LCD_I2C
#define LCD_I2C 1
#endif

// ---- LoRa RA-02 (SX1278) on the VSPI bus. 3.3 V ONLY (never 5 V!) ----
#define PIN_LORA_SCK   18
#define PIN_LORA_MISO  19
#define PIN_LORA_MOSI  23
#define PIN_LORA_NSS   5
#define PIN_LORA_RST   14
#define PIN_LORA_DIO0  26      // "packet received" interrupt

// ---- 1602 LCD ----
#if LCD_I2C
  #define PIN_LCD_SDA  21
  #define PIN_LCD_SCL  22
#else
  #define PIN_LCD_RS   13
  #define PIN_LCD_EN   27
  #define PIN_LCD_D4   32
  #define PIN_LCD_D5   33
  #define PIN_LCD_D6   25
  #define PIN_LCD_D7   4
#endif

// ---- BMP280 weather sensor: I2C, the same two wires as the LCD backpack (SDA 21, SCL 22), address 0x76 or 0x77. 3.3 V! ----
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    22

// ---- PS/2 keyboard (through a 5V -> 3.3V divider on both lines, see docs/WIRING.md) ----
#define PIN_PS2_CLK    34      // input-only pins are fine: we only listen
#define PIN_PS2_DATA   35

#define FIRMWARE_VERSION "1.5"
