#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Adafruit_DRV2605.h>
#include <Adafruit_TCA8418.h>
#include <TinyGPS++.h>
#include <TouchDrvCSTXXX.hpp>
#include "bq27220.h"
#include "driver/gpio.h"

#define BOARD_I2C_ADDR_TOUCH 0x1A
#define BOARD_I2C_ADDR_KEYBOARD 0x34
#define BOARD_I2C_ADDR_BQ27220 0x55
#define BOARD_I2C_ADDR_DRV2605 0x5A
#define BOARD_I2C_ADDR_BQ25896 0x6B

#define BOARD_I2C_SDA 13
#define BOARD_I2C_SCL 14
#define BOARD_KEYBOARD_INT 15
#define BOARD_KEYBOARD_LED 42
#define BOARD_TOUCH_INT 12
#define BOARD_TOUCH_RST 38
#define BOARD_SPI_SCK 36
#define BOARD_SPI_MOSI 33
#define BOARD_SPI_MISO 47
#define BOARD_EPD_DC 35
#define BOARD_EPD_CS 34
#define BOARD_EPD_BUSY 37
#define BOARD_EPD_RST 16
#define BOARD_EPD_BL 45
#define BOARD_SD_CS 48
#define BOARD_LORA_CS 3
#define BOARD_LORA_EN 46
#define BOARD_GPS_RXD 44
#define BOARD_GPS_TXD 43
#define BOARD_GPS_EN 39
#define BOARD_6609_EN 41
#define BOARD_A7682E_PWRKEY 40
#define BOARD_MOTOR_PIN 2

static constexpr uint32_t EPD_SPI_HZ = 2000000;
static constexpr uint32_t GPS_BAUD = 38400;
static constexpr int16_t SCREEN_W = 240;
static constexpr int16_t SCREEN_H = 320;

using InkPanel = GxEPD2_310_GDEQ031T10;
using InkDisplay = GxEPD2_BW<InkPanel, InkPanel::HEIGHT>;

static InkDisplay display(InkPanel(BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST, BOARD_EPD_BUSY));
static Adafruit_DRV2605 drv;
static Adafruit_TCA8418 keyboard;
static BQ27220 battery;
static TinyGPSPlus gps;
static TouchDrvCSTXXX touch;

static bool haptic_ok = false;
static bool touch_ok = false;
static bool keyboard_ok = false;
static bool battery_ok = false;
static uint8_t i2c_found = 0;
static uint32_t touch_count = 0;
static uint32_t redraw_count = 0;
static uint32_t last_status_draw_ms = 0;
static int wifi_count = -2;
static char keyboard_last = '\0';

enum Page : uint8_t {
    PAGE_HOME = 0,
    PAGE_GPS,
    PAGE_WIFI,
    PAGE_MESSAGES,
    PAGE_BATTERY,
    PAGE_KEYS,
    PAGE_DIAG,
    PAGE_COUNT
};

static Page current_page = PAGE_HOME;
static bool redraw_requested = true;

static const char *pageName(Page page)
{
    switch (page) {
        case PAGE_HOME: return "HOME";
        case PAGE_GPS: return "GPS";
        case PAGE_WIFI: return "WIFI";
        case PAGE_MESSAGES: return "MSGS";
        case PAGE_BATTERY: return "BAT";
        case PAGE_KEYS: return "KEYS";
        case PAGE_DIAG: return "DIAG";
        default: return "?";
    }
}

static void releaseSpiDevices()
{
    digitalWrite(BOARD_LORA_CS, HIGH);
    digitalWrite(BOARD_SD_CS, HIGH);
    digitalWrite(BOARD_EPD_CS, HIGH);
}

static void pulse(uint8_t effect = 1)
{
    if (!haptic_ok) {
        return;
    }
    drv.setWaveform(0, effect);
    drv.setWaveform(1, 0);
    drv.go();
}

static bool i2cPresent(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static bool touchReadReg16(uint16_t reg, uint8_t *buffer, uint8_t len)
{
    Wire.beginTransmission(BOARD_I2C_ADDR_TOUCH);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    const uint8_t got = Wire.requestFrom(BOARD_I2C_ADDR_TOUCH, len);
    if (got != len) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }

    for (uint8_t i = 0; i < len; ++i) {
        buffer[i] = Wire.read();
    }
    return true;
}

static bool touchWriteReg24(uint8_t b0, uint8_t b1, uint8_t b2)
{
    Wire.beginTransmission(BOARD_I2C_ADDR_TOUCH);
    Wire.write(b0);
    Wire.write(b1);
    Wire.write(b2);
    return Wire.endTransmission() == 0;
}

