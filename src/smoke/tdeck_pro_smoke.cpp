#if defined(TDECK_PRO_SMOKE)

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Adafruit_DRV2605.h>
#include "driver/gpio.h"

extern "C" {
#include "hyn_core.h"
}

int hyn_touch_init(void);
uint8_t hyn_touch_get_point(int16_t *x_array, int16_t *y_array, uint8_t get_point);

#define BOARD_I2C_ADDR_TOUCH 0x1A
#define BOARD_I2C_ADDR_KEYBOARD 0x34
#define BOARD_I2C_ADDR_BQ27220 0x55
#define BOARD_I2C_ADDR_DRV2605 0x5A
#define BOARD_I2C_ADDR_BQ25896 0x6B

#define BOARD_I2C_SDA 13
#define BOARD_I2C_SCL 14
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
#define BOARD_GPS_EN 39
#define BOARD_6609_EN 41
#define BOARD_A7682E_PWRKEY 40
#define BOARD_MOTOR_PIN 2

static constexpr uint32_t EPD_SPI_HZ = 2000000;

using InkPanel = GxEPD2_310_GDEQ031T10;
using InkDisplay = GxEPD2_BW<InkPanel, InkPanel::HEIGHT>;

static InkDisplay display(InkPanel(BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST, BOARD_EPD_BUSY));
static Adafruit_DRV2605 drv;

static bool haptic_ok = false;
static bool touch_ok = false;
static uint32_t last_haptic_ms = 0;
static uint32_t last_serial_ms = 0;
static uint32_t last_draw_ms = 0;
static uint32_t touch_count = 0;
static int16_t last_x = -1;
static int16_t last_y = -1;

static void release_spi_devices()
{
    digitalWrite(BOARD_LORA_CS, HIGH);
    digitalWrite(BOARD_SD_CS, HIGH);
    digitalWrite(BOARD_EPD_CS, HIGH);
}

static void play_haptic(uint8_t effect)
{
    if (!haptic_ok) {
        return;
    }
    drv.setWaveform(0, effect);
    drv.setWaveform(1, 0);
    drv.go();
}

static void boot_haptic()
{
    play_haptic(1);
    delay(150);
    play_haptic(47);
    delay(150);
    play_haptic(1);
}

static void scan_i2c()
{
    Serial.println("[SMOKE] I2C scan start");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[SMOKE] I2C device 0x%02X\n", addr);
            ++found;
        }
    }
    Serial.printf("[SMOKE] I2C scan done, found=%u\n", found);
}

static bool i2c_present(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void draw_status(const char *line1, const char *line2, int16_t x = -1, int16_t y = -1)
{
    release_spi_devices();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.fillRect(0, 0, display.width(), 54, GxEPD_BLACK);
        display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
        display.drawRect(4, 58, display.width() - 8, display.height() - 64, GxEPD_BLACK);

        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(10, 24);
        display.print("XNODE TDECK PRO");
        display.setCursor(10, 46);
        display.print("LILYGO SMOKE");

        display.setTextColor(GxEPD_BLACK);
        display.setCursor(12, 92);
        display.print(line1);
        display.setCursor(12, 118);
        display.print(line2);

        display.setCursor(12, 154);
        display.printf("DRV2605: %s", haptic_ok ? "OK" : "FAIL");
        display.setCursor(12, 180);
        display.printf("HYN TOUCH: %s", touch_ok ? "OK" : "FAIL");
        display.setCursor(12, 206);
        display.printf("TOUCHES: %lu", static_cast<unsigned long>(touch_count));

        if (x >= 0 && y >= 0) {
            display.setCursor(12, 238);
            display.printf("X=%d Y=%d", x, y);
            display.fillCircle(constrain(x, 6, 233), constrain(y, 60, 313), 6, GxEPD_BLACK);
            display.drawFastHLine(max(0, x - 20), constrain(y, 60, 313), 40, GxEPD_BLACK);
            display.drawFastVLine(constrain(x, 6, 233), max(60, y - 20), 40, GxEPD_BLACK);
        } else {
            display.setCursor(12, 238);
            display.print("Tap screen to redraw");
        }

        display.setCursor(12, 286);
        display.print("Haptic every 5 sec");
    } while (display.nextPage());
    display.powerOff();
}

