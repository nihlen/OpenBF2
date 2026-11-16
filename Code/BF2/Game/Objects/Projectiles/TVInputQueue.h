// [CHOSEN, not recovered] Local serverext/r1 change, not for upstream (no issue).
// Shared state for the tv-input-queue setting and the TV missile trace: which players guide a TV
// missile through its camera, and how many of their actions the server holds.
//
// A client sets its clock to the tick of each control-object state plus the number of its own
// actions the server has not consumed yet (GameClient::handleControlObjectState), and draws remote
// objects at that clock minus its interpolation time. Every action waiting in the player's
// ActionBuffer therefore moves the player's view one tick closer to the present. Retail leaves
// that depth to chance, anywhere from 0 to 4. See GameServer::simulatePlayerUpdate.
#pragma once

#include "../../../Core/System.h"

namespace dice { namespace hfe { namespace world { namespace tv_input_queue {

struct Slot
{
    unsigned int guidingTick;   // the last tick the player's TV missile flew with its camera on, plus one; 0: never
    int queued;                 // actions left in the ActionBuffer after the player's last update
    int held;                   // updates in the current flight that left a queued action waiting
    int dropped;                // actions dropped unexecuted since the flight began to bring the queue down
    int baseline;               // queued when the flight began: the depth the queue returns to afterwards
    int restoreWindows;         // windows left in which the queue may still be brought back to the baseline
    int windowMin;              // the smallest queued value in the current window of updates
    int windowTicks;            // updates in that window
};

// Updates per window. A queue whose smallest depth over one window is above the target loses one
// action at the window's end, so a burst of actions from one packet is not taken for a deep queue.
const int kWindowTicks = 15;

// Windows after a flight in which the queue is brought back to its baseline, one action per window.
const int kRestoreWindows = 9;

// One slot per player id. The simulation thread is the only reader and writer.
inline Slot& slot(int playerId)
{
    static Slot slots[256];
    return slots[playerId & 255];
}

// Whether the player's TV missile flew with its camera on within the last two ticks.
inline bool guiding(int playerId)
{
    const unsigned int marked = slot(playerId).guidingTick;
    return marked != 0 && WorldPref::mTickCount + 1u - marked <= 2u;
}

// Called by the missile once per server tick while its launcher watches its camera.
inline void noteGuiding(int playerId)
{
    Slot& s = slot(playerId);
    if (!guiding(playerId))                         // a new flight
    {
        s.held = 0;
        s.dropped = 0;
        if (s.restoreWindows == 0)                  // else the last flight's baseline still stands
            s.baseline = s.queued;
        s.windowMin = 0x7fffffff;
        s.windowTicks = 0;
    }
    s.guidingTick = WorldPref::mTickCount + 1u;
}

// The ActionBuffer depth the setting asks for, at most 8.
inline int clampTarget(int value)
{
    return value < 0 ? 0 : (value > 8 ? 8 : value);
}

} } } } // namespace dice::hfe::world::tv_input_queue
