// [CHOSEN, not recovered] Engine-side observer tables behind IServerExtension's observers. No
// retail image has them. Design record: openbf2-server-ext docs/protocol.md "v2" (items 5, 6 and 8)
// and docs/admin-channel-draft.md R4 and R5 (local design; no upstream issue yet).
//
// Each table is defined beside its only notify site (GameLogicDamage.cpp, FireComp.cpp,
// SpottedEvent.cpp, PythonHost.cpp), so every image that links the site links the table. GameServer's
// IServerExtension adapter forwards a module's add and remove calls here. Main thread only.

#ifndef GAME_GAMESERVER_SERVEREXTENSIONHOOKS_H
#define GAME_GAMESERVER_SERVEREXTENSIONHOOKS_H

#include "IServerExtension.h"

namespace dice {
namespace hfe {
namespace serverExtensionHooks {

/// A fixed-size list of borrowed observer pointers, notified in registration order.
template <class Observer>
class ObserverTable
{
public:
    static const int kCapacity = 4;

    ObserverTable() : m_count(0)
    {
        for (int i = 0; i < kCapacity; ++i)
            m_items[i] = 0;
    }

    /// @return False when observer is null, already present, or the table is full.
    bool add(Observer* observer)
    {
        if (!observer || m_count >= kCapacity)
            return false;
        for (int i = 0; i < m_count; ++i)
            if (m_items[i] == observer)
                return false;
        m_items[m_count++] = observer;
        return true;
    }

    void remove(Observer* observer)
    {
        for (int i = 0; i < m_count; ++i)
        {
            if (m_items[i] != observer)
                continue;
            for (int k = i + 1; k < m_count; ++k)
                m_items[k - 1] = m_items[k];
            m_items[--m_count] = 0;
            return;
        }
    }

    bool empty() const { return m_count == 0; }
    int size() const { return m_count; }
    Observer* at(int i) const { return m_items[i]; }

private:
    Observer* m_items[kCapacity];
    int m_count;
};

/// Observers of GameLogicDamage::serverGiveDamage (R5); defined in GameLogicDamage.cpp.
ObserverTable<IServerExtension::IDamageObserver>& damageObservers();
/// Observers of FireComp's projectile launch; defined in FireComp.cpp.
ObserverTable<IServerExtension::IProjectileObserver>& projectileObservers();
/// Observers of SpottedEvent::executeServer; defined in SpottedEvent.cpp.
ObserverTable<IServerExtension::ISpotObserver>& spotObservers();
/// The single script-event listener (R4, docs/admin-channel-draft.md), or null; defined in
/// Game/Python/PythonHost.cpp beside PythonHost::handleEvent.
IServerExtension::IScriptEventListener*& scriptEventListener();
/// The frame loop's tick source (R3, stepped time), or null for the wall clock; defined in
/// Game/Main/BF2Engine.cpp beside BF2Engine::mainLoop, its only reader.
IServerExtension::ITickSource*& tickSource();

} // namespace serverExtensionHooks
} // namespace hfe
} // namespace dice

#endif // GAME_GAMESERVER_SERVEREXTENSIONHOOKS_H
