#include "tdeck_plus_hal.h"

#if defined(LILYGO_T_DECK_PLUS)

namespace {
    constexpr uint16_t TDECK_BATTERY_EMPTY_MV = 3300;
    constexpr uint16_t TDECK_BATTERY_FULL_MV = 4200;
    constexpr uint8_t TDECK_BACKLIGHT_LEVELS = 16;
    constexpr uint8_t TDECK_KEYBOARD_ADDR = 0x55;
    constexpr uint8_t TDECK_ROTATION = 1;

    struct PanelInitCommand {
        uint8_t cmd;
        const uint8_t *data;
        uint8_t len;
        uint16_t delay_ms;
    };

    const uint8_t panel_madctl[] = {0x55};
    const uint8_t panel_colmod[] = {0x05};
    const uint8_t panel_porctrl[] = {0x0b, 0x0b, 0x00, 0x33, 0x33};
    const uint8_t panel_gctrl[] = {0x75};
    const uint8_t panel_vcoms[] = {0x28};
    const uint8_t panel_lcmctrl[] = {0x2c};
    const uint8_t panel_vdvvrhen[] = {0x01};
    const uint8_t panel_vrhs[] = {0x1f};
    const uint8_t panel_vdvs[] = {0x20};
    const uint8_t panel_frctrl2[] = {0x13};
    const uint8_t panel_pwctrl1[] = {0xa7};
    const uint8_t panel_pvgamctrl[] = {
        0xd0, 0xd0, 0x05, 0x0e, 0x15, 0x0d, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19
    };
    const uint8_t panel_nvgamctrl[] = {
        0xd0, 0xd0, 0x05, 0x0e, 0x0c, 0x06, 0x2d, 0x44, 0x40, 0x0e, 0x1c, 0x18, 0x16, 0x19
    };

    const PanelInitCommand panel_init_commands[] = {
        {0x01, nullptr, 0, 120},
        {0x11, nullptr, 0, 120},
        {0x36, panel_madctl, sizeof(panel_madctl), 0},
        {0x3a, panel_colmod, sizeof(panel_colmod), 0},
        {0xb2, panel_porctrl, sizeof(panel_porctrl), 0},
        {0xb7, panel_gctrl, sizeof(panel_gctrl), 0},
        {0xbb, panel_vcoms, sizeof(panel_vcoms), 0},
        {0xc0, panel_lcmctrl, sizeof(panel_lcmctrl), 0},
        {0xc2, panel_vdvvrhen, sizeof(panel_vdvvrhen), 0},
        {0xc3, panel_vrhs, sizeof(panel_vrhs), 0},
        {0xc4, panel_vdvs, sizeof(panel_vdvs), 0},
        {0xc6, panel_frctrl2, sizeof(panel_frctrl2), 0},
        {0xd0, panel_pwctrl1, sizeof(panel_pwctrl1), 0},
        {0xe0, panel_pvgamctrl, sizeof(panel_pvgamctrl), 0},
        {0xe1, panel_nvgamctrl, sizeof(panel_nvgamctrl), 0},
        {0x21, nullptr, 0, 0},
        {0x29, nullptr, 0, 120},
    };
}

TDeckPlusHal watch;

bool TDeckPlusHal::begin(Stream *stream) {
    (void)stream;

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire.setClock(400000);

    setupSharedSpiPins();
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    setupBacklight();

    display.init();
    applyPanelInit();
    display.setRotation(TDECK_ROTATION);
    display.setSwapBytes(false);
    display.fillScreen(TFT_BLACK);
    setBrightness(0);

    pinMode(BOARD_TOUCH_INT, INPUT_PULLUP);
    touch.setPins(-1, BOARD_TOUCH_INT);
    touch_ready = touch.begin(Wire, GT911_SLAVE_ADDRESS_H, BOARD_I2C_SDA, BOARD_I2C_SCL);
    if (!touch_ready) {
        touch_ready = touch.begin(Wire, GT911_SLAVE_ADDRESS_L, BOARD_I2C_SDA, BOARD_I2C_SCL);
    }
    if (touch_ready) {
        touch.setMaxCoordinates(BOARD_TFT_WIDTH, BOARD_TFT_HEIGHT);
        touch.setSwapXY(true);
        touch.setMirrorXY(false, true);
        touch.setInterruptMode(FALLING);
    }

    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
    pinMode(BOARD_BAT_ADC, INPUT);

    return true;
}

