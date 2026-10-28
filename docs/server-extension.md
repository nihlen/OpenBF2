# Server extension (`IServerExtension`)

`IServerExtension` is a native interface that the dedicated server publishes for an out-of-tree module,
`libserverext.so` from the openbf2-server-ext repository (`C:\Data\Projects\openbf2-server-ext`). The module gives
external tools a typed interface to the running game: a bot channel for a bot sidecar and an admin channel for
BF2WebAdmin. No retail image has any of this. Every part is `[CHOSEN, not recovered]` and carries that marker in
the source.

These changes are local only. They live in local commits on branch `serverext/r1`, not merged into `main` and never pushed: `9e1cda6`, `540fa88`,
`74e18d9`, `873d1b1` and the R3 commit after them, each marked "local, not for upstream". There is no upstream issue for them. The design
record is in the module's contracts, `docs/protocol.md` (R1, R1b, v1, v2) and `docs/admin-channel-draft.md` (R4, R5).
How the engine, the module, the sidecar and WebAdmin fit together, and how to build and test them, is in that repo's
`docs/system.md`.

[AGENTS.md](../AGENTS.md) applies to these changes as to any other: keep them small, keep them marked, and don't open
pull requests.

## How a module gets the interface

- `GameServer::init` registers one adapter object as the named singleton `"OpenBF2ServerExtension"`
  (`Game/GameServer/GameServer.cpp`, the R1 block before the mod's `openbf2.con` runs). It uses flag 4, so the
  object is borrowed and ClassManager does not own it. `GameServer::shutdown` unregisters it.
- The mod's `openbf2.con` holds `openbf2.addModDll serverext` (see [architecture.md](architecture.md), "The
  `openbf2` console extension"). The module's `initDll` fetches the singleton with
  `reinterpret_cast<IServerExtension*>(classManager->getSingleton("OpenBF2ServerExtension"))`.
- The adapter is `GameServer::ServerExtension`, defined in `GameServer.cpp` and declared in `GameServer.h` as a
  nested class with no data members. GameServer's layout and the retail vtables (`IGame`, `IGameServer` and the
  others) are unchanged.

## The interface (`Game/GameServer/IServerExtension.h`)

The interface is header-only and is not an `IBase`. Its structs are plain data, so the module and the engine only
have to agree on those layouts. Methods may be appended. Never reorder or remove one: the module is compiled against
this header and calls through its vtable. Every method must be called on the main thread.

