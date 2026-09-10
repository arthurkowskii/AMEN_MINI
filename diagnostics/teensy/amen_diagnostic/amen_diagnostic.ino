#include <Arduino.h>
#include <Entropy.h>
#include <IntervalTimer.h>
#include <Wire.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

constexpr char PROFILE[] = "amen-mini-pcb-9cd419cd-v1";
constexpr uint8_t ROWS[] = {5, 6, 9, 14, 15};
constexpr uint8_t COLS[] = {0, 1, 2, 3, 4};
constexpr uint8_t ENCODER_A[] = {16, 22, 25, 27, 29, 31, 33};
constexpr uint8_t ENCODER_B[] = {17, 24, 26, 28, 30, 32, 34};
constexpr uint8_t PUSH[] = {35, 36, 37, 38, 39, 40, 41};
constexpr int8_t QUADRATURE[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
constexpr uint32_t SCAN_US = 500;
constexpr uint32_t DEBOUNCE_US = 5000;

struct Readings {
    bool contacts[28];
    bool raw[28];
    uint32_t edges[28];
    uint32_t changes[28];
    int32_t encoders[7];
    uint32_t positive[7];
    uint32_t negative[7];
    uint32_t invalid[7];
    uint32_t a_edges[7];
    uint32_t b_edges[7];
    uint8_t ab[7];
    uint32_t scans;
    uint32_t max_scan_gap_us;
    bool overflow;
};

volatile Readings readings = {};
Readings frame = {};
IntervalTimer scan_timer;
uint32_t changed_at[28] = {};
int8_t partial[7] = {};
uint32_t last_scan = 0;
volatile bool settled = false;
bool first_scan = true;
bool timer_ok = false;
bool boot_ready = false;
bool oled_ok = false;
uint8_t oled_address = 0;
char boot_id[17] = {};
char tx[4096];
size_t tx_size = 0;
size_t tx_sent = 0;
uint32_t tx_started = 0;
bool tx_failed = false;
char rx[160];
size_t rx_size = 0;
bool rx_discard = false;
uint32_t rx_started = 0;
uint32_t sequence = 0;
uint32_t last_state = 0;
bool force_state = false;
char command[16];
char pattern[16];
int32_t command_id = -1;
bool have_id = false;

void increment(volatile uint32_t &value) {
    if (value == UINT32_MAX) readings.overflow = true;
    else ++value;
}

void scan_inputs() {
    const uint32_t now = micros();
    const uint32_t gap = now - last_scan;
    if (!first_scan) {
        if (gap > readings.max_scan_gap_us) readings.max_scan_gap_us = gap;
        if (gap > SCAN_US + SCAN_US / 2) readings.overflow = true;
    }
    last_scan = now;
    bool sample[28];
    for (uint8_t row = 0; row < 5; ++row) {
        digitalWrite(ROWS[row], LOW);
        pinMode(ROWS[row], OUTPUT);
        delayMicroseconds(3);
        for (uint8_t col = 0; col < 4; ++col) sample[(4 - row) * 4 + col] = !digitalRead(COLS[col]);
        if (row == 4) sample[20] = !digitalRead(COLS[4]);
        pinMode(ROWS[row], INPUT);
    }
    for (uint8_t i = 0; i < 7; ++i) {
        sample[21 + i] = !digitalRead(PUSH[i]);
        const uint8_t ab = (digitalRead(ENCODER_A[i]) << 1) | digitalRead(ENCODER_B[i]);
        const uint8_t before = readings.ab[i];
        if (!first_scan && ab != before) {
            if ((ab ^ before) & 2) increment(readings.a_edges[i]);
            if ((ab ^ before) & 1) increment(readings.b_edges[i]);
            if ((ab ^ before) == 3) {
                increment(readings.invalid[i]);
                partial[i] = 0;
            } else {
                partial[i] += QUADRATURE[before * 4 + ab];
                if (partial[i] == 4 || partial[i] == -4) {
                    const int8_t sign = partial[i] > 0 ? 1 : -1;
                    if (sign > 0) increment(readings.positive[i]);
                    else increment(readings.negative[i]);
                    if ((sign > 0 && readings.encoders[i] == INT32_MAX) ||
                        (sign < 0 && readings.encoders[i] == INT32_MIN)) readings.overflow = true;
                    else readings.encoders[i] += sign;
                    partial[i] = 0;
                }
            }
        }
        readings.ab[i] = ab;
    }
    for (uint8_t i = 0; i < 28; ++i) {
        if (first_scan) {
            readings.raw[i] = readings.contacts[i] = sample[i];
            changed_at[i] = now;
        } else if (sample[i] != readings.raw[i]) {
            readings.raw[i] = sample[i];
            changed_at[i] = now;
            increment(readings.edges[i]);
        }
        if (readings.contacts[i] != readings.raw[i] && now - changed_at[i] >= DEBOUNCE_US) {
            readings.contacts[i] = readings.raw[i];
            increment(readings.changes[i]);
        }
    }
    first_scan = false;
    increment(readings.scans);
    bool stable = readings.scans >= 40;
    for (uint8_t i = 0; i < 28; ++i) stable = stable && now - changed_at[i] >= DEBOUNCE_US;
    if (stable) settled = true;
    if (micros() - now >= SCAN_US) readings.overflow = true;
}

void set_overflow() {
    noInterrupts();
    readings.overflow = true;
    interrupts();
}

void reset_counts() {
    noInterrupts();
    for (uint8_t i = 0; i < 28; ++i) {
        readings.edges[i] = 0;
        readings.changes[i] = 0;
    }
    for (uint8_t i = 0; i < 7; ++i) {
        readings.encoders[i] = 0;
        readings.positive[i] = readings.negative[i] = readings.invalid[i] = 0;
        readings.a_edges[i] = readings.b_edges[i] = 0;
        partial[i] = 0;
    }
    readings.scans = 0;
    readings.max_scan_gap_us = 0;
    readings.overflow = !timer_ok;
    interrupts();
    sequence = 0;
}

void append(const char *format, ...) {
    if (tx_failed) return;
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(tx + tx_size, sizeof(tx) - tx_size, format, args);
    va_end(args);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(tx) - tx_size) {
        tx_failed = true;
        set_overflow();
    } else tx_size += count;
}

