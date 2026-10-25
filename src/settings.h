// Everything the setup page can change, kept in flash (NVS), plus the relay's identity key.
#pragma once
#include <Arduino.h>
#include "mc_crypto.h"

struct Settings {
    String name = "MeshRelay";          // chat name of this device (keyboard / LCD / Wi-Fi page users)
    String apSsid = "MeshChat-Relay";
    String apPass = "meshchat123";      // Wi-Fi password, 8+ characters (shorter = open network)
    String adminPass = "admin1234";     // password of the setup page (user: admin)
    float freq = 433.0f;                // MHz (RA-02: 410..525)
    int sf = 9;                         // spreading factor 7..12
    float bw = 125.0f;                  // kHz
    int cr = 5;                         // coding rate 4/5 .. 4/8
    int power = 14;                     // dBm, 2..17
    int sync = 0x12;                    // LoRa sync word: all relays of one network must match
    int duty = 10;                      // % of time the radio may transmit
    bool loraOn = true;
    int beaconMin = 12;                 // identity beacon interval (minutes)
    int loraTtl = 6;                    // hops a public post / identity may travel over LoRa (2..16)
    bool alertsOn = true;               // indicator LED + speaker
    bool alertSound = true;             // false = quiet mode: light only
    int ledPin = 2;                     // GPIO of the indicator LED (2 = the blue LED on the DevKit), -1 = none
    int buzPin = 17;                    // GPIO of the buzzer/speaker, -1 = none
    bool wxOn = true;                   // weather station: broadcast the BMP280 reading (nothing is sent while no sensor is found)
    int wxMin = 5;                      // minutes between two broadcasts (1..1440), set by hand on the setup page
    int wxAddr = 0;                     // BMP280 I2C address: 0 = find it (0x76 / 0x77)
    int lcdAddr = 0x27;                 // I2C address of the LCD backpack
    int wifiCh = 6;
};

void settingsLoad(Settings& s);
void settingsSave(const Settings& s);
/** Loads the identity key from flash, or creates one (Node ID starting with F0..FE so phones dial in) and stores it. */
bool identityLoad(mc::crypto::Identity& id);
void identityReset();      // forget the key: a new Node ID is created on the next boot
void factoryReset();       // settings + identity