static void init_board_pins()
{
    gpio_deep_sleep_hold_dis();
    const gpio_num_t hold_pins[] = {
        static_cast<gpio_num_t>(BOARD_6609_EN),
        static_cast<gpio_num_t>(BOARD_LORA_EN),
        static_cast<gpio_num_t>(BOARD_GPS_EN),
        static_cast<gpio_num_t>(BOARD_A7682E_PWRKEY),
        static_cast<gpio_num_t>(BOARD_EPD_BL),
        static_cast<gpio_num_t>(BOARD_MOTOR_PIN),
    };
    for (gpio_num_t pin : hold_pins) {
        gpio_hold_dis(pin);
    }

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

    digitalWrite(BOARD_KEYBOARD_LED, LOW);
    digitalWrite(BOARD_MOTOR_PIN, HIGH);
    digitalWrite(BOARD_6609_EN, HIGH);
    digitalWrite(BOARD_LORA_EN, HIGH);
    digitalWrite(BOARD_GPS_EN, HIGH);
    digitalWrite(BOARD_A7682E_PWRKEY, HIGH);
    digitalWrite(BOARD_EPD_BL, HIGH);
    release_spi_devices();
}

void setup()
{
    Serial.begin(115200);
    delay(250);
    Serial.println();
    Serial.println("[SMOKE] boot: T-Deck Pro LilyGo-derived hardware smoke test");
    Serial.printf("[SMOKE] pins: EPD_RST=%d EPD_BL=%d TOUCH_RST=%d TOUCH_INT=%d DRV=0x%02X\n",
                  BOARD_EPD_RST, BOARD_EPD_BL, BOARD_TOUCH_RST, BOARD_TOUCH_INT, BOARD_I2C_ADDR_DRV2605);

    init_board_pins();

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire.setClock(400000);
    scan_i2c();

    Serial.printf("[SMOKE] expected touch 0x%02X: %s\n",
                  BOARD_I2C_ADDR_TOUCH, i2c_present(BOARD_I2C_ADDR_TOUCH) ? "present" : "missing");
    Serial.printf("[SMOKE] expected haptic 0x%02X: %s\n",
                  BOARD_I2C_ADDR_DRV2605, i2c_present(BOARD_I2C_ADDR_DRV2605) ? "present" : "missing");

    haptic_ok = drv.begin();
    if (haptic_ok) {
        drv.selectLibrary(1);
        drv.setMode(DRV2605_MODE_INTTRIG);
        Serial.println("[SMOKE] DRV2605 init OK");
    } else {
        Serial.println("[SMOKE] DRV2605 init FAIL");
    }
    boot_haptic();

    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    release_spi_devices();
    display.epd2.selectSPI(SPI, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
    Serial.println("[SMOKE] EPD init start");
    display.init(115200, true, 2, false);
    display.setRotation(0);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    Serial.println("[SMOKE] EPD init done, drawing boot page");
    draw_status("EPD FULL REFRESH", "INITIALIZING TOUCH");

    touch_ok = hyn_touch_init();
    Serial.printf("[SMOKE] HYN touch init %s\n", touch_ok ? "OK" : "FAIL");
    draw_status("EPD OK", "TOUCH/HAPTIC READY");
    play_haptic(47);

    last_haptic_ms = millis();
    last_serial_ms = millis();
    last_draw_ms = millis();
}

void loop()
{
    const uint32_t now = millis();

    if (haptic_ok && now - last_haptic_ms >= 5000) {
        last_haptic_ms = now;
        Serial.println("[SMOKE] haptic heartbeat");
        play_haptic(1);
    }

    int16_t x = 0;
    int16_t y = 0;
    uint8_t touched = hyn_touch_get_point(&x, &y, 1);
    if (touched) {
        last_x = x;
        last_y = y;
        ++touch_count;
        Serial.printf("[SMOKE] touch #%lu x=%d y=%d\n",
                      static_cast<unsigned long>(touch_count), x, y);
        play_haptic(47);
        if (now - last_draw_ms >= 900) {
            last_draw_ms = now;
            draw_status("TOUCH EVENT", "DISPLAY REDRAW OK", x, y);
        }
    }

    if (now - last_serial_ms >= 2000) {
        last_serial_ms = now;
        Serial.printf("[SMOKE] alive haptic=%d touch=%d last=(%d,%d) count=%lu\n",
                      haptic_ok ? 1 : 0,
                      touch_ok ? 1 : 0,
                      last_x,
                      last_y,
                      static_cast<unsigned long>(touch_count));
    }

    delay(20);
}

#endif