void begin_frame() {
    tx_size = tx_sent = 0;
    tx_failed = false;
    tx_started = millis();
}

void finish_frame() {
    if (tx_failed) tx_size = 0;
}

void boolean_array(const char *name, const bool *values, size_t count) {
    append(",\"%s\":[", name);
    for (size_t i = 0; i < count; ++i) append("%s%s", i ? "," : "", values[i] ? "true" : "false");
    append("]");
}

void counter_array(const char *name, const uint32_t *values, size_t count) {
    append(",\"%s\":[", name);
    for (size_t i = 0; i < count; ++i) append("%s%lu", i ? "," : "", static_cast<unsigned long>(values[i]));
    append("]");
}

void state_frame() {
    uint32_t timestamp;
    noInterrupts();
    memcpy(&frame, const_cast<const Readings *>(&readings), sizeof(frame));
    timestamp = millis();
    interrupts();
    begin_frame();
    append("{\"type\":\"state\",\"seq\":%lu,\"ms\":%lu", static_cast<unsigned long>(sequence), static_cast<unsigned long>(timestamp));
    boolean_array("contacts", frame.contacts, 28);
    boolean_array("raw", frame.raw, 28);
    counter_array("edges", frame.edges, 28);
    counter_array("changes", frame.changes, 28);
    append(",\"encoders\":[");
    for (uint8_t i = 0; i < 7; ++i) append("%s%ld", i ? "," : "", static_cast<long>(frame.encoders[i]));
    append("]");
    counter_array("positive", frame.positive, 7);
    counter_array("negative", frame.negative, 7);
    counter_array("invalid", frame.invalid, 7);
    counter_array("a_edges", frame.a_edges, 7);
    counter_array("b_edges", frame.b_edges, 7);
    append(",\"ab\":[");
    for (uint8_t i = 0; i < 7; ++i) append("%s%u", i ? "," : "", frame.ab[i]);
    append("],\"oled\":%s,\"overflow\":%s,\"ready\":%s,\"scans\":%lu,\"max_scan_gap_us\":%lu}\n",
           oled_ok ? "true" : "false", frame.overflow ? "true" : "false", settled && timer_ok && boot_ready ? "true" : "false",
           static_cast<unsigned long>(frame.scans), static_cast<unsigned long>(frame.max_scan_gap_us));
    if (!tx_failed) ++sequence;
    finish_frame();
    last_state = millis();
    force_state = false;
}

