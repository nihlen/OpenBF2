// [CHOSEN, not recovered] OpenBF2 server-extension surface for native modules. No retail image has
// it. Design record: openbf2-server-ext docs/protocol.md R1, and docs/admin-channel-draft.md R4, R5
// and R7 (local design; no upstream issue yet).
//
// GameServer publishes one instance as the named singleton "OpenBF2ServerExtension" (flag 4,
// borrowed; ClassManager does not own or release it) before the mod's openbf2.con runs. A module
// loaded by openbf2.addModDll fetches it with
// reinterpret_cast<IServerExtension*>(classManager->getSingleton("OpenBF2ServerExtension")),
// the same raw-pointer idiom Core/DllModule.cpp uses for non-IBase singletons. The singleton is
// unregistered by GameServer::shutdown.
//
// The interface is header-only and not an IBase. It adds no slot to IGame, IGameServer or any
// other retail vtable. Methods may be appended; never reorder or remove existing ones.

#ifndef GAME_GAMESERVER_ISERVEREXTENSION_H
#define GAME_GAMESERVER_ISERVEREXTENSION_H

#include <string>

namespace dice {
namespace hfe {

namespace world {
class IObject;
class IPlayer;
} // namespace world

class IServerExtension
{
public:
    /// Creates a connection-less, AI-flagged player whose input the caller writes directly into
    /// the player's AI input buffer. Unlike IGameServer::createPlayer(name, true) it does not
    /// require the AI library; it is otherwise the same creation path (free id counted down from
    /// 255, CreatePlayerEvent broadcast, Python event 14, VoIP session).
    /// Must be called on the main thread.
    /// @param name Requested player name; truncated and de-duplicated as for any player.
    /// @return The new player id, or -1 when the player limit is reached.
    virtual int createInputPlayer(const std::string& name) = 0;

    /// Finds a spawn group an AI-flagged player of the given team can spawn at. Only groups the
    /// spawn manager lists for the team (same team, at least one spawn point) and that report
    /// getActive(true) are considered; every spawn point of the group must then match
    /// enterOnSpawn. A point counts as enter-on-spawn when getEnterOnSpawn() or
    /// getAIEnterOnSpawn() is set, as ServerGameLogic::spawnPlayer tests it for an AI player.
    /// Must be called on the main thread, with a level loaded.
    /// @param team Team number, as IPlayer::getTeam reports it.
    /// @param enterOnSpawn True for in-vehicle groups, false for on-foot groups.
    /// @return The lowest matching group id, or -1 when none matches or no spawn manager exists.
    virtual int findSpawnGroup(int team, bool enterOnSpawn) = 0;

    /// One spawn group as listSpawnGroups reports it. Plain data, so the module and the engine
    /// only have to agree on this layout, not on SpawnGroup's.
    struct SpawnGroupInfo
    {
        int id;
        int squad;                  // ISpawnGroup::getSquad; > 0 is a squad group
        int pointCount;             // spawn points in the group
        int enterOnSpawnPoints;     // points with getEnterOnSpawn() or getAIEnterOnSpawn()
        bool active;                // getActive(true): at least one point free for an AI player
        bool onlyForHuman;          // ISpawnGroup::getOnlyForHuman
        float x, y, z;              // ISpawnGroup::getPos, the average of the points
    };

    /// Lists every spawn group the spawn manager has for a team (same team, at least one spawn
    /// point), active or not, in map-key order. Must be called on the main thread.
    /// @param team Team number, as IPlayer::getTeam reports it.
    /// @param out Receives at most capacity entries; may be null when capacity is 0.
    /// @return The number of groups the team has, which can exceed capacity; 0 with no spawn
    ///         manager or no level loaded.
    virtual int listSpawnGroups(int team, SpawnGroupInfo* out, int capacity) = 0;

    /// Restarts the current round without reloading the map, exactly as the admin.restartMap
    /// console command does. Every player is reset and waits to respawn. Must be called on the
    /// main thread.
    /// @return False when no server game logic exists; nothing happens then.
    virtual bool restartRound() = 0;

    /// The game logic's status, as IGameLogic::getGameStatus reports it. Dead players are only
    /// respawned while it is 1 (playing); the module pauses its spawn-fallback timers otherwise.
    /// Must be called on the main thread.
    /// @return The status, or -1 when no game logic exists.
    virtual int getGameStatus() = 0;

