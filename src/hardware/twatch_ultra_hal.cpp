#include "twatch_ultra_hal.h"

#if defined(LILYGO_WATCH_ULTRA)

namespace {
    constexpr uint8_t XL9555_ADDR = 0x20;
    constexpr uint8_t XL9555_OUT0 = 0x02;
    constexpr uint8_t XL9555_OUT1 = 0x03;
    constexpr uint8_t XL9555_CFG0 = 0x06;
    constexpr uint8_t XL9555_CFG1 = 0x07;
    constexpr uint8_t CST226_STATUS_REG = 0x00;
    constexpr uint8_t CST226_SYNC = 0xAB;
    constexpr uint8_t CST226_SINGLE_POINT_READ_LEN = 8;
    constexpr uint8_t CST226_MAX_POINTS = 5;

    bool writeBytes(uint8_t addr, const uint8_t *data, size_t len) {
        Wire.beginTransmission(addr);
        Wire.write(data, len);
        return Wire.endTransmission() == 0;
    }

    bool readTouchStatus(uint8_t *data, size_t len) {
        Wire.beginTransmission(BOARD_TOUCH_ADDR);
        Wire.write(CST226_STATUS_REG);
        if (Wire.endTransmission(false) != 0) {
            return false;
        }

        const int read_len = Wire.requestFrom((int)BOARD_TOUCH_ADDR, (int)len);
        if (read_len != (int)len) {
            while (Wire.available()) {
                Wire.read();
            }
            return false;
        }

        for (size_t i = 0; i < len; i++) {
            data[i] = Wire.read();
        }
        return true;
    }

    void syncTouchStatus() {
        uint8_t sync[2] = { CST226_STATUS_REG, CST226_SYNC };
        writeBytes(BOARD_TOUCH_ADDR, sync, sizeof(sync));
    }
}

TWatchUltraHal watch;

const uint8_t *TWatchUltraPanel::getInitCommands(uint8_t listno) const {
    static constexpr uint8_t list0[] = {
        0xFE, 1, 0x00,
        0xC4, 1, 0x80,
        0x3A, 1, 0x55,
        0x35, 1, 0x00,
        0x53, 1, 0x20,
        0x63, 1, 0xFF,
        0x2A, 4, 0x00, 0x16, 0x01, 0xAF,
        0x2B, 4, 0x00, 0x00, 0x01, 0xF5,
        0x11, 0x80, 120,
        0x29, 0x80, 120,
        0x51, 1, 0x00,
        0xff, 0xff
    };
    return listno == 0 ? list0 : nullptr;
}

TWatchUltraDisplay::TWatchUltraDisplay() {
    auto bus_cfg = bus.config();
    bus_cfg.spi_host = SPI3_HOST;
    bus_cfg.spi_mode = SPI_MODE0;
    bus_cfg.freq_write = 45000000;
    bus_cfg.freq_read = 16000000;
    bus_cfg.spi_3wire = false;
    bus_cfg.use_lock = true;
    bus_cfg.dma_channel = SPI_DMA_CH_AUTO;
    bus_cfg.pin_sclk = DISP_SCK;
    bus_cfg.pin_io0 = DISP_D0;
    bus_cfg.pin_io1 = DISP_D1;
    bus_cfg.pin_io2 = DISP_D2;
    bus_cfg.pin_io3 = DISP_D3;
    bus.config(bus_cfg);
    panel.setBus(&bus);

    auto panel_cfg = panel.config();
    panel_cfg.pin_cs = DISP_CS;
    panel_cfg.pin_rst = DISP_RESET;
    panel_cfg.panel_width = TFT_WIDTH;
    panel_cfg.panel_height = TFT_HEIGHT;
    panel_cfg.offset_rotation = 0;
    panel_cfg.offset_x = 22;
    panel_cfg.offset_y = 0;
    panel_cfg.dummy_read_pixel = 1;
    panel_cfg.dummy_read_bits = 1;
    panel_cfg.readable = true;
    panel_cfg.invert = false;
    panel_cfg.rgb_order = false;
    panel_cfg.dlen_16bit = false;
    panel_cfg.bus_shared = true;
    panel_cfg.memory_width = TFT_WIDTH;
    panel_cfg.memory_height = TFT_HEIGHT;
    panel.config(panel_cfg);

    setPanel(&panel);
}

