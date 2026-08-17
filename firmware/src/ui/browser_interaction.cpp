#include "browser_interaction.h"

namespace {
std::uint64_t elapsedSince(std::uint64_t nowMs, std::uint64_t startMs) {
    // Monotonic wrap guard: a stale timestamp never produces a huge elapsed.
    return nowMs >= startMs ? nowMs - startMs : 0U;
}
}  // namespace

void BrowserInteraction::copyTarget(const char* name) {
    target_.fill('\0');
    if (name == nullptr) {
        return;
    }
    std::size_t index = 0;
    while (name[index] != '\0' && index < kBrowserTargetNameMax) {
        target_[index] = name[index];
        ++index;
    }
}

BrowserInteraction::Event BrowserInteraction::press(std::uint64_t nowMs,
                                                    const char* name,
                                                    EntryKind kind) {
    if (mode_ != BrowserMode::Browsing) {
        // Precondition violation: entry presses belong to Browsing. The menu
        // stays open and still awaits confirm().
        return Event{};
    }
    copyTarget(name);
    kind_ = kind;
    pressStartMs_ = nowMs;
    pressed_ = true;
    menuTriggered_ = false;
    return Event{};
}

BrowserInteraction::Event BrowserInteraction::hold(std::uint64_t nowMs) {
    if (mode_ != BrowserMode::Browsing || !pressed_ || menuTriggered_ ||
        kind_ == EntryKind::Folder) {
        // Folders never enter the menu; a triggered menu never re-triggers.
        return Event{};
    }
    if (elapsedSince(nowMs, pressStartMs_) < kLongPressThresholdMs) {
        return Event{};
    }
    menuTriggered_ = true;
    mode_ = BrowserMode::AssignmentMenu;
    Event event;
    event.type = Event::Type::EnterMenu;
    event.kind = kind_;
    event.name = target_;
    return event;
}

BrowserInteraction::Event BrowserInteraction::release(std::uint64_t nowMs) {
    if (!pressed_) {
        return Event{};
    }
    pressed_ = false;
    const bool menuOpened = menuTriggered_;
    menuTriggered_ = false;
    if (menuOpened ||
        elapsedSince(nowMs, pressStartMs_) >= kLongPressThresholdMs) {
        // Menu opened (WAV) or long folder hold: no event, no auto-confirm.
        return Event{};
    }
    Event event;
    event.type = Event::Type::ShortPress;
    event.kind = kind_;
    event.name = target_;
    return event;
}

BrowserInteraction::Event BrowserInteraction::confirm(std::uint64_t nowMs) {
    if (mode_ != BrowserMode::AssignmentMenu) {
        return Event{};
    }
    pressed_ = false;
    menuTriggered_ = false;
    pressStartMs_ = nowMs;
    mode_ = BrowserMode::Browsing;
    Event event;
    event.type = Event::Type::ConfirmTransient;
    event.kind = kind_;
    event.action = AssignmentAction::Transient;
    event.name = target_;
    return event;
}

BrowserInteraction::Event BrowserInteraction::cancel(std::uint64_t nowMs) {
    if (mode_ != BrowserMode::AssignmentMenu) {
        return Event{};
    }
    pressed_ = false;
    menuTriggered_ = false;
    pressStartMs_ = nowMs;
    mode_ = BrowserMode::Browsing;
    target_.fill('\0');
    Event event;
    event.type = Event::Type::CancelMenu;
    event.action = AssignmentAction::Cancel;
    return event;
}
