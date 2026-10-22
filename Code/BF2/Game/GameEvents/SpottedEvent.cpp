#include "SpottedEvent.h"

// PostRemoteEvent is declared alongside GameEvent, in Game/Common/GameEvent.h - already included
// through SpottedEvent.h. No separate header exists.
#include "../Objects/IObject.h"                  // m_aiObject (+0x34) via LayoutAccess
#include "../Objects/ObjectLayoutAccess.h"
#include "../Python/PyRef.h"
#include "../Python/PythonHost.h"                // pythonHost, buildArgs, handleEvent
#include "../../AI/AIInterface/AIInstance.h"     // aiInstance()->isBotsGame()  (ds:0x8AC37F8)
#include "../../Core/EventManager.h"             // eventManager->postEvent
#include "../../IO/BitStream.h"                  // readBits / writeBits
#include "../GameServer/ServerExtensionHooks.h"  // [CHOSEN, not recovered] openbf2-server-ext v2

namespace dice {
namespace hfe {

namespace {
// .rodata dword GESpotted (0x888A724) = 29.
const int GESpotted = 29;

// The Python callback slot this event fires. [UNRESOLVED] name - Game/Python/PythonHost.h records
// the ScriptEvent enumerators as unrecovered, so the value travels bare, as it does at every other
// handleEvent site in this tree.
const int SCRIPT_EVENT_SPOTTED = 41;             // 0x29

// The AI notification: category ecai (3), event id 0x16 = 22.
const unsigned int NOTIFY_AI_SPOTTED = 0x16u;

// The block executeServer hands to the ecai listener - built on the stack, eight bytes, passed by
// address. Its real name is [UNRESOLVED]; only its shape is attested (a dword at [ebp-0x28] and a
// byte at [ebp-0x24], with the address of the dword passed).
struct AiSpottedNotification
{
    void*         aiObject;      // IObject +0x34
    unsigned char team;          // the low byte of m_team
};
} // namespace

// Initializes only the two fields below; the remaining payload is filled by deserialization.
SpottedEvent::SpottedEvent()
    : GameEvent()
{
    m_position.x = 0.0f;                         // +0x14
    m_position.y = 0.0f;                         // +0x18
}

int SpottedEvent::getType() const
{
    return GESpotted;
}

// `mov eax, 1; ret`. This event travels client-to-server only.
void SpottedEvent::executeClient()
{
}

// Notifies Python using the sending connection ID, optionally informs bots, then rebroadcasts
// the payload to all playing clients. Missing AI/target state does not suppress the broadcast.
// The received HUD event ID is forwarded without validation.
bool SpottedEvent::executeServer(int connectionId)
{
    // [CHOSEN, not recovered] openbf2-server-ext docs/protocol.md v2 item 6: tell the spot
    // observers. Read-only; the retail body below is unchanged.
    {
        serverExtensionHooks::ObserverTable<IServerExtension::ISpotObserver>& observers =
            serverExtensionHooks::spotObservers();
        if (!observers.empty())
        {
            IServerExtension::SpotInfo info;
            info.connectionId = connectionId;
            info.spotterNetId = m_spotterVehicleNetId;
            info.targetNetId = m_targetNetId;
            info.team = m_team;
            info.x = m_position.x;
            info.z = m_position.y;   // Game::sendSpottedMessage packs the world x/z into this Vec2
            info.vehicleType = static_cast<int>(m_vehicleType);
            for (int i = 0; i < observers.size(); ++i)
                observers.at(i)->onSpotted(info);
        }
    }

    {
        python::PyRef args = python::buildArgs(connectionId);
        python::pythonHost()->handleEvent(static_cast<ScriptEvent>(SCRIPT_EVENT_SPOTTED),
                                          args.get());
    }

    if (ai::g_aiInstance && ai::aiInstance()->isBotsGame())                       // +0x1C
    {
        world::IObject* target = game->getObjectFromNetworkableId(m_targetNetId); // +0x7C
        if (target)
        {
            void* aiObject = world::object_detail::LayoutAccess::aiObject(target);   // +0x34
            if (aiObject)
            {
                AiSpottedNotification note;
                note.aiObject = aiObject;
                note.team     = m_team;
                eventManager->postEvent(ECAI, NOTIFY_AI_SPOTTED, &note, 0.0f);   // +0x14
            }
        }
    }

    PostRemoteEvent* broadcast = new PostRemoteEvent(
        ECHud,                                   // the category is fixed; the ID is not
        static_cast<unsigned int>(m_hudEventId),
        false,
        &m_spotterVehicleNetId,                  // +0x0C
        24,                                      // +0x0C..+0x23 - the whole payload
        0.0f);
    game->sendGameEventToAllPlaying(broadcast);                                  // +0x84

    return true;
}

// Reads team directly as one byte. Other narrow values use zero-initialized temporaries.
bool SpottedEvent::deSerialize(io::BitStream* stream)
{
    int spotterVehicleNetId = 0;
    stream->readBits(&spotterVehicleNetId, 16);
    m_spotterVehicleNetId = static_cast<unsigned short>(spotterVehicleNetId);

    int targetNetId = 0;
    stream->readBits(&targetNetId, 16);
    m_targetNetId = static_cast<unsigned short>(targetNetId);

    stream->readBits(&m_position.x, 32);
    stream->readBits(&m_position.y, 32);

    stream->readBits(&m_team, 8);                // in place, one byte wide - see above

    int vehicleType = 0;
    stream->readBits(&vehicleType, 32);
    m_vehicleType = static_cast<world::VehicleType>(vehicleType);

    int hudEventId = 0;
    stream->readBits(&hudEventId, 32);
    m_hudEventId = hudEventId;

    if (!stream->hasError())
        return true;
    stream->clearError();
    return false;
}

// serialize - 0x807BBD0, 315 B. The same order.
bool SpottedEvent::serialize(io::BitStream* stream)
{
    int type = getType();
    stream->writeBits(&type, gameEventTypeIdBitCount());

    int spotterVehicleNetId = m_spotterVehicleNetId;
    stream->writeBits(&spotterVehicleNetId, 16);

    int targetNetId = m_targetNetId;
    stream->writeBits(&targetNetId, 16);

    stream->writeBits(&m_position.x, 32);
    stream->writeBits(&m_position.y, 32);

    stream->writeBits(&m_team, 8);               // straight from the field - no load in the ELF

    int vehicleType = static_cast<int>(m_vehicleType);
    stream->writeBits(&vehicleType, 32);

    int hudEventId = m_hudEventId;
    stream->writeBits(&hudEventId, 32);

    if (!stream->hasError())
        return true;
    stream->clearError();
    return false;
}

namespace serverExtensionHooks {

// [CHOSEN, not recovered] openbf2-server-ext docs/protocol.md v2 item 6. The table lives beside its
// only notify site.
ObserverTable<IServerExtension::ISpotObserver>& spotObservers()
{
    static ObserverTable<IServerExtension::ISpotObserver> table;
    return table;
}

} // namespace serverExtensionHooks

} // namespace hfe
} // namespace dice
