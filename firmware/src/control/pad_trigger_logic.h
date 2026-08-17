#pragma once

#include "../engine/playback_mode.h"

enum class TriggerBehavior {
    Gate,
    Latch,
};

enum class PadTriggerAction {
    None,
    Trigger,
    Stop,
};

constexpr PadTriggerAction padDownAction(PlaybackMode mode,
                                         TriggerBehavior behavior,
                                         bool isPlaying) {
    // Un deuxieme appui en LATCH arrete la lecture pour les modes qui
    // tournent en continu (LOOP et CLOUD), et seulement eux : ONE SHOT en
    // LATCH se rejoue a chaque appui tant que la voix vit.
    if ((mode == PlaybackMode::Loop || mode == PlaybackMode::Granular) &&
        behavior == TriggerBehavior::Latch && isPlaying) {
        return PadTriggerAction::Stop;
    }
    return PadTriggerAction::Trigger;
}

constexpr PadTriggerAction padUpAction(TriggerBehavior behavior) {
    return behavior == TriggerBehavior::Gate ? PadTriggerAction::Stop
                                              : PadTriggerAction::None;
}
