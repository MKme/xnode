#pragma once

#if defined(LILYGO_T_DECK_PRO)
#include "tdeck_pro_hal.h"
#endif

#if defined(LILYGO_T_DECK_PLUS)

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TouchDrvGT911.hpp>
#include <Wire.h>

#define BOARD_TFT_WIDTH             (320)
#define BOARD_TFT_HEIGHT            (240)
#ifndef TFT_WIDTH
#define TFT_WIDTH                   BOARD_TFT_WIDTH
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT                  BOARD_TFT_HEIGHT
#endif
#ifndef TFT_BLACK
#define TFT_BLACK                   0x0000
#endif

#define BOARD_POWERON               (10)
#define BOARD_I2C_SDA               (18)
#define BOARD_I2C_SCL               (8)
#define BOARD_BAT_ADC               (4)
#define BOARD_TOUCH_INT             (16)
#define BOARD_KEYBOARD_INT          (46)

#define BOARD_SPI_MOSI              (41)
#define BOARD_SPI_MISO              (38)
#define BOARD_SPI_SCK               (40)
#define BOARD_SDCARD_CS             (39)
#define BOARD_TFT_CS                (12)
#define BOARD_TFT_DC                (11)
#define BOARD_TFT_BACKLIGHT         (42)
#define BOARD_BL_PIN                BOARD_TFT_BACKLIGHT

#define RADIO_CS_PIN                (9)
#define RADIO_BUSY_PIN              (13)
#define RADIO_RST_PIN               (17)
#define RADIO_DIO1_PIN              (45)

#define BOARD_RADIO_SCK             BOARD_SPI_SCK
#define BOARD_RADIO_MISO            BOARD_SPI_MISO
#define BOARD_RADIO_MOSI            BOARD_SPI_MOSI
#define BOARD_RADIO_SS              RADIO_CS_PIN
#define BOARD_RADIO_DI01            RADIO_DIO1_PIN
#define BOARD_RADIO_RST             RADIO_RST_PIN
#define BOARD_RADIO_BUSY            RADIO_BUSY_PIN

#define BOARD_GPS_TX_PIN            (43)
#define BOARD_GPS_RX_PIN            (44)
#define SHIELD_GPS_TX               BOARD_GPS_TX_PIN
#define SHIELD_GPS_RX               BOARD_GPS_RX_PIN
#define BOARD_GPS_BAUDRATE          (9600)

#define SD_CS                       BOARD_SDCARD_CS
#define SDCARD_CS                   BOARD_SDCARD_CS

enum PowerCtrlChannel {
    WATCH_POWER_DISPLAY_BL,
    WATCH_POWER_TOUCH_DISP,
    WATCH_POWER_RADIO,
    WATCH_POWER_DRV2605,
    WATCH_POWER_GPS,
    WATCH_POWER_GPS_DC_CHANNEL
};

class TDeckPlusHal {
public:
    bool begin(Stream *stream = nullptr);

    void setSwapBytes(bool swap);
    void fillScreen(uint32_t color);
    void startWrite();
    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h);
    void pushColors(const uint16_t *data, int32_t len, bool swap);
    void endWrite();
    void setRotation(uint8_t rotation);
    uint8_t getRotation() const;
    void setBrightness(uint8_t level);
    void displaySleep();
    void displayWakeup();

    bool getTouched();
    uint8_t getPoint(int16_t *x, int16_t *y);
    void interruptTrigger();
    void setMonitorTime(uint8_t time);

    char readKeyboardChar();

    void powerIoctl(PowerCtrlChannel ch, bool enable);
    uint64_t readPMU();
    void clearPMU();
    bool isVbusIn();
    bool isCharging();
    bool isBatteryConnect();
    bool isVbusInsertIrq();
    bool isVbusRemoveIrq();
    bool isBatChagerStartIrq();
    bool isBatChagerDoneIrq();
    bool isBatInsertIrq();
    bool isBatRemoveIrq();
    bool isPekeyShortPressIrq();
    bool isPekeyLongPressIrq();
    int getBatteryPercent();
    uint16_t getBattVoltage();
    uint16_t getVbusVoltage();
    void setChargeTargetVoltage(uint8_t opt);
    void setDC3Voltage(uint16_t millivolt);
    void shutdown();

    void run();

private:
    void setupSharedSpiPins();
    void setupBacklight();
    void applyPanelInit();
    uint16_t readBatteryMillivolts();

    TFT_eSPI display;
    TouchDrvGT911 touch;
    uint8_t rotation = 1;
    uint8_t brightness = 0;
    bool touch_ready = false;
    bool gps_powered = false;
};

extern TDeckPlusHal watch;

#define newModule() new Module(BOARD_RADIO_SS, BOARD_RADIO_DI01, BOARD_RADIO_RST, BOARD_RADIO_BUSY)

#endif
