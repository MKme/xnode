#include "hardware/deferred_events.h"
#include <atomic>
#include <assert.h>
#include <stdio.h>
#include <thread>
#include <vector>
#include <string>

using EventBits_t = uint32_t;
struct callback_t { int source; } ble{1}, wifi{2}, clock_events{3};
callback_t *blectl_callback = &ble, *wifictl_callback = &wifi, *timesync_callback = &clock_events;
constexpr uint32_t WIFICTL_AUTOON = 64;
static bool callback_send(callback_t *, EventBits_t, void *);
static unsigned warnings = 0;
#define log_w(...) (++warnings)
#include "deferred_actual.inc"

struct Observed { int source; uint32_t event; std::string text; bool boolean; };
static std::vector<Observed> observed;
static std::thread::id consumer;
static bool gui_guard = false, reentrant = false;
static bool callback_send(callback_t *source, EventBits_t event, void *arg) {
    // Models the actual GUI subscriber requirement. Producer threads may never
    // enter this function. The caller drains only under the existing GUI guard.
    assert(std::this_thread::get_id() == consumer && gui_guard);
    const bool boolean = source == &wifi && event == WIFICTL_AUTOON;
    observed.push_back({source->source, event, boolean || !arg ? "" : static_cast<char *>(arg),
                        boolean && arg ? *static_cast<bool *>(arg) : false});
    if (reentrant && source == &ble && event == 99) {
        reentrant = false;
        blectl_send_event_cb(100, nullptr); // Deadlocks if dispatch holds queue lock.
    }
    return true;
}
static void drain() {
    gui_guard = true;
    blectl_drain_events(0, nullptr); wifictl_drain_events(0, nullptr); timesync_drain_events(0, nullptr);
    gui_guard = false;
}
int main() {
    consumer = std::this_thread::get_id();
    char pin[] = "123456", ssid[] = "field network";
    bool autoon = true;
    blectl_send_event_cb(1, pin); wifictl_send_event_cb(2, ssid);
    wifictl_send_event_cb(WIFICTL_AUTOON, &autoon); timesync_send_event_cb(1, pin);
    memset(pin, 'x', 6); memset(ssid, 'x', 13); autoon = false;
    assert(observed.empty()); // Setup-time events also wait for guarded drain.
    drain();
    assert(observed.size() == 4 && observed[0].text == "123456" && observed[1].text == "field network");
    assert(observed[2].boolean && observed[3].text.empty());
    observed.clear(); reentrant = true; blectl_send_event_cb(99, nullptr); drain();
    assert(observed.size() == 2 && observed[1].event == 100);
    observed.clear();
    // Overflow retains latest distinct states even when scan rows dominate.
    wifictl_send_event_cb(4, (void *)"connected");
    for (int i = 0; i < 1000; ++i) wifictl_send_event_cb(8, (void *)"scan row");
    wifictl_send_event_cb(16, (void *)"disconnected");
    drain();
    assert(warnings && observed.front().event == 4 && observed.back().event == 16);
    observed.clear();
    std::atomic<int> done{0};
    auto producer = [&](int id) {
        for (int i = 0; i < 20000; ++i) {
            char text[80]; snprintf(text, sizeof(text), "%d:%d:complete", id, i);
            blectl_send_event_cb(static_cast<uint32_t>(id), text);
            memset(text, 'z', sizeof(text));
        }
        ++done;
    };
    std::thread first(producer, 1), second(producer, 2);
    while (done.load() != 2) { drain(); std::this_thread::yield(); }
    first.join(); second.join(); drain();
    int last[3] = {-1, -1, -1};
    for (const auto &value : observed) {
        int id = 0, sequence = 0, end = 0;
        assert(sscanf(value.text.c_str(), "%d:%d:complete%n", &id, &sequence, &end) == 2);
        assert(end == static_cast<int>(value.text.size()) && id == static_cast<int>(value.event));
        assert(sequence > last[id]); last[id] = sequence;
    }
    assert(last[1] == 19999 && last[2] == 19999);
    puts("PASS actual BLE/WiFi/time producers and drains: copied payloads, deferred GUI ownership, bounded overflow, reentrant enqueue, 40000 concurrent events");
}
