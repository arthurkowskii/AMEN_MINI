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
    if (mode == PlaybackMode::Loop && behavior == TriggerBehavior::Latch &&
        isPlaying) {
        return PadTriggerAction::Stop;
    }
    return PadTriggerAction::Trigger;
}

constexpr PadTriggerAction padUpAction(TriggerBehavior behavior) {
    return behavior == TriggerBehavior::Gate ? PadTriggerAction::Stop
                                              : PadTriggerAction::None;
}