    // ---- v2 (openbf2-server-ext docs/protocol.md "v2", R5 in docs/admin-channel-draft.md) ----

    /// One application of damage, as GameLogicDamage::serverGiveDamage hands it to the victim's
    /// armour. Pointers are valid only for the duration of the observer call. Plain data, so the
    /// module and the engine only have to agree on this layout.
    struct DamageInfo
    {
        world::IObject* victim;        // the object owning the armour that takes the damage (a soldier or a vehicle)
        world::IPlayer* attacker;      // credited player after the team filter, or null
        world::IObject* source;        // the projectile or colliding object that caused it, or null
        world::IObject* weapon;        // the attacker's weapon object, or null
        world::IObject* vehicle;       // the attacker's vehicle, or null
        float amount;                  // damage passed to the armour, after friendly-fire and vehicle modifiers
        float hitPointsBefore;         // the armour's hit points just before
        float hitPointsAfter;          // the armour's hit points just after
        float maxHitPoints;            // the armour's maximum hit points
        float position[3];             // world position the caller passed: the projectile's start for a
                                       // projectile hit, the centre for an explosion, 0/0/0 for a bleed
        float direction[3];            // the hit direction the caller passed
        int material;                  // the material index the caller passed
        world::IPlayer* const* killed; // players this application killed (PlayerKilled raised), killedCount long
        int killedCount;
    };

    /// Receives every DamageInfo on the main thread. It must not call back into damage, kill or
    /// object lifetime code, and must return quickly: it runs inside the damage path.
    class IDamageObserver
    {
    public:
        virtual void onDamage(const DamageInfo& info) = 0;

    protected:
        ~IDamageObserver() {}
    };

    /// Registers a damage observer (R5). Read-only: an observer cannot change or veto the damage.
    /// Damage set outside GameLogicDamage::serverGiveDamage (Python setDamage, network replication,
    /// fullDestroy, heals) is not observed. Must be called on the main thread.
    /// @return False when observer is null, already registered, or the observer table is full.
    virtual bool addDamageObserver(IDamageObserver* observer) = 0;

    /// Removes an observer added with addDamageObserver. Must be called on the main thread.
    virtual void removeDamageObserver(IDamageObserver* observer) = 0;

    /// What the engine knows about a projectile beyond IObject (position) and its physics node
    /// (velocity). IProjectileObject has no recovered virtual surface, so the engine reads it.
    struct ProjectileInfo
    {
        bool active;                   // in flight; false once it has hit, expired or been cached
        bool guided;                   // has a seek component (heat, laser, wire or TV guidance)
        int ownerPlayerId;             // the firing player's id, or -1
        int team;                      // the team of the PCO that fired it
        world::IObject* launcherWeapon;   // or null
        world::IObject* launcherVehicle;  // or null
        world::IObject* target;        // the seek component's current target object, or null
        float timeToLive;              // seconds left
    };

    /// Fills info for a projectile object (one answering IID_IProjectileObject). Main thread only.
    /// @return False when object is null or not a projectile; info is unchanged then.
    virtual bool getProjectileInfo(world::IObject* object, ProjectileInfo& info) = 0;

    /// Told about every projectile a weapon launches, bullets included, right after it is
    /// activated and given its velocity. Must return quickly; it runs inside the fire path.
    class IProjectileObserver
    {
    public:
        virtual void onProjectileLaunched(world::IObject* projectile, world::IPlayer* shooter) = 0;

    protected:
        ~IProjectileObserver() {}
    };

    /// @return False when observer is null, already registered, or the table is full.
    virtual bool addProjectileObserver(IProjectileObserver* observer) = 0;
    virtual void removeProjectileObserver(IProjectileObserver* observer) = 0;

    /// One "spotted" radio message as SpottedEvent::executeServer relays it to the spotting
    /// team's HUDs. The server keeps no spotted state; clients show a timed minimap marker.
    struct SpotInfo
    {
        int connectionId;              // the sender's connection, or -1 when posted on the server
        unsigned short spotterNetId;   // networkable id of the spotter's default vehicle (soldier)
        unsigned short targetNetId;    // networkable id of the spotted object
        int team;                      // the spotting team
        float x, z;                    // the 2-D position the spotter sent
        int vehicleType;               // world::VehicleType of the target
    };