void TDeckPlusHal::setupSharedSpiPins() {
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    pinMode(BOARD_TFT_CS, OUTPUT);
    digitalWrite(BOARD_TFT_CS, HIGH);
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    pinMode(RADIO_CS_PIN, OUTPUT);
    digitalWrite(RADIO_CS_PIN, HIGH);
}

void TDeckPlusHal::setupBacklight() {
    pinMode(BOARD_BL_PIN, OUTPUT);
    digitalWrite(BOARD_BL_PIN, LOW);
}

void TDeckPlusHal::applyPanelInit() {
    display.startWrite();
    for (const auto &entry : panel_init_commands) {
        display.writecommand(entry.cmd);
        for (uint8_t i = 0; i < entry.len; ++i) {
            display.writedata(entry.data[i]);
        }
        if (entry.delay_ms) {
            display.endWrite();
            delay(entry.delay_ms);
            display.startWrite();
        }
    }
    display.endWrite();
}

void TDeckPlusHal::setSwapBytes(bool swap) {
    display.setSwapBytes(swap);
}

void TDeckPlusHal::fillScreen(uint32_t color) {
    display.fillScreen(color);
}

void TDeckPlusHal::startWrite() {
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    digitalWrite(RADIO_CS_PIN, HIGH);
    display.startWrite();
}

void TDeckPlusHal::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
    display.setAddrWindow(x, y, w, h);
}

void TDeckPlusHal::pushColors(const uint16_t *data, int32_t len, bool swap) {
    display.pushColors(const_cast<uint16_t *>(data), len, swap);
}

void TDeckPlusHal::endWrite() {
    display.endWrite();
}

void TDeckPlusHal::setRotation(uint8_t next_rotation) {
    (void)next_rotation;
    rotation = TDECK_ROTATION;
    display.setRotation(rotation);
}

uint8_t TDeckPlusHal::getRotation() const {
    return rotation;
}