static uint8_t readTouchFramePoint(int16_t *x, int16_t *y)
{
    static uint32_t last_raw_log_ms = 0;
    uint8_t buffer[32] = {};
    const uint32_t now = millis();

    if (!touchReadReg16(0xD000, buffer, 7)) {
        if (now - last_raw_log_ms > 5000) {
            last_raw_log_ms = now;
            Serial.println("[XNODE-PRO] touch raw read failed");
        }
        return 0;
    }

    uint8_t points = 0;
    const bool has_frame = buffer[6] == 0xAB && buffer[0] != 0xAB;
    if (has_frame) {
        points = buffer[5] & 0x7F;
        if (points > 5) {
            points = 5;
        }

        uint8_t extra_len = 0;
        if (points > 1) {
            extra_len += (points - 1) * 5;
        }
        if ((buffer[5] & 0x80) != 0 && points != 0) {
            extra_len += 3;
        }
        if (extra_len > 0 && !touchReadReg16(0xD007, &buffer[5], extra_len)) {
            points = 0;
        }
    }

    touchWriteReg24(0xD0, 0x00, 0xAB);

    if (points == 0) {
        if (now - last_raw_log_ms > 5000) {
            last_raw_log_ms = now;
            Serial.printf("[XNODE-PRO] touch raw %02X %02X %02X %02X %02X %02X %02X irq=%d\n",
                          buffer[0], buffer[1], buffer[2], buffer[3],
                          buffer[4], buffer[5], buffer[6],
                          digitalRead(BOARD_TOUCH_INT));
        }
        return 0;
    }

    const uint8_t status = buffer[0] & 0x0F;
    const int16_t raw_x = static_cast<int16_t>((static_cast<uint16_t>(buffer[1]) << 4) | ((buffer[3] >> 4) & 0x0F));
    const int16_t raw_y = static_cast<int16_t>((static_cast<uint16_t>(buffer[2]) << 4) | (buffer[3] & 0x0F));
    if (raw_x < 0 || raw_x >= SCREEN_W || raw_y < 0 || raw_y >= SCREEN_H) {
        Serial.printf("[XNODE-PRO] touch raw out-of-range p=%u st=%u x=%d y=%d\n",
                      points, status, raw_x, raw_y);
        return 0;
    }

    *x = raw_x;
    *y = raw_y;
    Serial.printf("[XNODE-PRO] touch raw point p=%u st=%u x=%d y=%d irq=%d\n",
                  points, status, raw_x, raw_y, digitalRead(BOARD_TOUCH_INT));
    return 1;
}

static uint8_t scanI2c()
{
    uint8_t found = 0;
    Serial.println("[XNODE-PRO] I2C scan start");
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[XNODE-PRO] I2C 0x%02X\n", addr);
            ++found;
        }
    }
    Serial.printf("[XNODE-PRO] I2C scan done found=%u\n", found);
    return found;
}

static void textAt(int16_t x, int16_t y, const char *text)
{
    display.setCursor(x, y);
    display.print(text);
}

static void lineAt(int row, const char *text)
{
    textAt(10, 76 + row * 24, text);
}

static void lineAtf(int row, const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lineAt(row, buf);
}

static void drawFrame(const char *title)
{
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(0, 0, SCREEN_W, 48, GxEPD_BLACK);
    display.drawRect(0, 0, SCREEN_W, SCREEN_H, GxEPD_BLACK);
    display.drawFastHLine(0, 276, SCREEN_W, GxEPD_BLACK);

    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    textAt(8, 22, "XNODE T-DECK PRO");
    textAt(8, 43, title);

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, 303);
    display.printf("< %s %u/%u >", pageName(current_page), unsigned(current_page + 1), unsigned(PAGE_COUNT));
}

static void drawHome()
{
    drawFrame("DIRECT LILYGO UI");
    lineAt(0, "Hardware path OK");
    lineAtf(1, "Touch: %s  Haptic: %s", touch_ok ? "OK" : "FAIL", haptic_ok ? "OK" : "FAIL");
    lineAtf(2, "Battery: %s", battery_ok ? "OK" : "NA");
    lineAtf(3, "GPS chars: %lu", static_cast<unsigned long>(gps.charsProcessed()));
    lineAtf(4, "Touches: %lu", static_cast<unsigned long>(touch_count));
    lineAtf(5, "Redraws: %lu", static_cast<unsigned long>(redraw_count));
    lineAt(7, "Tap edges or swipe.");
}