    class ISpotObserver
    {
    public:
        virtual void onSpotted(const SpotInfo& info) = 0;

    protected:
        ~ISpotObserver() {}
    };

    /// @return False when observer is null, already registered, or the table is full.
    virtual bool addSpotObserver(ISpotObserver* observer) = 0;
    virtual void removeSpotObserver(ISpotObserver* observer) = 0;

    /// IGameLogic::getBeginRoundState: 0 announce, 1 waiting for players, 2 pre-round countdown,
    /// 3 running. Module-created bots never count as players. @return The state, or -1 with no
    /// game logic.
    virtual int getBeginRoundState() = 0;

    /// IGameLogic::getTimeToNextStateChange in seconds, or -1 with no game logic.
    virtual float getTimeToNextStateChange() = 0;

    // ---- admin channel phase 1 (openbf2-server-ext docs/admin-channel-draft.md, R4 and the
    // appended getters; layouts in docs/findings/admin-phase1.md) ----

    /// One argument of a script event, decoded back from the Python tuple the engine built for
    /// PythonHost::handleEvent. Plain data; every pointer is valid only during the listener call.
    struct ScriptEventArg
    {
        enum Kind { None = 0, Int = 1, Float = 2, String = 3, Player = 4, Object = 5, Tuple = 6 };
        int kind;
        int i;                         // Int: the value (Python bools too); Player: the player id
        double f;                      // Float: the value
        const char* s;                 // String: the bytes, borrowed from the Python string
        int sLength;
        world::IPlayer* player;        // Player: getPlayerFromId(id), may be null
        world::IObject* object;        // Object: null when the wrapped object is gone
        int first;                     // Tuple: the index of its first item in the same array
        int count;                     // Tuple: its item count (items may be tuples again)
    };

    /// Receives every script event the engine dispatches, before Python's handlers run and whether
    /// or not a handler is registered. args[0..argc) are the event's top-level arguments; a Tuple
    /// argument's items follow later in the same array. The listener must only read engine state
    /// and copy what it needs: no Python, no console, no object creation or deletion, no damage or
    /// kills. Main thread only.
    class IScriptEventListener
    {
    public:
        virtual void onScriptEvent(int eventId, const ScriptEventArg* args, int argc) = 0;

    protected:
        ~IScriptEventListener() {}
    };

    /// Sets the single script-event listener (R4); null clears it. Nothing is decoded while no
    /// listener is set. Main thread only.
    /// @return False when another listener is already set.
    virtual bool setScriptEventListener(IScriptEventListener* listener) = 0;

    /// Server settings a module cannot read through a typed interface.
    struct ServerInfo
    {
        char serverName[256];          // sv.serverName, Windows-1252, NUL-terminated (IGameServer::getName is
                                       // the network listener's name, which can be empty)
        int gamePort;                  // sv.serverPort
        int gameSpyPort;               // sv.gameSpyPort
        int maxPlayers;                // sv.maxPlayers, as the server reports it (ServerSettings::getMaxPlayers)
        int timeLimit;                 // sv.timeLimit, seconds; 0 for none
        int roundsPerMap;              // sv.roundsPerMap
        bool internet;                 // sv.internet
        bool ranked;                   // sv.ranked
    };

    /// @return False with no server settings; out is unchanged then.
    virtual bool getServerInfo(ServerInfo& out) = 0;

    /// The current round, from the server game logic and the score manager.
    struct RoundInfo
    {
        int tickets[2];                // teams 1 and 2 (ServerGameLogic::getNumberOfTickets)
        int winner;                    // ScoreManager::getWinner: the last round's winner, 0 if none
        int victoryType;               // ScoreManager::getVictoryType
        float roundTime;               // seconds since the round started, -1 when not started
        int roundsPlayed;              // rounds played on this map
    };

    /// @return False with no server game logic; out is unchanged then. Main thread only.
    virtual bool getRoundInfo(RoundInfo& out) = 0;

    /// IGameLogic::getTeamName. @return An empty string with no game logic.
    virtual std::string getTeamName(int team) = 0;