void drain_tx() {
    if (tx_sent == tx_size) return;
    if (millis() - tx_started > 250) set_overflow();
    if (!Serial) return;
    const int available = Serial.availableForWrite();
    if (available <= 0) return;
    size_t count = tx_size - tx_sent;
    if (count > static_cast<size_t>(available)) count = available;
    if (count > 64) count = 64;
    tx_sent += Serial.write(reinterpret_cast<const uint8_t *>(tx) + tx_sent, count);
}

bool oled_write(uint8_t control, const uint8_t *data, size_t count) {
    Wire.beginTransmission(oled_address);
    Wire.write(control);
    const bool complete = Wire.write(data, count) == count;
    const uint8_t status = Wire.endTransmission();
    return complete && status == 0;
}

bool initialize_oled() {
    oled_address = 0;
    for (uint8_t address = 0x3c; address <= 0x3d; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            oled_address = address;
            break;
        }
    }
    if (!oled_address) return false;
    const uint8_t init[] = {0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
                            0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x02,
                            0x81, 0x8f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf};
    return oled_write(0x00, init, sizeof(init));
}

bool valid_pattern() {
    return !strcmp(pattern, "black") || !strcmp(pattern, "white") || !strcmp(pattern, "checker") ||
           !strcmp(pattern, "inverse") || !strcmp(pattern, "border") || !strcmp(pattern, "rows") || !strcmp(pattern, "columns");
}

bool display_pattern() {
    if (!oled_ok && !initialize_oled()) return false;
    const uint8_t window[] = {0x21, 0, 127, 0x22, 0, 3};
    if (!oled_write(0x00, window, sizeof(window))) return false;
    for (uint8_t page = 0; page < 4; ++page) {
        for (uint8_t block = 0; block < 8; ++block) {
            uint8_t data[16] = {};
            for (uint8_t i = 0; i < 16; ++i) {
                const uint8_t x = block * 16 + i;
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    const uint8_t y = page * 8 + bit;
                    bool on = false;
                    if (!strcmp(pattern, "white")) on = true;
                    else if (!strcmp(pattern, "checker")) on = ((x / 4 + y / 4) & 1) == 0;
                    else if (!strcmp(pattern, "inverse")) on = ((x / 4 + y / 4) & 1) != 0;
                    else if (!strcmp(pattern, "border")) on = x == 0 || x == 127 || y == 0 || y == 31;
                    else if (!strcmp(pattern, "rows")) on = (y & 1) == 0;
                    else if (!strcmp(pattern, "columns")) on = (x & 1) == 0;
                    if (on) data[i] |= 1 << bit;
                }
            }
            if (!oled_write(0x40, data, sizeof(data))) return false;
        }
    }
    return true;
}

void whitespace(const char *&p) {
    while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
}

bool read_string(const char *&p, char *out, size_t capacity) {
    if (*p++ != '"') return false;
    size_t length = 0;
    while (*p && *p != '"') {
        if (static_cast<unsigned char>(*p) < 32 || *p == '\\' || length + 1 >= capacity) return false;
        out[length++] = *p++;
    }
    if (*p != '"') return false;
    ++p;
    out[length] = 0;
    return true;
}

bool parse_command() {
    command[0] = pattern[0] = 0;
    command_id = -1;
    have_id = false;
    bool have_command = false;
    bool have_pattern = false;
    const char *p = rx;
    whitespace(p);
    if (*p++ != '{') return false;
    for (uint8_t fields = 0; fields < 3; ++fields) {
        char key[16];
        whitespace(p);
        if (!read_string(p, key, sizeof(key))) return false;
        whitespace(p);
        if (*p++ != ':') return false;
        whitespace(p);
        if (!strcmp(key, "cmd") && !have_command) {
            if (!read_string(p, command, sizeof(command))) return false;
            have_command = true;
        } else if (!strcmp(key, "pattern") && !have_pattern) {
            if (!read_string(p, pattern, sizeof(pattern))) return false;
            have_pattern = true;
        } else if (!strcmp(key, "id") && !have_id) {
            bool negative = *p == '-';
            if (negative) ++p;
            if (*p < '0' || *p > '9') return false;
            if (*p == '0' && p[1] >= '0' && p[1] <= '9') return false;
            uint32_t value = 0;
            const uint32_t maximum = negative ? 2147483648UL : 2147483647UL;
            while (*p >= '0' && *p <= '9') {
                const uint8_t digit = *p++ - '0';
                if (value > (maximum - digit) / 10) return false;
                value = value * 10 + digit;
            }
            command_id = negative ? static_cast<int32_t>(-static_cast<int64_t>(value)) : value;
            have_id = true;
        } else return false;
        whitespace(p);
        if (*p == '}') {
            ++p;
            whitespace(p);
            return !*p && have_id && have_command && (have_pattern == !strcmp(command, "pattern"));
        }
        if (*p++ != ',') return false;
    }
    return false;
}

