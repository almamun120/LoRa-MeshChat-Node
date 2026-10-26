#include "lora_radio.h"
#include "config.h"
#include <RadioLib.h>
#include <SPI.h>

static SX1278* radio = nullptr;
static volatile bool rxFlag = false;
static void IRAM_ATTR onRxIrq() { rxFlag = true; }

bool LoraRadio::begin(const Settings& s) {
    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    radio = new SX1278(new Module(PIN_LORA_NSS, PIN_LORA_DIO0, PIN_LORA_RST, RADIOLIB_NC));
    // begin(freq MHz, bw kHz, sf, cr (5..8 = 4/5..4/8), sync word, power dBm, preamble, gain)
    err_ = radio->begin(s.freq, s.bw, s.sf, s.cr, s.sync, s.power, 8, 0);
    if (err_ != RADIOLIB_ERR_NONE) { Serial.printf("lora: begin failed (%d) - check wiring\n", err_); ok_ = false; return false; }
    radio->setCRC(true);
    radio->setCurrentLimit(100);
    radio->setPacketReceivedAction(onRxIrq);
    err_ = radio->startReceive();
    ok_ = err_ == RADIOLIB_ERR_NONE;
    Serial.printf("lora: %.3f MHz SF%d BW%.1f CR4/%d %d dBm sync 0x%02X -> %s\n", s.freq, s.sf, s.bw, s.cr, s.power, s.sync, ok_ ? "ready" : "ERROR");
    return ok_;
}

void LoraRadio::poll() {
    if (!ok_ || !rxFlag) return;
    rxFlag = false;
    size_t len = radio->getPacketLength();
    if (len > 0 && len <= 255) {
        uint8_t buf[256];
        int st = radio->readData(buf, len);
        if (st == RADIOLIB_ERR_NONE) {
            rssi_ = radio->getRSSI();
            snr_ = radio->getSNR();
            if (onFrame) onFrame(mc::Bytes(buf, buf + len), rssi_, snr_);
        }
    }
    radio->startReceive();
}

bool LoraRadio::transmit(const mc::Bytes& frame) {
    if (!ok_) return false;
    int st = radio->transmit(const_cast<uint8_t*>(frame.data()), frame.size());
    rxFlag = false;
    radio->startReceive();
    return st == RADIOLIB_ERR_NONE;
}

bool LoraRadio::channelBusy() {
    if (!ok_) return false;
    int st = radio->scanChannel();          // preamble detection, a few milliseconds
    rxFlag = false;
    radio->startReceive();
    return st == RADIOLIB_LORA_DETECTED;
}
