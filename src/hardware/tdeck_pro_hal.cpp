#include "tdeck_pro_hal.h"

#if defined(LILYGO_T_DECK_PRO)

#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#undef DEFAULT_SDA
#undef DEFAULT_SCL
#include "bq27220.h"

int hyn_touch_init(void);
uint8_t hyn_touch_get_point(int16_t *x_array, int16_t *y_array, uint8_t get_point);

namespace {
    constexpr size_t EPD_STRIDE = (BOARD_EPD_WIDTH + 7) / 8;
    constexpr size_t EPD_BUFFER_SIZE = EPD_STRIDE * BOARD_EPD_HEIGHT;
    constexpr uint8_t KEYBOARD_ROWS = 4;
    constexpr uint8_t KEYBOARD_COLS = 10;
    constexpr uint8_t KEYPAD_PRESS_MIN = 129;
    constexpr uint8_t KEYPAD_PRESS_MAX = 163;
    constexpr uint32_t EPD_SPI_HZ = 2000000;

    const char keymap[KEYBOARD_ROWS][KEYBOARD_COLS] = {
        {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
        {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '0'},
        {'2', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '$', 'E'},
        {' ', ' ', ' ', ' ', ' ', '-', '*', 'S', '0', 'U'},
    };

    BQ27220 batteryGauge;
    Adafruit_DRV2605 hapticDrv;
}

TDeckProHal watch;

TDeckProHal::TDeckProHal()
    : display(GxEPD2_310_GDEQ031T10(BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST, BOARD_EPD_BUSY)) {
}

bool TDeckProHal::begin(Stream *stream) {
    (void)stream;

    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)BOARD_6609_EN);
    gpio_hold_dis((gpio_num_t)BOARD_LORA_EN);
    gpio_hold_dis((gpio_num_t)BOARD_GPS_EN);
    gpio_hold_dis((gpio_num_t)BOARD_A7682E_PWRKEY);
    gpio_hold_dis((gpio_num_t)BOARD_EPD_BL);
    gpio_hold_dis((gpio_num_t)BOARD_MOTOR_PIN);

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire.setClock(400000);

    setupSharedSpiPins();
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    if (spi_mutex == nullptr) {
        spi_mutex = xSemaphoreCreateRecursiveMutex();
    }

    pinMode(BOARD_EPD_BL, OUTPUT);
    digitalWrite(BOARD_EPD_BL, HIGH);
    display_powered = true;

    epd_buffer = static_cast<uint8_t *>(heap_caps_calloc(EPD_BUFFER_SIZE, sizeof(uint8_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (epd_buffer == nullptr) {
        epd_buffer = static_cast<uint8_t *>(heap_caps_calloc(EPD_BUFFER_SIZE, sizeof(uint8_t), MALLOC_CAP_8BIT));
    }
    if (epd_buffer != nullptr) {
        memset(epd_buffer, 0xff, EPD_BUFFER_SIZE);
    }

    epdTransferBegin();
    display.epd2.selectSPI(SPI, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
    display.init(115200, true, 2, false);
    display.setRotation(rotation);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());
    display.powerOff();
    epdTransferEnd();

    pinMode(BOARD_TOUCH_INT, INPUT_PULLUP);
    touch_ready = hyn_touch_init() != 0;
    cst_touch_ready = false;
    if (!touch_ready) {
        touch.setPins(BOARD_TOUCH_RST, BOARD_TOUCH_INT);
        cst_touch_ready = touch.begin(Wire, BOARD_I2C_ADDR_TOUCH, BOARD_I2C_SDA, BOARD_I2C_SCL);
        touch_ready = cst_touch_ready;
    }
    Serial.printf("[TDECKPRO] touch: %s (%s)\r\n", touch_ready ? "OK" : "FAIL", cst_touch_ready ? "CST fallback" : "HYN");

    setupKeyboard();
    setupHaptics();
    setupBatteryGauge();

    pinMode(BOARD_GPS_EN, OUTPUT);
    digitalWrite(BOARD_GPS_EN, HIGH);

    bootSelfTest();

    return true;
}

void TDeckProHal::setupSharedSpiPins() {
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    pinMode(BOARD_6609_EN, OUTPUT);
    digitalWrite(BOARD_6609_EN, HIGH);
    pinMode(BOARD_A7682E_PWRKEY, OUTPUT);
    digitalWrite(BOARD_A7682E_PWRKEY, HIGH);
    pinMode(BOARD_EPD_CS, OUTPUT);
    digitalWrite(BOARD_EPD_CS, HIGH);
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    pinMode(RADIO_CS_PIN, OUTPUT);
    digitalWrite(RADIO_CS_PIN, HIGH);
    pinMode(RADIO_RST_PIN, OUTPUT);
    digitalWrite(RADIO_RST_PIN, HIGH);
    pinMode(BOARD_LORA_EN, OUTPUT);
    digitalWrite(BOARD_LORA_EN, HIGH);
}

void TDeckProHal::setupKeyboard() {
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
    pinMode(BOARD_KEYBOARD_LED, OUTPUT);
    digitalWrite(BOARD_KEYBOARD_LED, LOW);
    keyboard_ready = keyboard.begin(BOARD_I2C_ADDR_KEYBOARD, &Wire);
    if (keyboard_ready) {
        keyboard.matrix(KEYBOARD_ROWS, KEYBOARD_COLS);
        keyboard.flush();
    }
}

void TDeckProHal::setupHaptics() {
    pinMode(BOARD_MOTOR_PIN, OUTPUT);
    digitalWrite(BOARD_MOTOR_PIN, HIGH);
    drv2605_ready = i2cDevicePresent(BOARD_I2C_ADDR_DRV2605) && hapticDrv.begin(&Wire);
    if (drv2605_ready) {
        hapticDrv.selectLibrary(1);
        hapticDrv.setWaveform(0, 1);
        hapticDrv.setWaveform(1, 0);
        hapticDrv.setMode(DRV2605_MODE_INTTRIG);
    }
    Serial.printf("[TDECKPRO] haptic DRV2605: %s\r\n", drv2605_ready ? "OK" : "FAIL");
}

void TDeckProHal::setupBatteryGauge() {
    battery_ready = i2cDevicePresent(0x55);
    if (battery_ready) {
        batteryGauge.init();
    }
}

void TDeckProHal::bootSelfTest() {
    Serial.printf("[TDECKPRO][SELFTEST] V2 pins: TOUCH_RST=%d EPD_RST=%d EPD_BL=%d TOUCH=%s HAPTIC=%s BQ27220=%s\r\n",
                  BOARD_TOUCH_RST,
                  BOARD_EPD_RST,
                  BOARD_EPD_BL,
                  touch_ready ? "OK" : "FAIL",
                  drv2605_ready ? "OK" : "FAIL",
                  battery_ready ? "OK" : "FAIL");

    digitalWrite(BOARD_EPD_BL, HIGH);
    display_powered = true;

    if (drv2605_ready) {
        hapticDrv.setWaveform(0, 1);
        hapticDrv.setWaveform(1, 0);
        hapticDrv.go();
        delay(90);
        hapticDrv.setWaveform(0, 47);
        hapticDrv.setWaveform(1, 0);
        hapticDrv.go();
    }

    epdTransferBegin();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(8, 34);
        display.print("XNODE T-DECK PRO");
        display.setCursor(8, 58);
        display.print("LILYGO V2 HAL");
        display.setCursor(8, 86);
        display.print("TOUCH ");
        display.print(touch_ready ? "OK" : "FAIL");
        display.setCursor(8, 110);
        display.print("HAPTIC ");
        display.print(drv2605_ready ? "OK" : "FAIL");
        display.setCursor(8, 134);
        display.print("EPD BL GPIO45 ON");
    } while (display.nextPage());
    display.powerOff();
    epdTransferEnd();
}

bool TDeckProHal::i2cDevicePresent(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

void TDeckProHal::selectSharedSpiIdle() {
    digitalWrite(BOARD_EPD_CS, HIGH);
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    digitalWrite(RADIO_CS_PIN, HIGH);
}

void TDeckProHal::epdTransferBegin() {
    if (spi_mutex != nullptr) {
        xSemaphoreTakeRecursive(spi_mutex, portMAX_DELAY);
    }
    selectSharedSpiIdle();
}

void TDeckProHal::epdTransferEnd() {
    selectSharedSpiIdle();
    if (spi_mutex != nullptr) {
        xSemaphoreGiveRecursive(spi_mutex);
    }
}

void TDeckProHal::setSwapBytes(bool swap) {
    (void)swap;
}

void TDeckProHal::fillScreen(uint32_t color) {
    if (epd_buffer != nullptr) {
        memset(epd_buffer, color == TFT_BLACK ? 0x00 : 0xff, EPD_BUFFER_SIZE);
    }
    epdTransferBegin();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(color == TFT_BLACK ? GxEPD_BLACK : GxEPD_WHITE);
    } while (display.nextPage());
    display.powerOff();
    epdTransferEnd();
}

void TDeckProHal::startWrite() {
}

void TDeckProHal::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void TDeckProHal::pushColors(const uint16_t *data, int32_t len, bool swap) {
    (void)data;
    (void)len;
    (void)swap;
}

void TDeckProHal::endWrite() {
}

void TDeckProHal::setRotation(uint8_t next_rotation) {
    rotation = next_rotation % 4;
    epdTransferBegin();
    display.setRotation(rotation);
    epdTransferEnd();
}

uint8_t TDeckProHal::getRotation() const {
    return rotation;
}

void TDeckProHal::setBrightness(uint8_t level) {
    brightness = level;
    if (display_powered) {
        digitalWrite(BOARD_EPD_BL, HIGH);
    }
}

void TDeckProHal::displaySleep() {
    epdTransferBegin();
    display.hibernate();
    epdTransferEnd();
    digitalWrite(BOARD_EPD_BL, LOW);
    display_powered = false;
}

void TDeckProHal::displayWakeup() {
    digitalWrite(BOARD_EPD_BL, HIGH);
    display_powered = true;
}

void TDeckProHal::encodeArea(const lv_area_t *area, const lv_color_t *color_p) {
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;

    if (epd_buffer == nullptr) {
        return;
    }

    for (int32_t y = 0; y < height; ++y) {
        const int32_t screen_y = area->y1 + y;
        if (screen_y < 0 || screen_y >= BOARD_EPD_HEIGHT) {
            continue;
        }
        for (int32_t x = 0; x < width; ++x) {
            const int32_t screen_x = area->x1 + x;
            if (screen_x < 0 || screen_x >= BOARD_EPD_WIDTH) {
                continue;
            }
            const size_t src_index = y * width + x;
            const size_t dst_index = screen_y * EPD_STRIDE + (screen_x / 8);
            const uint8_t mask = 0x80 >> (screen_x & 0x07);
            const uint8_t brightness_value = lv_color_brightness(color_p[src_index]);
            if (brightness_value < 128) {
                epd_buffer[dst_index] &= ~mask;
            }
            else {
                epd_buffer[dst_index] |= mask;
            }
        }
    }
}

void TDeckProHal::drawBufferWindow(int16_t x, int16_t y, int16_t w, int16_t h, bool partial) {
    if (partial) {
        display.setPartialWindow(x, y, w, h);
    }
    else {
        display.setFullWindow();
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        // Draw the full backing store so GxEPD2-expanded partial windows retain surrounding pixels.
        display.drawInvertedBitmap(0, 0, epd_buffer, BOARD_EPD_WIDTH, BOARD_EPD_HEIGHT, GxEPD_BLACK);
    } while (display.nextPage());

    if (partial) {
        display.powerOff();
    }
}

void TDeckProHal::flushArea(const lv_area_t *area, const lv_color_t *color_p) {
    if (area == nullptr || color_p == nullptr || epd_buffer == nullptr) {
        return;
    }

    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;
    if (width <= 0 || height <= 0) {
        return;
    }

    encodeArea(area, color_p);
}

void TDeckProHal::refreshArea(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (epd_buffer == nullptr || w <= 0 || h <= 0) {
        return;
    }

    int16_t right = x + w - 1;
    int16_t bottom = y + h - 1;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (right >= BOARD_EPD_WIDTH) {
        right = BOARD_EPD_WIDTH - 1;
    }
    if (bottom >= BOARD_EPD_HEIGHT) {
        bottom = BOARD_EPD_HEIGHT - 1;
    }
    if (right < x || bottom < y) {
        return;
    }

    x &= ~0x07;
    right |= 0x07;
    if (right >= BOARD_EPD_WIDTH) {
        right = BOARD_EPD_WIDTH - 1;
    }

    const uint32_t refresh_area = static_cast<uint32_t>(right - x + 1) * static_cast<uint32_t>(bottom - y + 1);
    const uint32_t full_area = static_cast<uint32_t>(BOARD_EPD_WIDTH) * static_cast<uint32_t>(BOARD_EPD_HEIGHT);
    if (refresh_area >= (full_area * 3U) / 4U) {
        refreshFull();
        return;
    }

    epdTransferBegin();
    drawBufferWindow(x, y, right - x + 1, bottom - y + 1, true);
    epdTransferEnd();
}

void TDeckProHal::refreshFull() {
    epdTransferBegin();
    if (epd_buffer != nullptr) {
        drawBufferWindow(0, 0, BOARD_EPD_WIDTH, BOARD_EPD_HEIGHT, false);
    }
    epdTransferEnd();
}

bool TDeckProHal::getTouched() {
    return touch_ready && digitalRead(BOARD_TOUCH_INT) == LOW;
}

uint8_t TDeckProHal::getPoint(int16_t *x, int16_t *y) {
    if (!touch_ready || x == nullptr || y == nullptr) {
        return 0;
    }
    if (!cst_touch_ready) {
        return hyn_touch_get_point(x, y, 1);
    }
    return touch.getPoint(x, y, 1);
}

void TDeckProHal::interruptTrigger() {
    if (!touch_ready) {
        return;
    }
    int16_t x = 0;
    int16_t y = 0;
    if (!cst_touch_ready) {
        hyn_touch_get_point(&x, &y, 1);
    }
    else {
        touch.getPoint(&x, &y, 1);
    }
}

void TDeckProHal::setMonitorTime(uint8_t time) {
    (void)time;
}

char TDeckProHal::readKeyboardChar() {
    if (!keyboard_ready || keyboard.available() == 0) {
        return '\0';
    }

    int key = keyboard.getEvent();
    if (key < KEYPAD_PRESS_MIN || key > KEYPAD_PRESS_MAX) {
        return '\0';
    }

    key -= KEYPAD_PRESS_MIN;
    const uint8_t row = key / KEYBOARD_COLS;
    const uint8_t col = (KEYBOARD_COLS - 1) - (key % KEYBOARD_COLS);
    if (row >= KEYBOARD_ROWS || col >= KEYBOARD_COLS) {
        return '\0';
    }

    const char mapped = keymap[row][col];
    if (mapped == 'E') {
        return '\n';
    }
    if (mapped == 'U') {
        return LV_KEY_UP;
    }
    if (mapped == 'S') {
        return LV_KEY_BACKSPACE;
    }
    return mapped;
}

void TDeckProHal::powerIoctl(PowerCtrlChannel ch, bool enable) {
    switch (ch) {
        case WATCH_POWER_DISPLAY_BL:
            setBrightness(enable ? brightness : 0);
            break;
        case WATCH_POWER_RADIO:
            digitalWrite(BOARD_LORA_EN, enable ? HIGH : LOW);
            break;
        case WATCH_POWER_GPS:
        case WATCH_POWER_GPS_DC_CHANNEL:
            digitalWrite(BOARD_GPS_EN, enable ? HIGH : LOW);
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
        case WATCH_POWER_DRV2605:
            break;
        case WATCH_POWER_TOUCH_DISP:
        default:
            break;
    }
}

uint64_t TDeckProHal::readPMU() {
    return 0;
}

void TDeckProHal::clearPMU() {
}

bool TDeckProHal::isVbusIn() {
    return false;
}

bool TDeckProHal::isCharging() {
    return battery_ready && batteryGauge.getIsCharging();
}

bool TDeckProHal::isBatteryConnect() {
    return getBattVoltage() > 0;
}

bool TDeckProHal::isVbusInsertIrq() {
    return false;
}

bool TDeckProHal::isVbusRemoveIrq() {
    return false;
}

bool TDeckProHal::isBatChagerStartIrq() {
    return false;
}

bool TDeckProHal::isBatChagerDoneIrq() {
    return false;
}

bool TDeckProHal::isBatInsertIrq() {
    return false;
}

bool TDeckProHal::isBatRemoveIrq() {
    return false;
}

bool TDeckProHal::isPekeyShortPressIrq() {
    return false;
}

bool TDeckProHal::isPekeyLongPressIrq() {
    return false;
}

int TDeckProHal::getBatteryPercent() {
    if (!battery_ready) {
        return 0;
    }
    const uint16_t soc = batteryGauge.getStateOfCharge();
    return soc > 100 ? 100 : soc;
}

uint16_t TDeckProHal::getBattVoltage() {
    return battery_ready ? batteryGauge.getVoltage() : 0;
}

uint16_t TDeckProHal::getVbusVoltage() {
    return 0;
}

void TDeckProHal::setChargeTargetVoltage(uint8_t opt) {
    (void)opt;
}

void TDeckProHal::setDC3Voltage(uint16_t millivolt) {
    (void)millivolt;
}

void TDeckProHal::shutdown() {
    powerIoctl(WATCH_POWER_GPS, false);
    powerIoctl(WATCH_POWER_RADIO, false);
    displaySleep();
}

void TDeckProHal::run() {
    vibrate();
}

void TDeckProHal::vibrate(uint16_t duration_ms) {
    if (drv2605_ready) {
        hapticDrv.setWaveform(0, 1);
        hapticDrv.setWaveform(1, 0);
        hapticDrv.go();
        return;
    }

    digitalWrite(BOARD_MOTOR_PIN, HIGH);
    delay(duration_ms);
    digitalWrite(BOARD_MOTOR_PIN, LOW);
}

#endif