void ack(bool ok, const char *error) {
    begin_frame();
    append("{\"type\":\"ack\",\"id\":%ld,\"ok\":%s", static_cast<long>(have_id ? command_id : -1), ok ? "true" : "false");
    if (error) append(",\"error\":\"%s\"", error);
    append(",\"oled\":%s}\n", oled_ok ? "true" : "false");
    finish_frame();
}

void execute_command() {
    if (!parse_command()) {
        ack(false, "malformed_command");
    } else if (!strcmp(command, "hello")) {
        begin_frame();
        append("{\"type\":\"hello\",\"id\":%ld,\"protocol\":1,\"firmware\":\"1.0.0\",\"profile\":\"%s\",\"boot\":\"%s\",\"ready\":%s,\"scan_hz\":2000}\n",
               static_cast<long>(command_id), PROFILE, boot_id, settled && timer_ok && boot_ready ? "true" : "false");
        finish_frame();
    } else if (!strcmp(command, "start")) {
        if (!settled || !timer_ok || !boot_ready) ack(false, "scan_or_boot_not_ready");
        else {
            reset_counts();
            ack(true, nullptr);
            force_state = true;
        }
    } else if (!strcmp(command, "snapshot")) {
        ack(true, nullptr);
        force_state = true;
    } else if (!strcmp(command, "pattern")) {
        if (!valid_pattern()) ack(false, "unknown_pattern");
        else {
            oled_ok = display_pattern();
            ack(oled_ok, oled_ok ? nullptr : "oled_i2c_failed");
        }
    } else ack(false, "unknown_command");
}

void receive_command() {
    if ((rx_size || rx_discard) && millis() - rx_started > 1000) {
        rx_discard = true;
        set_overflow();
    }
    if (tx_sent != tx_size) return;
    for (uint8_t budget = 0; budget < 64 && Serial.available(); ++budget) {
        const int value = Serial.read();
        if (value < 0) return;
        const char c = value;
        if (!rx_size && !rx_discard) rx_started = millis();
        if (c == '\n') {
            if (rx_discard) {
                have_id = false;
                ack(false, "rx_overflow_or_timeout");
            } else {
                rx[rx_size] = 0;
                execute_command();
            }
            rx_size = 0;
            rx_discard = false;
            return;
        }
        if (rx_discard) continue;
        if (!c || rx_size + 1 >= sizeof(rx)) {
            rx_discard = true;
            set_overflow();
        } else rx[rx_size++] = c;
    }
}

void setup() {
    Serial.begin(115200);
    for (uint8_t pin : ROWS) {
        pinMode(pin, INPUT);
        digitalWrite(pin, LOW);
    }
    for (uint8_t pin : COLS) pinMode(pin, INPUT_PULLUP);
    for (uint8_t i = 0; i < 7; ++i) {
        pinMode(ENCODER_A[i], INPUT_PULLUP);
        pinMode(ENCODER_B[i], INPUT_PULLUP);
        pinMode(PUSH[i], INPUT_PULLUP);
    }
    delayMicroseconds(20);
    timer_ok = scan_timer.begin(scan_inputs, SCAN_US);
    scan_timer.priority(64);
    if (!timer_ok) set_overflow();
    Entropy.Initialize();
    const uint32_t entropy_started = millis();
    while (Entropy.available() < 2 && millis() - entropy_started < 100) yield();
    boot_ready = Entropy.available() >= 2;
    if (boot_ready) {
        const uint32_t high = Entropy.random();
        const uint32_t low = Entropy.random();
        snprintf(boot_id, sizeof(boot_id), "%08lx%08lx", static_cast<unsigned long>(high), static_cast<unsigned long>(low));
    }
    Wire.begin();
    Wire.setClock(400000);
    oled_ok = initialize_oled();
    strcpy(pattern, "black");
    if (oled_ok) oled_ok = display_pattern();
}

void loop() {
    drain_tx();
    receive_command();
    if (tx_sent == tx_size && Serial && (force_state || millis() - last_state >= 50)) state_frame();
}