    /// The GameSpy CD-key hash the server holds for a remote player, as admin.listPlayers shows it
    /// (gcd_getkeyhash). @return An empty string when the server has none: on a LAN server
    /// (sv.internet 0) the GameSpy authenticator never stores one, and bots have none.
    virtual std::string getCdKeyHash(int playerId) = 0;

    /// The reason the server recorded for disconnecting a connection that is about to be torn
    /// down (for example 21 kicked, 10 timed out, 13 refused or dropped). It exists while the
    /// player is destroyed by the network pass, so a PlayerDisconnect listener can read it.
    /// @return The engine's result code, or -1 when none is pending.
    virtual int getDisconnectReason(int playerId) = 0;

    /// One map-rotation entry.
    struct MapListEntry
    {
        char name[64];
        char gameMode[32];
        int size;
    };

    /// Copies the map rotation (maplist.list). @param currentIndex receives the current entry, -1
    /// if none. @return The number of entries, which can exceed capacity.
    virtual int getMapList(MapListEntry* out, int capacity, int& currentIndex) = 0;

    /// Ends the round and loads a map-rotation entry: the next one for -1 (admin.runNextLevel),
    /// else admin.nextLevel index followed by admin.runNextLevel. Main thread only.
    /// @return False with no server game logic or an index outside the rotation.
    virtual bool runMapListEntry(int index) = 0;

    /// Python's bf2.gameLogic.sendGameEvent: a PostRemoteEvent(ECGameLogic, eventId) with a 4-byte
    /// payload to that player's connection. @return False for an event id above 24.
    virtual bool sendGameLogicEvent(int playerId, int eventId, int data) = 0;

    /// Python's sendHudEvent: a PostRemoteEvent(ECHud, eventId). @return False above 78.
    virtual bool sendHudEvent(int playerId, int eventId, int data) = 0;

    /// Python's sendMedalEvent: a MedalEvent to every playing client. @return False with no game.
    virtual bool sendMedalEvent(int playerId, int medal, int value) = 0;

    /// Sets a player's rank (PlayerScoreData::setRank, as Python's score.rank) and, with
    /// sendEvent, first sends the RankEvent Python's sendRankEvent sends.
    /// @return False when the player does not exist.
    virtual bool setRank(int playerId, int rank, bool sendEvent) = 0;

    /// Kills a living player as a death without a killer: ServerGameLogic::killPlayer(player, true,
    /// true), which raises script event 3 (PlayerDeath). Main thread only.
    /// @return False when the player does not exist, is dead, or no server game logic exists.
    virtual bool killPlayer(int playerId) = 0;

    /// [CHOSEN, not recovered] openbf2-server-ext docs/protocol.md R3 (stepped time). Decides how
    /// many 1/30 s ticks each dedicated-server frame runs, instead of the wall clock.
    struct ITickSource
    {
        /// Called once per frame on the main thread. It may wait a few milliseconds for ticks to become
        /// due (in place of the frame sleep that follows a frame without ticks), never longer.
        /// @return The ticks to run this frame, 0 to maxTicks.
        virtual int takeTicks(int maxTicks) = 0;

    protected:
        ~ITickSource() {}
    };

    /// Installs the frame loop's tick source (R3). Set it once at load and keep it for the life of
    /// the server: the stepped clock moves System's wall reference, so the wall clock can't take
    /// over again. Main thread only.
    /// @return False on a client (non-dedicated) image, for null, or when a source is installed.
    virtual bool setTickSource(ITickSource* source) = 0;

    // ---- projectile collisions (openbf2-server-ext docs/findings/tv-hitbox.md) ----