bool TWatchUltraHal::begin(Stream *stream) {
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire.setClock(400000);

    pinMode(BOARD_RADIO_SS, OUTPUT);
    digitalWrite(BOARD_RADIO_SS, HIGH);
    pinMode(DISP_CS, OUTPUT);
    digitalWrite(DISP_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(NFC_CS, OUTPUT);
    digitalWrite(NFC_CS, HIGH);

    const bool power_ok = beginPower(stream);
    expander_ready = beginExpander();

    beginRadioBus();

    pinMode(DISP_RESET, OUTPUT);
    digitalWrite(DISP_RESET, HIGH);
    delay(200);
    digitalWrite(DISP_RESET, LOW);
    delay(300);
    digitalWrite(DISP_RESET, HIGH);
    delay(200);

    display.init_without_reset(false);
    display.setRotation(rotation);
    display.setBrightness(0);
    display.fillScreen(TFT_BLACK);

    pinMode(BOARD_TOUCH_INT, INPUT_PULLUP);
    touch_ready = true;

    return power_ok;
}

bool TWatchUltraHal::beginPower(Stream *stream) {
    if (!power.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
        if (stream) {
            stream->println("T-Watch Ultra PMU init failed");
        }
        return false;
    }

    power.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
    power.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);
    power.setSysPowerDownVoltage(2600);

    power.setALDO1Voltage(3300);
    power.enableALDO1();
    power.setALDO2Voltage(3300);
    power.enableALDO2();
    power.setALDO3Voltage(3300);
    power.enableALDO3();
    power.setALDO4Voltage(1800);
    power.enableALDO4();
    power.setBLDO1Voltage(3300);
    power.enableBLDO1();
    power.setBLDO2Voltage(3300);
    power.enableBLDO2();

    power.disableDC2();
    power.disableDC3();
    power.disableDC4();
    power.disableDC5();
    power.disableCPUSLDO();

    power.disableTSPinMeasure();
    power.enableBattDetection();
    power.enableVbusVoltageMeasure();
    power.enableBattVoltageMeasure();
    power.enableSystemVoltageMeasure();
    power.enableTemperatureMeasure();
    power.setChargingLedMode(XPOWERS_CHG_LED_OFF);
    power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    power.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ |
        XPOWERS_AXP2101_BAT_REMOVE_IRQ |
        XPOWERS_AXP2101_PKEY_SHORT_IRQ |
        XPOWERS_AXP2101_PKEY_LONG_IRQ
    );
    power.clearIrqStatus();
    return true;
}

bool TWatchUltraHal::beginExpander() {
    uint8_t probe = 0;
    if (!expanderRead(0x00, probe)) {
        return false;
    }

    expanderPinMode(EXPANDS_DRV_EN, true);
    expanderDigitalWrite(EXPANDS_DRV_EN, true);
    delay(1);
    expanderPinMode(EXPANDS_DISP_EN, true);
    expanderDigitalWrite(EXPANDS_DISP_EN, true);
    delay(1);
    expanderPinMode(EXPANDS_TOUCH_RST, true);
    expanderDigitalWrite(EXPANDS_TOUCH_RST, false);
    delay(20);
    expanderDigitalWrite(EXPANDS_TOUCH_RST, true);
    delay(60);
    expanderPinMode(EXPANDS_LORA_RF_SW, true);
    expanderDigitalWrite(EXPANDS_LORA_RF_SW, true);
    expanderPinMode(EXPANDS_SD_DET, false);
    return true;
}

bool TWatchUltraHal::expanderRead(uint8_t reg, uint8_t &value) {
    Wire.beginTransmission(XL9555_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((int)XL9555_ADDR, 1) != 1) {
        return false;
    }
    value = Wire.read();
    return true;
}

bool TWatchUltraHal::expanderWrite(uint8_t reg, uint8_t value) {
    uint8_t data[2] = { reg, value };
    return writeBytes(XL9555_ADDR, data, sizeof(data));
}

bool TWatchUltraHal::expanderPinMode(uint8_t pin, bool output) {
    const uint8_t reg = pin < 8 ? XL9555_CFG0 : XL9555_CFG1;
    const uint8_t bit = pin & 0x07;
    uint8_t cfg = 0xff;
    if (!expanderRead(reg, cfg)) {
        return false;
    }
    if (output) {
        cfg &= ~(1 << bit);
    } else {
        cfg |= (1 << bit);
    }
    return expanderWrite(reg, cfg);
}

bool TWatchUltraHal::expanderDigitalWrite(uint8_t pin, bool high) {
    const uint8_t reg = pin < 8 ? XL9555_OUT0 : XL9555_OUT1;
    const uint8_t bit = pin & 0x07;
    uint8_t value = 0x00;
    if (!expanderRead(reg, value)) {
        return false;
    }
    if (high) {
        value |= (1 << bit);
    } else {
        value &= ~(1 << bit);
    }
    return expanderWrite(reg, value);
}

void TWatchUltraHal::beginRadioBus() {
    SPI.begin(BOARD_RADIO_SCK, BOARD_RADIO_MISO, BOARD_RADIO_MOSI);
}

void TWatchUltraHal::setSwapBytes(bool swap) {
    display.setSwapBytes(swap);
}

void TWatchUltraHal::fillScreen(uint32_t color) {
    display.fillScreen(color);
}

void TWatchUltraHal::startWrite() {
    display.startWrite();
}

void TWatchUltraHal::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
    display.setAddrWindow(x, y, w, h);
}

void TWatchUltraHal::pushColors(const uint16_t *data, int32_t len, bool swap) {
    display.writePixels(data, len, swap);
}

void TWatchUltraHal::endWrite() {
    display.endWrite();
}

void TWatchUltraHal::setRotation(uint8_t next_rotation) {
    rotation = next_rotation;
    display.setRotation(rotation);
}