static void drawGps()
{
    drawFrame("GPS");
    lineAtf(0, "Chars: %lu", static_cast<unsigned long>(gps.charsProcessed()));
    lineAtf(1, "Sats: %u", gps.satellites.isValid() ? gps.satellites.value() : 0);
    lineAtf(2, "HDOP: %s", gps.hdop.isValid() ? String(gps.hdop.hdop(), 1).c_str() : "--");
    if (gps.location.isValid()) {
        lineAtf(3, "Lat: %.6f", gps.location.lat());
        lineAtf(4, "Lon: %.6f", gps.location.lng());
        lineAtf(5, "Age: %lums", static_cast<unsigned long>(gps.location.age()));
    } else {
        lineAt(3, "No fix yet");
        lineAt(4, "GPS rail is ON");
        lineAt(5, "Wait outside/window");
    }
    lineAtf(7, "Time: %02u:%02u:%02u",
            gps.time.isValid() ? gps.time.hour() : 0,
            gps.time.isValid() ? gps.time.minute() : 0,
            gps.time.isValid() ? gps.time.second() : 0);
}

static void drawWifi()
{
    drawFrame("WIFI");
    if (wifi_count == -2) {
        lineAt(0, "Tap center to scan");
        lineAt(2, "WiFi is off idle.");
        return;
    }
    if (wifi_count == -1) {
        lineAt(0, "Scanning...");
        return;
    }

    lineAtf(0, "Networks: %d", wifi_count);
    const int shown = wifi_count < 5 ? wifi_count : 5;
    for (int i = 0; i < shown; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() > 12) {
            ssid = ssid.substring(0, 12);
        }
        lineAtf(i + 1, "%d %s %ddBm", i + 1, ssid.c_str(), WiFi.RSSI(i));
    }
    if (wifi_count == 0) {
        lineAt(2, "No networks found");
    }
}

static void drawMessages()
{
    drawFrame("MESSAGES");
    lineAt(0, "XNODE shell active");
    lineAt(1, "Full bridge UI is off");
    lineAt(2, "on this panel.");
    lineAt(4, "Next step:");
    lineAt(5, "bring message list");
    lineAt(6, "onto this shell.");
}

static void drawBattery()
{
    drawFrame("BATTERY");
    if (!battery_ok) {
        lineAt(0, "BQ27220 not ready");
        return;
    }
    lineAtf(0, "Voltage: %umV", battery.getVoltage());
    lineAtf(1, "SOC: %u%%", battery.getStateOfCharge());
    lineAtf(2, "Charging: %s", battery.getIsCharging() ? "YES" : "NO");
    lineAtf(3, "I2C 0x55: %s", i2cPresent(BOARD_I2C_ADDR_BQ27220) ? "OK" : "MISS");
}

