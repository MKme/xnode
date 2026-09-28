#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef NATIVE_64BIT
#include <mutex>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

// Cross-task producers only copy data. Subscribers run later on the GUI-guarded
// power loop; never invoke them while holding this short queue lock.
class DeferredEvents {
public:
    static constexpr size_t CAPACITY = 32;
    static constexpr size_t TEXT_SIZE = 96;
    struct Event {
        uint32_t bits = 0;
        enum Kind { NONE, TEXT, BOOLEAN } kind = NONE;
        bool boolean = false;
        char text[TEXT_SIZE] = {};
        void *argument() { return kind == TEXT ? static_cast<void *>(text) :
            kind == BOOLEAN ? static_cast<void *>(&boolean) : nullptr; }
    };

    void push_text(uint32_t bits, const char *text) {
        Event event;
        event.bits = bits;
        if (text) {
            event.kind = Event::TEXT;
            size_t n = 0;
            while (n + 1 < TEXT_SIZE && text[n]) { event.text[n] = text[n]; ++n; }
            event.text[n] = 0;
        }
        push(event);
    }
    void push_bool(uint32_t bits, bool value) {
        Event event;
        event.bits = bits; event.kind = Event::BOOLEAN; event.boolean = value;
        push(event);
    }
    bool pop(Event &event) {
        Guard guard(*this);
        if (!size_) return false;
        event = entries_[head_];
        head_ = (head_ + 1) % CAPACITY;
        --size_;
        return true;
    }
    uint32_t take_overflow_count() {
        Guard guard(*this);
        uint32_t result = overflow_; overflow_ = 0; return result;
    }
private:
    void push(const Event &event) {
        Guard guard(*this);
        if (size_ == CAPACITY) {
            // Prefer an older value of this event, otherwise an older duplicate
            // of another event. Retain the latest state for every event kind,
            // and the relative order of retained events. Scan rows may be lost
            // under overload; the caller reports the overflow instead of hanging.
            size_t remove = CAPACITY;
            for (size_t i = 0; i < size_; ++i)
                if (entries_[(head_ + i) % CAPACITY].bits == event.bits) { remove = i; break; }
            for (size_t i = 0; remove == CAPACITY && i < size_; ++i)
                for (size_t j = i + 1; j < size_; ++j)
                    if (entries_[(head_ + i) % CAPACITY].bits == entries_[(head_ + j) % CAPACITY].bits) { remove = i; break; }
            // Current BLE/WiFi producers have fewer than32 distinct event kinds.
            // This fallback also bounds future unknown producers safely.
            if (remove == CAPACITY) remove = 0;
            for (size_t i = remove; i + 1 < size_; ++i)
                entries_[(head_ + i) % CAPACITY] = entries_[(head_ + i + 1) % CAPACITY];
            --size_; ++overflow_;
        }
        entries_[(head_ + size_) % CAPACITY] = event;
        ++size_;
    }
    struct Guard {
        DeferredEvents &owner;
        explicit Guard(DeferredEvents &queue) : owner(queue) {
#ifdef NATIVE_64BIT
            owner.mutex_.lock();
#else
            portENTER_CRITICAL(&owner.mutex_);
#endif
        }
        ~Guard() {
#ifdef NATIVE_64BIT
            owner.mutex_.unlock();
#else
            portEXIT_CRITICAL(&owner.mutex_);
#endif
        }
    };
#ifdef NATIVE_64BIT
    std::mutex mutex_;
#else
    portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
#endif
    Event entries_[CAPACITY];
    size_t head_ = 0, size_ = 0;
    uint32_t overflow_ = 0;
};
