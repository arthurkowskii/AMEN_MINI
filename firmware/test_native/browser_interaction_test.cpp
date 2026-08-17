#include "browser_interaction.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using Event = BrowserInteraction::Event;

bool none(const Event& event) {
    return event.type == Event::Type::None;
}

bool nameIs(const Event& event, const char* expected) {
    return std::strcmp(event.name.data(), expected) == 0;
}

void testShortPressOnWavReportsExistingPath() {
    BrowserInteraction interaction;
    assert(interaction.mode() == BrowserMode::Browsing);
    assert(!interaction.pressed());

    assert(none(interaction.press(1000, "amen.wav",
                                  BrowserInteraction::EntryKind::Wav)));
    assert(interaction.pressed());
    assert(none(interaction.hold(1599)));
    assert(interaction.mode() == BrowserMode::Browsing);

    const Event event = interaction.release(1599);
    assert(event.type == Event::Type::ShortPress);
    assert(event.kind == BrowserInteraction::EntryKind::Wav);
    assert(nameIs(event, "amen.wav"));
    assert(!interaction.pressed());
}

void testHoldThresholdsEnterMenuExactlyOnce() {
    BrowserInteraction interaction;
    assert(none(interaction.press(0, "break.wav",
                                  BrowserInteraction::EntryKind::Wav)));

    // 599 ms: still browsing, no menu.
    assert(none(interaction.hold(599)));
    assert(interaction.mode() == BrowserMode::Browsing);

    // 600 ms: the menu opens exactly once per press.
    const Event first = interaction.hold(600);
    assert(first.type == Event::Type::EnterMenu);
    assert(nameIs(first, "break.wav"));
    assert(interaction.mode() == BrowserMode::AssignmentMenu);

    // Further hold notifications never re-trigger the menu.
    assert(none(interaction.hold(601)));
    assert(none(interaction.hold(700)));
    assert(none(interaction.hold(100000)));
    assert(interaction.mode() == BrowserMode::AssignmentMenu);
}

void testReleaseAfterMenuDoesNotAutoConfirm() {
    BrowserInteraction interaction;
    interaction.press(0, "amen.wav", BrowserInteraction::EntryKind::Wav);
    assert(interaction.hold(600).type == Event::Type::EnterMenu);
    assert(interaction.mode() == BrowserMode::AssignmentMenu);

    assert(none(interaction.release(800)));
    assert(interaction.mode() == BrowserMode::AssignmentMenu);
}

void testConfirmReturnsTransientWithIdentityAndResets() {
    BrowserInteraction interaction;
    interaction.press(0, "amen.wav", BrowserInteraction::EntryKind::Wav);
    assert(interaction.hold(600).type == Event::Type::EnterMenu);
    assert(none(interaction.release(700)));

    const Event confirm = interaction.confirm(900);
    assert(confirm.type == Event::Type::ConfirmTransient);
    assert(confirm.action == AssignmentAction::Transient);
    assert(nameIs(confirm, "amen.wav"));
    assert(interaction.mode() == BrowserMode::Browsing);
    assert(!interaction.pressed());

    // Back in Browsing, confirm/cancel report nothing.
    assert(none(interaction.confirm(1000)));
    assert(none(interaction.cancel(1000)));

    // A new press can open the menu and confirm again.
    interaction.press(2000, "other.wav", BrowserInteraction::EntryKind::Wav);
    assert(interaction.hold(2600).type == Event::Type::EnterMenu);
    assert(none(interaction.release(2700)));
    const Event second = interaction.confirm(2800);
    assert(second.type == Event::Type::ConfirmTransient);
    assert(nameIs(second, "other.wav"));
}

void testFoldersNeverEnterTheMenu() {
    BrowserInteraction interaction;
    assert(none(interaction.press(0, "drums",
                                  BrowserInteraction::EntryKind::Folder)));

    assert(none(interaction.hold(599)));
    assert(none(interaction.hold(600)));
    assert(none(interaction.hold(5000)));
    assert(interaction.mode() == BrowserMode::Browsing);

    // A folder held past the threshold releases without any event.
    assert(none(interaction.release(5001)));
    assert(interaction.mode() == BrowserMode::Browsing);

    // A short folder press reports the navigation event.
    interaction.press(6000, "drums", BrowserInteraction::EntryKind::Folder);
    const Event shortPress = interaction.release(6400);
    assert(shortPress.type == Event::Type::ShortPress);
    assert(shortPress.kind == BrowserInteraction::EntryKind::Folder);
    assert(nameIs(shortPress, "drums"));
}

