#include <atomic>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdio.h>

constexpr char EndofText = 3, DataLinkEscape = 16, LineFeed = 10;
constexpr int GADGETBRIDGE_CONNECT = 1, BLECTL_CONNECT = 1, pdTRUE = 1;
static bool connected = true, fail_alloc = false, queue_full = false;
static int value_reads = 0, resumes = 0, dispatched = 0, allocated = 0;
static std::vector<char *> queued;
static void *gadgetbridge_msg_receive_queue = nullptr;
static std::atomic<bool> gadgetbridge_connect_pending(false);
struct Buffer {
    std::string value;
    void clear() { value.clear(); }
    void append(char ch) { value += ch; }
    const char *c_str() { return value.c_str(); }
} gadgetbridge_RX_msg;
struct NimBLECharacteristic {
    std::string value;
    std::string getValue() { ++value_reads; return value; }
};
static size_t strlcpy(char *to, const char *from, size_t n) {
    const size_t length = strlen(from);
    if (n) { memcpy(to, from, length < n - 1 ? length : n - 1); to[length < n - 1 ? length : n - 1] = 0; }
    return length;
}
static void *checked_calloc(size_t n, size_t size) {
    if (fail_alloc) return nullptr;
    ++allocated; return calloc(n, size);
}
static void checked_free(void *pointer) { if (pointer) { --allocated; free(pointer); } }
static bool blectl_get_event(int) { return connected; }
static bool gadgetbridge_send_event_cb(int event, void *) { assert(event == GADGETBRIDGE_CONNECT); ++dispatched; return true; }
static void powermgm_resume() { ++resumes; }
static int xQueueSend(void *, char **pointer, int wait) {
    assert(wait == 0);
    if (queue_full) return 0;
    queued.push_back(*pointer); return pdTRUE;
}
#define CALLOC checked_calloc
#define free checked_free
#define log_e(...) ((void)0)
#include "gadgetbridge_actual.inc"
#undef free

int main() {
    NimBLECharacteristic characteristic;
    characteristic.value = std::string(200, 'a') + "\n";
    onWrite(&characteristic);
    characteristic.value.assign(300, 'z');
    assert(value_reads == 1 && queued.size() == 1 && strlen(queued[0]) == 200 && queued[0][199] == 'a');
    checked_free(queued[0]); queued.clear();
    characteristic.value = "first fragment "; onWrite(&characteristic);
    characteristic.value = "second\nnext\n"; onWrite(&characteristic);
    assert(queued.size() == 2 && std::string(queued[0]) == "first fragment second" && std::string(queued[1]) == "next");
    for (auto pointer : queued) checked_free(pointer);
    queued.clear();
    const int previous_resumes = resumes;
    queue_full = true; characteristic.value = "rejected\n"; onWrite(&characteristic);
    assert(queued.empty() && allocated == 0 && resumes == previous_resumes);
    queue_full = false; fail_alloc = true; onWrite(&characteristic);
    assert(allocated == 0 && gadgetbridge_RX_msg.value.empty()); fail_alloc = false;
    characteristic.value = std::string(1, EndofText); onWrite(&characteristic);
    assert(dispatched == 0 && gadgetbridge_connect_pending.load());
    gadgetbridge_dispatch_pending_connect(); assert(dispatched == 1);
    gadgetbridge_dispatch_pending_connect(); assert(dispatched == 1);
    onWrite(&characteristic); connected = false;
    gadgetbridge_dispatch_pending_connect(); assert(dispatched == 1 && !gadgetbridge_connect_pending.load());
    puts("PASS actual Gadgetbridge onWrite: owned value lifetime, fragments, task queue/wakeup, allocation failure, full-queue free, deferred connect");
}