uint8_t TWatchUltraHal::getRotation() const {
    return rotation;
}

void TWatchUltraHal::setBrightness(uint8_t level) {
    display.setBrightness(level);
}

bool TWatchUltraHal::getTouched() {
    return touch_ready && digitalRead(BOARD_TOUCH_INT) == LOW;
}

uint8_t TWatchUltraHal::getPoint(int16_t *x, int16_t *y) {
    if (!touch_ready || !x || !y) {
        return 0;
    }

    uint8_t read_buffer[CST226_SINGLE_POINT_READ_LEN] = { 0 };
    if (!readTouchStatus(read_buffer, sizeof(read_buffer))) {
        return 0;
    }

    if (read_buffer[6] != CST226_SYNC || read_buffer[0] == CST226_SYNC || read_buffer[5] == 0x80) {
        return 0;
    }

    const uint8_t points = read_buffer[5] & 0x7f;
    if (points == 0 || points > CST226_MAX_POINTS) {
        syncTouchStatus();
        return 0;
    }

    *x = static_cast<int16_t>((read_buffer[1] << 4) | ((read_buffer[3] >> 4) & 0x0f));
    *y = static_cast<int16_t>((read_buffer[2] << 4) | (read_buffer[3] & 0x0f));
    return points;
}

void TWatchUltraHal::interruptTrigger() {
    if (!touch_ready || digitalRead(BOARD_TOUCH_INT) != LOW) {
        return;
    }

    int16_t x = 0;
    int16_t y = 0;
    getPoint(&x, &y);
}

void TWatchUltraHal::setMonitorTime(uint8_t time) {
    (void)time;
}

void TWatchUltraHal::powerIoctl(PowerCtrlChannel ch, bool enable) {
    switch (ch) {
        case WATCH_POWER_DISPLAY_BL:
            enable ? power.enableALDO2() : power.disableALDO2();
            break;
        case WATCH_POWER_TOUCH_DISP:
            enable ? power.enableALDO2() : power.disableALDO2();
            break;
        case WATCH_POWER_RADIO:
            enable ? power.enableALDO3() : power.disableALDO3();
            break;
        case WATCH_POWER_DRV2605:
            enable ? power.enableBLDO2() : power.disableBLDO2();
            break;
        case WATCH_POWER_GPS:
            if ( enable ) {
                power.enableBLDO1();
                Serial1.begin(BOARD_GPS_BAUDRATE, SERIAL_8N1, SHIELD_GPS_RX, SHIELD_GPS_TX);
                pinMode(BOARD_GPS_1PPS, INPUT);
            }
            else {
                Serial1.end();
                power.disableBLDO1();
                pinMode(SHIELD_GPS_RX, INPUT);
                pinMode(SHIELD_GPS_TX, INPUT);
                pinMode(BOARD_GPS_1PPS, INPUT);
            }
            break;
        case WATCH_POWER_GPS_DC_CHANNEL:
        default:
            return;
    }
}

uint64_t TWatchUltraHal::readPMU() {
    return power.getIrqStatus();
}

void TWatchUltraHal::clearPMU() {
    power.clearIrqStatus();
}

bool TWatchUltraHal::isVbusIn() {
    return power.isVbusIn();
}

bool TWatchUltraHal::isCharging() {
    return power.isCharging();
}

bool TWatchUltraHal::isBatteryConnect() {
    return power.isBatteryConnect();
}

bool TWatchUltraHal::isVbusInsertIrq() {
    return power.isVbusInsertIrq();
}

bool TWatchUltraHal::isVbusRemoveIrq() {
    return power.isVbusRemoveIrq();
}

bool TWatchUltraHal::isBatChagerStartIrq() {
    return power.isBatChagerStartIrq();
}

bool TWatchUltraHal::isBatChagerDoneIrq() {
    return power.isBatChagerDoneIrq();
}

bool TWatchUltraHal::isBatInsertIrq() {
    return power.isBatInsertIrq();
}

bool TWatchUltraHal::isBatRemoveIrq() {
    return power.isBatRemoveIrq();
}

bool TWatchUltraHal::isPekeyShortPressIrq() {
    return power.isPekeyShortPressIrq();
}

bool TWatchUltraHal::isPekeyLongPressIrq() {
    return power.isPekeyLongPressIrq();
}

int TWatchUltraHal::getBatteryPercent() {
    return power.getBatteryPercent();
}

uint16_t TWatchUltraHal::getBattVoltage() {
    return power.getBattVoltage();
}

uint16_t TWatchUltraHal::getVbusVoltage() {
    return power.getVbusVoltage();
}

void TWatchUltraHal::setChargeTargetVoltage(uint8_t opt) {
    power.setChargeTargetVoltage(opt);
}

void TWatchUltraHal::setDC3Voltage(uint16_t millivolt) {
    power.setDC3Voltage(millivolt);
}

void TWatchUltraHal::shutdown() {
    power.shutdown();
}

void TWatchUltraHal::run() {
}

#endif
