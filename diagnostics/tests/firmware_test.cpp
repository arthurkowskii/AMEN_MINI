#include <algorithm>
#include <cassert>
#include <iostream>
#include "../teensy/amen_diagnostic/amen_diagnostic.ino"

void tick() {
    clock_us = last_scan + 500;
    scan_inputs();
}

void ticks(int count) {
    while (count--) tick();
}

void ab_input(int value) {
    levels[ENCODER_A[0]] = value >> 1;
    levels[ENCODER_B[0]] = value & 1;
    tick();
}

void request(const char *text) {
    strcpy(rx, text);
    execute_command();
}

void test_contacts() {
    for (int i = 0; i < 21; ++i) {
        const int row = i == 20 ? 4 : 4 - i / 4;
        const int col = i == 20 ? 4 : i % 4;
        switches[row][col] = true;
        ticks(12);
        for (int j = 0; j < 28; ++j) assert(readings.contacts[j] == (j == i));
        switches[row][col] = false;
        ticks(12);
        assert(readings.changes[i] == 2 && readings.edges[i] == 2);
    }
    for (int i = 0; i < 7; ++i) {
        levels[PUSH[i]] = 0;
        ticks(12);
        assert(readings.contacts[21 + i]);
        levels[PUSH[i]] = 1;
        ticks(12);
        assert(readings.changes[21 + i] == 2);
    }
    reset_counts();
    switches[4][0] = true;
    ticks(3);
    switches[4][0] = false;
    ticks(12);
    assert(readings.edges[0] == 2 && readings.changes[0] == 0);
}

void test_rotation() {
    for (int ab : {1, 0, 2, 3}) ab_input(ab);
    assert(readings.positive[0] + readings.negative[0] == 1);
    for (int ab : {2, 0, 1, 3}) ab_input(ab);
    assert(readings.encoders[0] == 0 && readings.positive[0] == 1 && readings.negative[0] == 1);
    assert(readings.a_edges[0] == 4 && readings.b_edges[0] == 4);
    ab_input(0);
    assert(readings.invalid[0] == 1);
}

void test_protocol() {
    reset_counts();
    request("{\"id\":2,\"cmd\":\"snapshot\"}");
    assert(strstr(tx, "\"ok\":true") && force_state);
    state_frame();
    assert(strstr(tx, "\"seq\":0") && strstr(tx, "\"contacts\":[false") && strstr(tx, "\"ready\":true"));
    const char *bad[] = {"", "{", "{\"id\":01,\"cmd\":\"start\"}",
        "{\"id\":1,\"cmd\":\"start\",\"cmd\":\"start\"}", "{\"id\":2147483648,\"cmd\":\"start\"}",
        "{\"id\":1,\"cmd\":\"start\"}junk", "{\"id\":1,\"cmd\":\"pattern\"}"};
    for (const char *text : bad) {
        request(text);
        assert(strstr(tx, "\"ok\":false"));
    }
}

void test_patterns() {
    for (const char *name : {"black", "white", "checker", "inverse", "border", "rows", "columns"}) {
        strcpy(pattern, name);
        Wire.pixels.clear();
        assert(display_pattern());
        assert(Wire.pixels.size() == 512);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 128; ++x) {
                bool expected = false;
                if (!strcmp(name, "white")) expected = true;
                if (!strcmp(name, "checker")) expected = ((x / 4 + y / 4) % 2) == 0;
                if (!strcmp(name, "inverse")) expected = ((x / 4 + y / 4) % 2) == 1;
                if (!strcmp(name, "border")) expected = x == 0 || x == 127 || y == 0 || y == 31;
                if (!strcmp(name, "rows")) expected = y % 2 == 0;
                if (!strcmp(name, "columns")) expected = x % 2 == 0;
                assert(bool(Wire.pixels[(y / 8) * 128 + x] & (1 << (y % 8))) == expected);
            }
        }
    }
    Wire.status = 2;
    request("{\"cmd\":\"pattern\",\"id\":3,\"pattern\":\"white\"}");
    assert(!oled_ok && strstr(tx, "oled_i2c_failed"));
    const uint32_t scans = readings.scans;
    ticks(2);
    assert(readings.scans > scans);
}

void test_overflow() {
    clock_us = last_scan + 1000;
    scan_inputs();
    assert(readings.overflow);
    reset_counts();
    assert(!readings.overflow);
    readings.edges[0] = UINT32_MAX;
    switches[4][0] = true;
    tick();
    assert(readings.overflow);
    reset_counts();
    state_frame();
    Serial.capacity = 0;
    clock_us += 251000;
    drain_tx();
    assert(readings.overflow && tx_sent == 0);
    Serial.capacity = 64;
    while (tx_sent < tx_size) drain_tx();
}

int main() {
    std::fill(levels, levels + 64, 1);
    setup();
    ticks(45);
    assert(settled && !readings.overflow);
    for (int i = 0; i < 28; ++i) assert(!readings.contacts[i]);
    request("{\"cmd\":\"start\",\"id\":1}");
    assert(strstr(tx, "\"ok\":true"));
    test_contacts();
    test_rotation();
    test_protocol();
    test_patterns();
    test_overflow();
    std::cout << "Firmware simulation passed: contacts, rotation, protocol, OLED patterns, overflow.\n";
}
