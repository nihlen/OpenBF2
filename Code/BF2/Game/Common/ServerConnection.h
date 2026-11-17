// Server-side session with object scoping, ghost priorities and join-database delivery.
// Incoming events execute on the server. A connection records only when demo mode is 1.
// m_dword17C counts priority evaluations during the current scoping pass.

#ifndef GAME_COMMON_SERVERCONNECTION_H       // [INFERRED] guard style
#define GAME_COMMON_SERVERCONNECTION_H

#include "ClientConnection.h"
#include "../../Math/Mat4.h"                   // calculateObjectPriority / getRelevantObjects
#include "../../Math/Vec3.h"

namespace dice {
namespace hfe {

namespace io { struct NetworkableDescriptor; }   // struct, matching NetworkManager.h (was `class`,
                                                 //   an MSVC mangling mismatch) -
namespace world { class IObject; class IPlayer; }

class ServerConnection : public ClientConnection
{
public:
    explicit ServerConnection(ConnectionDemoMode demoMode);
    virtual ~ServerConnection();

    // the ONE override
    virtual bool getIsRecordingConnection() const;               // +0x20

    // scope and ghosting. NON-virtual, every one of them.
    void addStaticGhosts();
    void getAllObjects();
    void getSpecialCommanderObjects(int team);
    void updateGhostManager(float dt);
    void updateGhostable(world::IObject* object, bool add);

    // THE ghost-scoping front IS  - all five bodies read in full; the
    // priority metric and the relevance rules the surface note asked for are in the .cpp.
    // returns bool - the ELF tail is `return ghostManager->updatePlayerControlObject(...)` with
    // a movzx, and the early-outs materialise false. Was declared void until the body was read.
    bool  updatePlayerControlObject(int frameCount);
    // parameter semantics the two floats after the Vec3 are the scope
    // range and its square (the falloff divisor and the distance gate), not a delta time; the Vec3
    // is the viewer's root-vehicle velocity and is dead inside (see the body); the final bool
    // selects raw score (true) vs score * m_basePriority (false).
    float calculateObjectPriority(io::NetworkableDescriptor* descriptor, float fovThreshold,
                                  world::IPlayer* player, Mat4& viewTransform,
                                  const Vec3& viewerVelocity, float range, float rangeSq,
                                  bool rawOnly);
    // the float is the scope range (metres), not a delta time - it flows into 1 - dist/range.
    void  getRelevantObjects(Mat4& viewTransform, float range);
    void  getRelevantEnemyObjects(const Vec3& centre, float radius);
    // the float is the same range, not a priority - it is squared into the gate like the others.
    void  addScopableGhost(io::NetworkableDescriptor* descriptor, float range);
    void  clientSendPlayerDatabase();
    void  clientSendDatabase();                                  //  the join push
    // [CHOSEN, not recovered] openbf2-server-ext docs/admin-channel.md R8. The first part of
    // clientSendDatabase's body, split out: the create events for every dynamic networked object,
    // each living player's vehicle and kit events, and the sticky projectiles. clientSendDatabase
    // calls it with ObjectDatabaseAll first, so a join sends what it always did. The other two
    // parts are for IServerExtension::resyncClient: ObjectDatabaseScoped leaves out the create
    // events of objects with a negative base priority (static ghosts, such as spawned static
    // objects), ObjectDatabaseStatic sends only those and no player or projectile events.
    enum ObjectDatabasePart { ObjectDatabaseAll, ObjectDatabaseScoped, ObjectDatabaseStatic };
    void  clientSendObjectDatabase(ObjectDatabasePart part);

public:
    /* +0x17C */ int m_dword17C;        // = 0   [UNRESOLVED role]
    /* +0x180 */ int m_loadUnload;      // = 0   sendClientMapInfo increments it
    /* +0x184 */ int m_gameId;          // = -1  [INFERRED name]
};

// Orders connections by ascending scope timer so overdue ghost updates run first.
// Unordered floating-point comparisons return false.
struct ScopeTimeSort
{
    bool operator()(const ServerConnection* a, const ServerConnection* b) const
    {
        return a->m_timer20 < b->m_timer20;
    }
};

} // namespace hfe
} // namespace dice

#endif // GAME_COMMON_SERVERCONNECTION_H
