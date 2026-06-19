#pragma once

#if defined(LILYGO_WATCH_ULTRA)

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <RadioLib.h>
#include <XPowersLib.h>

#define BOARD_TFT_WIDTH             (410)
#define BOARD_TFT_HEIHT             (502)
#define TFT_WIDTH                   BOARD_TFT_WIDTH
#define TFT_HEIGHT                  BOARD_TFT_HEIHT
#ifndef TFT_BLACK
#define TFT_BLACK                   0x0000
#endif

#define T_WATCH_ULTRA_SAFE_LEFT     (24)
#define T_WATCH_ULTRA_SAFE_TOP      (36)
#define T_WATCH_ULTRA_SAFE_RIGHT    (24)
#define T_WATCH_ULTRA_SAFE_BOTTOM   (26)
#define T_WATCH_ULTRA_SAFE_WIDTH    (TFT_WIDTH - T_WATCH_ULTRA_SAFE_LEFT - T_WATCH_ULTRA_SAFE_RIGHT)
#define T_WATCH_ULTRA_SAFE_HEIGHT   (TFT_HEIGHT - T_WATCH_ULTRA_SAFE_TOP - T_WATCH_ULTRA_SAFE_BOTTOM)

#define BOARD_I2C_SDA               (3)
#define BOARD_I2C_SCL               (2)
#define BOARD_PMU_INT               (7)
#define BOARD_BHI260_INT            (8)
#define BOARD_BHI260_RST            (-1)

#define BOARD_TOUCH_INT             (12)
#define BOARD_TOUCH_ADDR            (0x1A)

#define BOARD_GPS_1PPS              (13)
#define SHIELD_GPS_RX               (44)
#define SHIELD_GPS_TX               (43)
#define BOARD_GPS_BAUDRATE          (38400)

#define DISP_CS                     (41)
#define DISP_SCK                    (40)
#define DISP_RESET                  (37)
#define DISP_TE                     (6)
#define DISP_D0                     (38)
#define DISP_D1                     (39)
#define DISP_D2                     (42)
#define DISP_D3                     (45)

#define SD_SCLK                     (35)
#define SD_MISO                     (33)
#define SD_MOSI                     (34)
#define SD_CS                       (21)

#define SDCARD_CS                   SD_CS
#define NFC_CS                      (4)

#define BOARD_RADIO_SCK             (35)
#define BOARD_RADIO_MISO            (33)
#define BOARD_RADIO_MOSI            (34)
#define BOARD_RADIO_SS              (36)
#define BOARD_RADIO_DI01            (14)
#define BOARD_RADIO_RST             (47)
#define BOARD_RADIO_BUSY            (48)
#define BOARD_RADIO_DI02            (48)

#define EXPANDS_DRV_EN              (6)
#define EXPANDS_DISP_EN             (7)
#define EXPANDS_TOUCH_RST           (8)
#define EXPANDS_SD_DET              (10)
#define EXPANDS_LORA_RF_SW          (11)

enum PowerCtrlChannel {
    WATCH_POWER_DISPLAY_BL,
    WATCH_POWER_TOUCH_DISP,
    WATCH_POWER_RADIO,
    WATCH_POWER_DRV2605,
    WATCH_POWER_GPS,
    WATCH_POWER_GPS_DC_CHANNEL
};

class TWatchUltraPanel : public lgfx::Panel_CO5300 {
public:
    const uint8_t *getInitCommands(uint8_t listno) const override;
};

class TWatchUltraDisplay : public lgfx::LGFX_Device {
public:
    TWatchUltraDisplay();

private:
    TWatchUltraPanel panel;
    lgfx::Bus_SPI bus;
};

class TWatchUltraHal {
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

    bool getTouched();
    uint8_t getPoint(int16_t *x, int16_t *y);
    void interruptTrigger();
    void setMonitorTime(uint8_t time);

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
    bool beginPower(Stream *stream);
    bool beginExpander();
    bool expanderRead(uint8_t reg, uint8_t &value);
    bool expanderWrite(uint8_t reg, uint8_t value);
    bool expanderPinMode(uint8_t pin, bool output);
    bool expanderDigitalWrite(uint8_t pin, bool high);
    void beginRadioBus();

    TWatchUltraDisplay display;
    XPowersAXP2101 power;
    uint8_t rotation = 0;
    bool touch_ready = false;
    bool expander_ready = false;
};

extern TWatchUltraHal watch;

#define newModule() new Module(BOARD_RADIO_SS, BOARD_RADIO_DI01, BOARD_RADIO_RST, BOARD_RADIO_BUSY)

#endif
