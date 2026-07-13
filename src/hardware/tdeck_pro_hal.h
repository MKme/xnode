#pragma once

#if defined(LILYGO_T_DECK_PRO)

#include <Adafruit_TCA8418.h>
#include <Adafruit_DRV2605.h>
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <RadioLib.h>
#include <SPI.h>
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#include <lvgl.h>

#define BOARD_EPD_WIDTH             (240)
#define BOARD_EPD_HEIGHT            (320)
#define BOARD_TFT_WIDTH             BOARD_EPD_WIDTH
#define BOARD_TFT_HEIGHT            BOARD_EPD_HEIGHT
#ifndef TFT_WIDTH
#define TFT_WIDTH                   BOARD_EPD_WIDTH
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT                  BOARD_EPD_HEIGHT
#endif
#ifndef TFT_BLACK
#define TFT_BLACK                   0x0000
#endif
#ifndef TFT_WHITE
#define TFT_WHITE                   0xffff
#endif

#define BOARD_I2C_SDA               (13)
#define BOARD_I2C_SCL               (14)

#define BOARD_TOUCH_INT             (12)
#define BOARD_TOUCH_RST             (38)
#define BOARD_I2C_ADDR_TOUCH        (0x1a)

#define BOARD_KEYBOARD_INT          (15)
#define BOARD_KEYBOARD_LED          (42)
#define BOARD_I2C_ADDR_KEYBOARD     (0x34)

#define BOARD_SPI_SCK               (36)
#define BOARD_SPI_MOSI              (33)
#define BOARD_SPI_MISO              (47)

#define BOARD_EPD_CS                (34)
#define BOARD_EPD_DC                (35)
#define BOARD_EPD_BUSY              (37)
#define BOARD_EPD_RST               (16)
#define BOARD_EPD_BL                (45)
#define BOARD_TFT_CS                BOARD_EPD_CS

#define BOARD_SDCARD_CS             (48)
#define SD_CS                       BOARD_SDCARD_CS
#define SDCARD_CS                   BOARD_SDCARD_CS

#define BOARD_LORA_CS               (3)
#define BOARD_LORA_BUSY             (6)
#define BOARD_LORA_RST              (4)
#define BOARD_LORA_INT              (5)
#define BOARD_LORA_EN               (46)

#define BOARD_6609_EN               (41)
#define BOARD_A7682E_PWRKEY         (40)

#define RADIO_CS_PIN                BOARD_LORA_CS
#define RADIO_BUSY_PIN              BOARD_LORA_BUSY
#define RADIO_RST_PIN               BOARD_LORA_RST
#define RADIO_DIO1_PIN              BOARD_LORA_INT

#define BOARD_RADIO_SCK             BOARD_SPI_SCK
#define BOARD_RADIO_MISO            BOARD_SPI_MISO
#define BOARD_RADIO_MOSI            BOARD_SPI_MOSI
#define BOARD_RADIO_SS              RADIO_CS_PIN
#define BOARD_RADIO_DI01            RADIO_DIO1_PIN
#define BOARD_RADIO_RST             RADIO_RST_PIN
#define BOARD_RADIO_BUSY            RADIO_BUSY_PIN

#define BOARD_GPS_RXD               (44)
#define BOARD_GPS_TXD               (43)
#define BOARD_GPS_PPS               (1)
#define BOARD_GPS_EN                (39)
#define SHIELD_GPS_TX               BOARD_GPS_TXD
#define SHIELD_GPS_RX               BOARD_GPS_RXD
#define BOARD_GPS_BAUDRATE          (38400)

#define BOARD_MOTOR_PIN             (2)
#define BOARD_I2C_ADDR_DRV2605      (0x5a)

enum PowerCtrlChannel {
    WATCH_POWER_DISPLAY_BL,
    WATCH_POWER_TOUCH_DISP,
    WATCH_POWER_RADIO,
    WATCH_POWER_DRV2605,
    WATCH_POWER_GPS,
    WATCH_POWER_GPS_DC_CHANNEL
};

class TDeckProHal {
public:
    TDeckProHal();
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
    void flushArea(const lv_area_t *area, const lv_color_t *color_p);
    void refreshArea(int16_t x, int16_t y, int16_t w, int16_t h);
    void refreshFull();

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
    void vibrate(uint16_t duration_ms = 25);

private:
    void setupSharedSpiPins();
    void setupKeyboard();
    void setupHaptics();
    void setupBatteryGauge();
    void bootSelfTest();
    void selectSharedSpiIdle();
    bool i2cDevicePresent(uint8_t address);
    void epdTransferBegin();
    void epdTransferEnd();
    void encodeArea(const lv_area_t *area, const lv_color_t *color_p);
    void drawBufferWindow(int16_t x, int16_t y, int16_t w, int16_t h, bool partial);

    GxEPD2_BW<GxEPD2_310_GDEQ031T10, GxEPD2_310_GDEQ031T10::HEIGHT> display;
    TouchDrvCSTXXX touch;
    Adafruit_TCA8418 keyboard;
    SemaphoreHandle_t spi_mutex = nullptr;
    uint8_t *epd_buffer = nullptr;
    uint8_t rotation = 0;
    uint8_t brightness = 0;
    bool touch_ready = false;
    bool cst_touch_ready = false;
    bool keyboard_ready = false;
    bool gps_powered = false;
    bool display_powered = false;
    bool drv2605_ready = false;
    bool battery_ready = false;
};

extern TDeckProHal watch;

#define newModule() new Module(BOARD_RADIO_SS, BOARD_RADIO_DI01, BOARD_RADIO_RST, BOARD_RADIO_BUSY)

#endif
