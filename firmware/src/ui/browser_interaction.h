#ifndef BROWSER_INTERACTION_H
#define BROWSER_INTERACTION_H

#include <array>
#include <cstddef>
#include <cstdint>

// Browser long-press state machine (portable, control-path only).
//
// No audio, no display, no heap, no PlaybackMode/latch/speed knowledge.
// Every notification carries the caller's monotonic timestamp, so tests are
// fully deterministic with injected clocks.
//
// Wiring (harness):
//   - E1 press on a browser entry   -> press(nowMs, name, kind)
//   - E1 still held (poll tick)     -> hold(nowMs)
//   - E1 released                   -> release(nowMs)
//   - E1 fresh press inside menu    -> confirm(nowMs)
//   - Backspace                     -> cancel(nowMs)
//
// Contract:
//   - A WAV short press (release before kLongPressThresholdMs) reports the
//     existing single-pad assignment path as a ShortPress event. No audio is
//     performed by this component.
//   - A WAV hold reaching kLongPressThresholdMs enters AssignmentMenu exactly
//     once per press; further hold notifications do not re-trigger.
//   - Releasing after the menu opened does NOT auto-confirm; the menu awaits
//     a fresh E1 press.
//   - Folders never enter the menu. A folder short press reports ShortPress so
//     navigation keeps working; a folder held past the threshold releases
//     without any event.
//   - confirm() reports ConfirmTransient carrying the targeted WAV identity
//     (bounded copy) and resets to Browsing. cancel() reports CancelMenu,
//     returns to Browsing, and has no side effects.

// The targeted WAV identity is copied into a fixed array of at most this many
// characters (plus terminator). Longer identities are truncated at this bound
// with no allocation; the harness must look the entry up by this bounded copy.
constexpr std::size_t kBrowserTargetNameMax = 63;

enum class BrowserMode {
    Browsing,
    AssignmentMenu,
};

enum class AssignmentAction {
    Transient,
    Cancel,
};

class BrowserInteraction {
public:
    static constexpr std::uint64_t kLongPressThresholdMs = 600;

    enum class EntryKind { Wav, Folder };

    struct Event {
        enum class Type {
            None,
            ShortPress,       // WAV: existing single-pad assignment path.
                              // Folder: navigate into it.
            EnterMenu,        // WAV hold reached the threshold (once per press).
            ConfirmTransient, // Fresh E1 press inside the menu.
            CancelMenu,       // Backspace/cancel inside the menu.
        };

        Type type = Type::None;
        EntryKind kind = EntryKind::Wav;
        AssignmentAction action = AssignmentAction::Cancel;
        // Targeted identity for ShortPress, EnterMenu and ConfirmTransient.
        std::array<char, kBrowserTargetNameMax + 1> name{};
    };

    BrowserMode mode() const noexcept { return mode_; }
    bool pressed() const noexcept { return pressed_; }

    // Browsing: a browser entry went down. Copies the identity (bounded).
    // Pressing while already pressed restarts the hold timer with the new
    // identity. Not valid inside AssignmentMenu; returns None there.
    Event press(std::uint64_t nowMs, const char* name, EntryKind kind);

    // Browsing: the pressed entry is still down. Reports EnterMenu exactly
    // once per press when a WAV hold reaches the threshold.
    Event hold(std::uint64_t nowMs);

    // Browsing: the pressed entry was released. Reports ShortPress when the
    // hold was shorter than the threshold. Releasing after the menu opened
    // reports nothing and never auto-confirms.
    Event release(std::uint64_t nowMs);

    // AssignmentMenu: fresh E1 press confirms the Transient assignment for the
    // targeted WAV and resets to Browsing.
    Event confirm(std::uint64_t nowMs);

    // AssignmentMenu: backspace/cancel closes the menu, returns to Browsing,
    // and has no side effects.
    Event cancel(std::uint64_t nowMs);

private:
    void copyTarget(const char* name);

    BrowserMode mode_ = BrowserMode::Browsing;
    EntryKind kind_ = EntryKind::Wav;
    std::array<char, kBrowserTargetNameMax + 1> target_{};
    std::uint64_t pressStartMs_ = 0;
    bool pressed_ = false;
    bool menuTriggered_ = false;
};

#endif