static char mapKey(int raw)
{
    static const char keymap[4][10] = {
        {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
        {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '0'},
        {'2', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '$', 'E'},
        {' ', ' ', ' ', ' ', ' ', '-', '*', 'S', '0', 'U'},
    };
    if (raw < 129 || raw > 163) {
        return '\0';
    }
    raw -= 129;
    const uint8_t row = raw / 10;
    const uint8_t col = 9 - (raw % 10);
    if (row >= 4 || col >= 10) {
        return '\0';
    }
    return keymap[row][col];
}

static void drawKeys()
{
    drawFrame("KEYBOARD");
    lineAtf(0, "TCA8418: %s", keyboard_ok ? "OK" : "FAIL");
    if (keyboard_last == '\0') {
        lineAt(2, "Press a key");
    } else if (keyboard_last == ' ') {
        lineAt(2, "Last key: SPACE");
    } else {
        lineAtf(2, "Last key: %c", keyboard_last);
    }
}

static void drawDiag()
{
    drawFrame("DIAG");
    lineAtf(0, "I2C devices: %u", i2c_found);
    lineAtf(1, "Touch 0x1A: %s", i2cPresent(BOARD_I2C_ADDR_TOUCH) ? "OK" : "MISS");
    lineAtf(2, "DRV 0x5A: %s", i2cPresent(BOARD_I2C_ADDR_DRV2605) ? "OK" : "MISS");
    lineAtf(3, "Keys 0x34: %s", i2cPresent(BOARD_I2C_ADDR_KEYBOARD) ? "OK" : "MISS");
    lineAtf(4, "BQ 0x55: %s", i2cPresent(BOARD_I2C_ADDR_BQ27220) ? "OK" : "MISS");
    lineAtf(5, "Touch IRQ: %d", digitalRead(BOARD_TOUCH_INT));
    lineAtf(6, "Heap: %lu", static_cast<unsigned long>(ESP.getFreeHeap()));
}

static void drawCurrentPage()
{
    ++redraw_count;
    Serial.printf("[XNODE-PRO] draw page=%s redraw=%lu\n",
                  pageName(current_page),
                  static_cast<unsigned long>(redraw_count));
    releaseSpiDevices();
    display.setFullWindow();
    display.firstPage();
    do {
        switch (current_page) {
            case PAGE_HOME: drawHome(); break;
            case PAGE_GPS: drawGps(); break;
            case PAGE_WIFI: drawWifi(); break;
            case PAGE_MESSAGES: drawMessages(); break;
            case PAGE_BATTERY: drawBattery(); break;
            case PAGE_KEYS: drawKeys(); break;
            case PAGE_DIAG: drawDiag(); break;
            default: drawHome(); break;
        }
    } while (display.nextPage());
    display.powerOff();
    redraw_requested = false;
    last_status_draw_ms = millis();
}

static void gotoPage(int delta)
{
    int next = static_cast<int>(current_page) + delta;
    while (next < 0) {
        next += PAGE_COUNT;
    }
    next %= PAGE_COUNT;
    current_page = static_cast<Page>(next);
    Serial.printf("[XNODE-PRO] page switch -> %s\n", pageName(current_page));
    pulse(47);
    redraw_requested = true;
}

static void scanWifi()
{
    Serial.println("[XNODE-PRO] WiFi scan start");
    wifi_count = -1;
    drawCurrentPage();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(100);
    wifi_count = WiFi.scanNetworks(false, true);
    Serial.printf("[XNODE-PRO] WiFi scan result=%d\n", wifi_count);
    WiFi.mode(WIFI_OFF);
    redraw_requested = true;
    pulse(47);
}

static void pageAction(int16_t x, int16_t y)
{
    if (y < 58) {
        if (x < 80) {
            gotoPage(-1);
        } else if (x > 160) {
            gotoPage(1);
        } else {
            redraw_requested = true;
            pulse(47);
        }
        return;
    }

    if (current_page == PAGE_WIFI) {
        scanWifi();
    } else if (current_page == PAGE_DIAG) {
        i2c_found = scanI2c();
        redraw_requested = true;
        pulse(47);
    } else {
        redraw_requested = true;
        pulse(47);
    }
}

static void initPins()
{
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)BOARD_6609_EN);
    gpio_hold_dis((gpio_num_t)BOARD_LORA_EN);
    gpio_hold_dis((gpio_num_t)BOARD_GPS_EN);
    gpio_hold_dis((gpio_num_t)BOARD_A7682E_PWRKEY);
    gpio_hold_dis((gpio_num_t)BOARD_EPD_BL);
    gpio_hold_dis((gpio_num_t)BOARD_MOTOR_PIN);

    pinMode(BOARD_KEYBOARD_LED, OUTPUT);
    pinMode(BOARD_MOTOR_PIN, OUTPUT);
    pinMode(BOARD_6609_EN, OUTPUT);
    pinMode(BOARD_LORA_EN, OUTPUT);
    pinMode(BOARD_GPS_EN, OUTPUT);
    pinMode(BOARD_A7682E_PWRKEY, OUTPUT);
    pinMode(BOARD_EPD_BL, OUTPUT);
    pinMode(BOARD_LORA_CS, OUTPUT);
    pinMode(BOARD_SD_CS, OUTPUT);
    pinMode(BOARD_EPD_CS, OUTPUT);
    pinMode(BOARD_TOUCH_INT, INPUT_PULLUP);
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);

    digitalWrite(BOARD_KEYBOARD_LED, LOW);
    digitalWrite(BOARD_MOTOR_PIN, HIGH);
    digitalWrite(BOARD_6609_EN, HIGH);
    digitalWrite(BOARD_LORA_EN, HIGH);
    digitalWrite(BOARD_GPS_EN, HIGH);
    digitalWrite(BOARD_A7682E_PWRKEY, HIGH);
    digitalWrite(BOARD_EPD_BL, HIGH);
    releaseSpiDevices();
}