| Group | Methods | Used for |
|---|---|---|
| Bots (R1, v1) | `createInputPlayer`, `findSpawnGroup`, `listSpawnGroups` (`SpawnGroupInfo`), `restartRound`, `getGameStatus` | Creating a connection-less, AI-flagged player whose input the module writes, without the AI library. Finding the team's spawn groups for the spawn fallback. Round restarts. |
| Observers (v2, R5) | `addDamageObserver`/`removeDamageObserver` (`DamageInfo`), `addProjectileObserver`/`remove…`, `getProjectileInfo` (`ProjectileInfo`), `addSpotObserver`/`remove…` (`SpotInfo`), `getBeginRoundState`, `getTimeToNextStateChange` | Read-only notification of damage, projectile launches and spotted messages, plus the round's begin state. They feed the bot channel's `Damage`, `Kill`, `ProjectileList` and `RoundState`, and the admin channel's events. |
| Projectile collisions | `addProjectileCollisionObserver`/`remove…` (`ProjectileCollisionInfo`) | Read-only notification of every collision the server hands to a projectile: the object part touched (a vehicle's hull, a child part such as a rotor or a dummy missile, a soldier; null for terrain), the impact point, normal, relative speed and material, the shooter's lag-compensation latency, and whether the part was tested at its rewound pose. Damage is booked to the nearest armour up the parent chain, so `DamageInfo` cannot tell the parts apart. Design and measurements: openbf2-server-ext `docs/findings/tv-hitbox.md`. Not used by the module yet. |
| Stepped time (R3) | `setTickSource` (`ITickSource`) | Bot tests faster than real time: the module decides each dedicated-server frame's ticks (`serverext.cfg stepMode 1`, protocol v4 `Step`). Set once at load; the retail wall-clock loop runs when none is installed. |
| Admin channel (R4 and getters) | `setScriptEventListener` (`ScriptEventArg`), `getServerInfo`, `getRoundInfo`, `getTeamName`, `getCdKeyHash`, `getDisconnectReason`, `getMapList`, `runMapListEntry`, `sendGameLogicEvent`, `sendHudEvent`, `sendMedalEvent`, `setRank`, `killPlayer` | Every script event (the Python game events), decoded for the module, and the server state and commands BF2WebAdmin used to reach through `mm_webadmin.py`. |

Observers cannot change or veto anything. They must return quickly and must not call back into damage, kill,
object-lifetime or Python code.

## Where the engine calls out

`Game/GameServer/ServerExtensionHooks.h` declares the observer tables: `ObserverTable<T>`, a fixed list of up to
4 borrowed pointers, notified in registration order. Each table is defined beside its only notify site, so every
image that links the site also links the table. The adapter forwards `add…`/`remove…` calls to these tables.

| Hook | Site |
|---|---|
| Damage observers | `GameLogicDamage::serverGiveDamage` (`Game/GameLogic/GameLogicDamage.cpp`). It records the players each application kills and reports the hit points before and after. |
| Projectile observers | `FireComp::setupProjectile` (`Game/Objects/Weapons/FireComponents/FireComp.cpp`), right after a projectile is activated and given its velocity |
| Projectile-collision observers | `GenericProjectile::handleCollision` (`Game/Objects/Projectiles/GenericProjectile.cpp`), server only, after the impact point is recorded and before the collision component acts; past the method's two early returns (a collision already handled, a tracer copy) |
| Spot observers | `SpottedEvent::executeServer` (`Game/GameEvents/SpottedEvent.cpp`) |
| Script-event listener (R4) | `PythonHost::handleEvent` (`Game/Python/PythonHost.cpp`, `notifyScriptEventListener`). It runs before the begin-round gate and before Python's handlers, whether or not a handler is registered. |
| Round info | inline, non-virtual accessors in `Game/GameLogic/ServerGameLogic.h` (`getServerExtRoundStartTick`, `getServerExtRoundsPlayed`); the class layout is unchanged |
| Tick source (R3) | `BF2Engine::mainLoop` (`Game/Main/BF2Engine.cpp`): with a source, the frame runs its ticks, advances the exact clock by exactly those (`System::advanceExactTime(unsigned)`, the demo recorder's fixed step) and skips the 30 Hz limiter; a frame without ticks sleeps 1 ms. The source pointer is defined there (`serverExtensionHooks::tickSource`). |
| Ungated player creation | `GameServer::createPlayerUngated` (`GameServer.cpp`): the retail `createPlayer` body after its AI-library gate, split out unchanged. `createPlayer` behaves as before. |

There are three R1b guards. Retail has no null check on the AI-player courier, and without the AI library that
courier is null. A null check is therefore added where an AI-flagged player can reach it:

- `ServerGameLogic::handleCollisionForResurrecter` (`Game/GameLogic/ServerGameLogic.cpp`)
- `BFfindEntryPoint` (`Game/Objects/Game/PlayerControlObjectHelpers.cpp`)
- `GameServer::sendMissileMessageToOccupyingPlayers` (`GameServer.cpp`)

None of the three has run at runtime yet.

## Changing it

- Find every site with `rg "openbf2-server-ext"` in `Code/BF2`.
- Change the module's contract first. A change to the engine surface is an R-item in openbf2-server-ext
  `docs/protocol.md` or `docs/admin-channel-draft.md`. The author approves it before it is implemented.
- Append methods and struct fields only where the module is rebuilt against the new header in the same step. The
  localtest image builds engine and module from the same pair of commits for this reason.
- Validate as [development.md](development.md) requires for a shared header. Then build the module against the
  checkout (`build-module.sh` in openbf2-server-ext) and run it on a server with representative content. A compile
  proves neither dispatch nor gameplay.