void testCancelReturnsToBrowsingWithoutSideEffects() {
    BrowserInteraction interaction;
    interaction.press(0, "amen.wav", BrowserInteraction::EntryKind::Wav);
    interaction.hold(600);
    interaction.release(700);

    const Event cancel = interaction.cancel(800);
    assert(cancel.type == Event::Type::CancelMenu);
    assert(cancel.action == AssignmentAction::Cancel);
    assert(interaction.mode() == BrowserMode::Browsing);
    assert(!interaction.pressed());

    // A fresh press after cancel starts clean.
    interaction.press(1000, "other.wav", BrowserInteraction::EntryKind::Wav);
    assert(none(interaction.hold(1599)));
    const Event shortPress = interaction.release(1599);
    assert(shortPress.type == Event::Type::ShortPress);
    assert(nameIs(shortPress, "other.wav"));
}

void testTimerRestartsAfterShortPress() {
    BrowserInteraction interaction;
    interaction.press(0, "a.wav", BrowserInteraction::EntryKind::Wav);
    assert(interaction.release(400).type == Event::Type::ShortPress);

    // Re-press restarts the timer from the new press timestamp.
    interaction.press(1000, "b.wav", BrowserInteraction::EntryKind::Wav);
    assert(none(interaction.hold(1550)));  // 550 ms since re-press
    const Event menu = interaction.hold(1600);  // 600 ms since re-press
    assert(menu.type == Event::Type::EnterMenu);
    assert(nameIs(menu, "b.wav"));
}

void testPressWhilePressedRestartsTheHold() {
    BrowserInteraction interaction;
    interaction.press(0, "a.wav", BrowserInteraction::EntryKind::Wav);
    interaction.press(200, "a.wav", BrowserInteraction::EntryKind::Wav);
    assert(none(interaction.hold(700)));  // 500 ms since the last press
    assert(interaction.hold(800).type == Event::Type::EnterMenu);
}

void testIdentityCopyIsBoundedWithoutHeap() {
    std::array<char, 300> longName{};
    longName.fill('L');
    longName.back() = '\0';

    BrowserInteraction interaction;
    interaction.press(0, longName.data(), BrowserInteraction::EntryKind::Wav);

    const Event menu = interaction.hold(600);
    assert(menu.type == Event::Type::EnterMenu);
    assert(menu.name[kBrowserTargetNameMax] == '\0');
    assert(menu.name[kBrowserTargetNameMax - 1] == 'L');

    assert(none(interaction.release(700)));
    const Event confirm = interaction.confirm(800);
    assert(confirm.type == Event::Type::ConfirmTransient);
    assert(confirm.name[kBrowserTargetNameMax] == '\0');
    assert(confirm.name[kBrowserTargetNameMax - 1] == 'L');
    assert(confirm.name[0] == 'L');
}

void testNullNameIsSafe() {
    BrowserInteraction interaction;
    assert(none(interaction.press(0, nullptr,
                                  BrowserInteraction::EntryKind::Wav)));
    const Event shortPress = interaction.release(100);
    assert(shortPress.type == Event::Type::ShortPress);
    assert(shortPress.name[0] == '\0');
}

void testInputsOutsideRelevantStateAreIgnored() {
    BrowserInteraction interaction;
    assert(none(interaction.hold(0)));
    assert(none(interaction.release(0)));
    assert(none(interaction.confirm(0)));
    assert(none(interaction.cancel(0)));

    // press() inside the menu is ignored; the menu still awaits confirm().
    interaction.press(1000, "amen.wav", BrowserInteraction::EntryKind::Wav);
    assert(interaction.hold(1600).type == Event::Type::EnterMenu);
    assert(interaction.mode() == BrowserMode::AssignmentMenu);
    assert(none(interaction.press(1700, "other.wav",
                                  BrowserInteraction::EntryKind::Wav)));
    assert(interaction.mode() == BrowserMode::AssignmentMenu);
    const Event confirm = interaction.confirm(1800);
    assert(confirm.type == Event::Type::ConfirmTransient);
    assert(nameIs(confirm, "amen.wav"));
}
}  // namespace

int main() {
    testShortPressOnWavReportsExistingPath();
    testHoldThresholdsEnterMenuExactlyOnce();
    testReleaseAfterMenuDoesNotAutoConfirm();
    testConfirmReturnsTransientWithIdentityAndResets();
    testFoldersNeverEnterTheMenu();
    testCancelReturnsToBrowsingWithoutSideEffects();
    testTimerRestartsAfterShortPress();
    testPressWhilePressedRestartsTheHold();
    testIdentityCopyIsBoundedWithoutHeap();
    testNullNameIsSafe();
    testInputsOutsideRelevantStateAreIgnored();
    std::printf("browser_interaction: ok\n");
    return 0;
}