static void setupHardware()
{
    initPins();

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire.setClock(400000);
    i2c_found = scanI2c();

    haptic_ok = drv.begin();
    if (haptic_ok) {
        drv.selectLibrary(1);
        drv.setMode(DRV2605_MODE_INTTRIG);
        Serial.println("[XNODE-PRO] DRV2605 OK");
    } else {
        Serial.println("[XNODE-PRO] DRV2605 FAIL");
    }
    pulse(1);
    delay(120);
    pulse(47);

    keyboard_ok = keyboard.begin(BOARD_I2C_ADDR_KEYBOARD, &Wire);
    if (keyboard_ok) {
        keyboard.matrix(4, 10);
        keyboard.flush();
    }
    Serial.printf("[XNODE-PRO] keyboard=%d\n", keyboard_ok ? 1 : 0);

    battery_ok = i2cPresent(BOARD_I2C_ADDR_BQ27220);
    if (battery_ok) {
        battery.init();
    }
    Serial.printf("[XNODE-PRO] battery=%d\n", battery_ok ? 1 : 0);

    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    display.epd2.selectSPI(SPI, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
    display.init(115200, true, 2, false);
    display.setRotation(0);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    touch.setPins(BOARD_TOUCH_RST, BOARD_TOUCH_INT);
    touch_ok = touch.begin(Wire, BOARD_I2C_ADDR_TOUCH, BOARD_I2C_SDA, BOARD_I2C_SCL);
    Serial.printf("[XNODE-PRO] touch=%d model=%s\n",
                  touch_ok ? 1 : 0,
                  touch_ok ? touch.getModelName() : "NONE");

    Serial2.begin(GPS_BAUD, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    WiFi.mode(WIFI_OFF);
}

static void pollGps()
{
    while (Serial2.available()) {
        gps.encode(static_cast<char>(Serial2.read()));
    }
}

static void pollKeyboard()
{
    if (!keyboard_ok || keyboard.available() == 0) {
        return;
    }
    const int raw = keyboard.getEvent();
    const char mapped = mapKey(raw);
    if (mapped != '\0') {
        keyboard_last = mapped;
        Serial.printf("[XNODE-PRO] key raw=%d mapped=%c\n", raw, mapped == ' ' ? '_' : mapped);
        pulse(47);
        if (mapped == 'a') {
            gotoPage(-1);
        } else if (mapped == 'l' || mapped == 'E') {
            gotoPage(1);
        } else {
            redraw_requested = true;
        }
    }
}

static void pollTouch()
{
    static bool tracking = false;
    static bool haptic_on_press = false;
    static int16_t start_x = 0;
    static int16_t start_y = 0;
    static int16_t last_x = 0;
    static int16_t last_y = 0;
    static uint32_t last_touch_ms = 0;

    int16_t x = 0;
    int16_t y = 0;
    const uint8_t touched = touch_ok ? readTouchFramePoint(&x, &y) : 0;
    const uint32_t now = millis();

    if (touched) {
        ++touch_count;
        last_touch_ms = now;
        last_x = x;
        last_y = y;
        if (!tracking) {
            start_x = x;
            start_y = y;
            tracking = true;
            haptic_on_press = false;
            Serial.printf("[XNODE-PRO] touch start %d,%d\n", x, y);
        }
        if (!haptic_on_press) {
            haptic_on_press = true;
            pulse(47);
        }
        return;
    }

    if (!tracking || now - last_touch_ms < 140) {
        return;
    }

    tracking = false;
    const int16_t dx = last_x - start_x;
    const int16_t dy = last_y - start_y;
    const int16_t abs_x = dx < 0 ? -dx : dx;
    const int16_t abs_y = dy < 0 ? -dy : dy;
    Serial.printf("[XNODE-PRO] touch release start=%d,%d end=%d,%d diff=%d,%d\n",
                  start_x, start_y, last_x, last_y, dx, dy);

    if (abs_x >= 45 && abs_x > abs_y) {
        gotoPage(dx < 0 ? 1 : -1);
    } else if (abs_y >= 70 && abs_y > abs_x) {
        redraw_requested = true;
        pulse(47);
    } else {
        pageAction(last_x, last_y);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(250);
    Serial.println();
    Serial.println("[XNODE-PRO] boot direct LilyGo-derived shell");
    setupHardware();
    redraw_requested = true;
}

void loop()
{
    static uint32_t last_alive_ms = 0;

    pollGps();
    pollKeyboard();
    pollTouch();

    if (millis() - last_alive_ms > 5000) {
        last_alive_ms = millis();
        Serial.printf("[XNODE-PRO] alive page=%s irq=%d touches=%lu\n",
                      pageName(current_page),
                      digitalRead(BOARD_TOUCH_INT),
                      static_cast<unsigned long>(touch_count));
    }

    if (current_page == PAGE_GPS && millis() - last_status_draw_ms > 10000) {
        redraw_requested = true;
    }

    if (redraw_requested) {
        drawCurrentPage();
    }

    delay(20);
}