void TDeckPlusHal::setBrightness(uint8_t level) {
    brightness = level;
    const uint8_t target = map(level, 0, 255, 0, TDECK_BACKLIGHT_LEVELS);

    digitalWrite(BOARD_BL_PIN, LOW);
    delayMicroseconds(200);

    if (target == 0) {
        return;
    }

    digitalWrite(BOARD_BL_PIN, HIGH);
    delayMicroseconds(30);
    for (uint8_t i = 0; i < (TDECK_BACKLIGHT_LEVELS - target); ++i) {
        digitalWrite(BOARD_BL_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(BOARD_BL_PIN, HIGH);
        delayMicroseconds(2);
    }
}

void TDeckPlusHal::displaySleep() {
    digitalWrite(BOARD_BL_PIN, LOW);
}

void TDeckPlusHal::displayWakeup() {
    setBrightness(brightness);
}

bool TDeckPlusHal::getTouched() {
    return touch_ready && touch.isPressed();
}

uint8_t TDeckPlusHal::getPoint(int16_t *x, int16_t *y) {
    if (!touch_ready || !x || !y) {
        return 0;
    }

    int16_t raw_x = 0;
    int16_t raw_y = 0;
    uint8_t points = touch.getPoint(&raw_x, &raw_y, 1);
    if (points == 0) {
        return 0;
    }

    *x = raw_x;
    *y = raw_y;
    return points;
}

void TDeckPlusHal::interruptTrigger() {
    if (!touch_ready) {
        return;
    }
    int16_t x = 0;
    int16_t y = 0;
    touch.getPoint(&x, &y, 1);
}

void TDeckPlusHal::setMonitorTime(uint8_t time) {
    (void)time;
}

char TDeckPlusHal::readKeyboardChar() {
    Wire.requestFrom(static_cast<int>(TDECK_KEYBOARD_ADDR), 1);
    if (!Wire.available()) {
        return '\0';
    }
    return static_cast<char>(Wire.read());
}

void TDeckPlusHal::powerIoctl(PowerCtrlChannel ch, bool enable) {
    switch (ch) {
        case WATCH_POWER_DISPLAY_BL:
            setBrightness(enable ? brightness : 0);
            break;
        case WATCH_POWER_GPS:
        case WATCH_POWER_GPS_DC_CHANNEL:
            digitalWrite(BOARD_POWERON, HIGH);
            if (enable && !gps_powered) {
                pinMode(SHIELD_GPS_RX, INPUT_PULLUP);
                pinMode(SHIELD_GPS_TX, OUTPUT);
                Serial1.begin(BOARD_GPS_BAUDRATE, SERIAL_8N1, SHIELD_GPS_RX, SHIELD_GPS_TX);
                gps_powered = true;
            }
            else if (!enable && gps_powered) {
                Serial1.end();
                pinMode(SHIELD_GPS_RX, INPUT);
                pinMode(SHIELD_GPS_TX, INPUT);
                gps_powered = false;
            }
            break;
        case WATCH_POWER_TOUCH_DISP:
        case WATCH_POWER_RADIO:
        case WATCH_POWER_DRV2605:
        default:
            break;
    }
}

uint64_t TDeckPlusHal::readPMU() {
    return 0;
}

void TDeckPlusHal::clearPMU() {
}

bool TDeckPlusHal::isVbusIn() {
    return false;
}

bool TDeckPlusHal::isCharging() {
    return false;
}

bool TDeckPlusHal::isBatteryConnect() {
    return getBattVoltage() > 0;
}

bool TDeckPlusHal::isVbusInsertIrq() {
    return false;
}

bool TDeckPlusHal::isVbusRemoveIrq() {
    return false;
}

bool TDeckPlusHal::isBatChagerStartIrq() {
    return false;
}

bool TDeckPlusHal::isBatChagerDoneIrq() {
    return false;
}

bool TDeckPlusHal::isBatInsertIrq() {
    return false;
}

bool TDeckPlusHal::isBatRemoveIrq() {
    return false;
}

bool TDeckPlusHal::isPekeyShortPressIrq() {
    return false;
}

bool TDeckPlusHal::isPekeyLongPressIrq() {
    return false;
}

int TDeckPlusHal::getBatteryPercent() {
    const uint16_t mv = getBattVoltage();
    if (mv <= TDECK_BATTERY_EMPTY_MV) {
        return 0;
    }
    if (mv >= TDECK_BATTERY_FULL_MV) {
        return 100;
    }
    return (mv - TDECK_BATTERY_EMPTY_MV) * 100 / (TDECK_BATTERY_FULL_MV - TDECK_BATTERY_EMPTY_MV);
}

uint16_t TDeckPlusHal::getBattVoltage() {
    return readBatteryMillivolts();
}

uint16_t TDeckPlusHal::getVbusVoltage() {
    return 0;
}

void TDeckPlusHal::setChargeTargetVoltage(uint8_t opt) {
    (void)opt;
}

void TDeckPlusHal::setDC3Voltage(uint16_t millivolt) {
    (void)millivolt;
}

void TDeckPlusHal::shutdown() {
    setBrightness(0);
    digitalWrite(BOARD_POWERON, LOW);
}

void TDeckPlusHal::run() {
}

uint16_t TDeckPlusHal::readBatteryMillivolts() {
    uint32_t sum = 0;
    analogRead(BOARD_BAT_ADC);
    for (uint8_t i = 0; i < 8; ++i) {
        sum += analogRead(BOARD_BAT_ADC);
        delay(1);
    }
    const uint32_t adc = sum / 8;
    return static_cast<uint16_t>((adc * 3300UL * 2UL) / 4095UL);
}

#endif