    /// One collision the server reports to a projectile (GenericProjectile::handleCollision), before
    /// its collision component decides between impact, ricochet, penetration and detonation. Tells
    /// which part was touched: damage is booked to the nearest armour up the parent chain, so a hit
    /// on a child part (a rotor, a dummy missile) reads as a hit on the vehicle in DamageInfo.
    /// Pointers are valid only for the duration of the observer call.
    struct ProjectileCollisionInfo
    {
        world::IObject* projectile;    // the projectile
        world::IPlayer* shooter;       // the projectile's player, or null
        world::IObject* other;         // the object part it touched: a vehicle's root (its hull), one of
                                       // its child parts, a soldier or a static object; null for terrain
        float position[3];             // the impact point the projectile records (its position plus the
                                       // collision's push-out vector)
        float normal[3];               // the contact normal
        float speed[3];                // the relative speed the collision passed
        int material;                  // the collision material index
        float latency;                 // seconds of lag compensation the engine uses for this shooter now
                                       // (LatencyCompensation::getLatency); 1/30 s or less rewinds nothing
        bool otherRewound;             // other was tested at its pose latency seconds ago: compensation is
                                       // on, latency is above 1/30 s and other keeps a latency history
                                       // (parentless vehicles and soldiers). Child parts are tested where
                                       // they are now
    };

    /// Receives every ProjectileCollisionInfo on the main thread, server only. It must not call back
    /// into collision, damage or object-lifetime code, and must return quickly: it runs inside the
    /// collision response.
    class IProjectileCollisionObserver
    {
    public:
        virtual void onProjectileCollision(const ProjectileCollisionInfo& info) = 0;

    protected:
        ~IProjectileCollisionObserver() {}
    };

    /// Registers a projectile-collision observer. Read-only. Main thread only.
    /// @return False when observer is null, already registered, or the table is full.
    virtual bool addProjectileCollisionObserver(IProjectileCollisionObserver* observer) = 0;
    virtual void removeProjectileCollisionObserver(IProjectileCollisionObserver* observer) = 0;

    /// How far back, in seconds, the server tests this player's projectiles against other vehicles and
    /// soldiers now: LatencyCompensation::getLatency (ping x pingFactor plus the interpolation time the
    /// client reports, capped by phy-latencyComp-max, or the forced phy-latencyComp-debug; 0 with lag
    /// compensation off; a local player such as a bot gets its local input latency, normally 0).
    /// 1/30 s or less rewinds nothing. Main thread only.
    /// @return The latency, or -1 when the player does not exist.
    virtual float getLagCompensation(int playerId) = 0;

    // ---- R7 client presentation hooks (openbf2-server-ext docs/admin-channel-draft.md "R7") ----

    /// Sends a HUD event (PostRemoteEvent on ECHud) with up to 52 payload bytes to one player, or to
    /// every playing client for -1. Only ids whose retail client handlers read the payload as plain
    /// data are accepted: 40, 41, 42, 43, 44, 51, 64, 65, 66, 70, 71, 74 and 83. The payload is
    /// zero-padded and always sent as 52 bytes, so handlers reading past size see zeros.
    /// Main thread only.
    /// @return False for another id, a size above 52, a null payload with a nonzero size, or a player
    ///         that does not exist.
    virtual bool sendHudEventRaw(int playerId, int eventId, const void* payload, int size) = 0;

    /// Moves the client's round clock and begin-round banner: sends a BeginRoundEvent with the
    /// server's current begin-round state and a next-state-change tick secondsFromNow ahead, to one
    /// player or to every connected client for -1. The server's own state and clock do not change;
    /// its next BeginRoundEvent (a phase change, a join) puts the client's clock back. Main thread only.
    /// @return False without game logic, for a negative or non-finite time, or a player that does
    ///         not exist.
    virtual bool sendRoundClock(int playerId, float secondsFromNow) = 0;

    /// Issues a squad order through the retail SquadManager (issueSquadOrder, or issueCommanderOrder
    /// when commander is set), so the server's squad state, the IssueSquadOrderEvent broadcast and the
    /// members' HUD notices all follow the retail path. orderId: 0 artillery, 1 UAV, 2 vehicle drop,
    /// 3 supply, 5 attack, 6 defend, 7 repair, 8 destroy, 9 mines, 10 attack control point, anything
    /// else move. points are three positions (x, y, z each). Main thread only.
    /// @return False for squad 0, a team other than 1 or 2, a squad that does not exist, or a null
    ///         points array.
    virtual bool issueSquadOrder(int team, int squad, int orderId, unsigned short targetNetId,
                                 const float* points, bool commander) = 0;

protected:
    ~IServerExtension() {}
};

} // namespace hfe
} // namespace dice

#endif // GAME_GAMESERVER_ISERVEREXTENSION_H
