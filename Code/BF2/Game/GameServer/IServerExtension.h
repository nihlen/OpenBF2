// [CHOSEN, not recovered] OpenBF2 server-extension surface for native modules. No retail image has
// it. Design record: openbf2-server-ext docs/protocol.md R1 (local design; no upstream issue yet).
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

protected:
    ~IServerExtension() {}
};

} // namespace hfe
} // namespace dice

#endif // GAME_GAMESERVER_ISERVEREXTENSION_H
