# Server extension (`IServerExtension`)

`IServerExtension` is a native interface that the dedicated server publishes for an out-of-tree module,
`libserverext.so` from [openbf2-server-ext](https://github.com/nihlen/openbf2-server-ext). The module gives
external tools a typed interface to the running game: a bot channel for a bot sidecar and an admin channel for
BF2WebAdmin. No retail image has any of this. Every part is `[CHOSEN, not recovered]` and carries that marker in
the source.

These changes are not upstream's. They live on branch `serverext/r1` of the fork
[nihlen/OpenBF2](https://github.com/nihlen/OpenBF2/tree/serverext/r1), in commits marked "local, not for upstream",
and are not merged into `main`. There is no upstream issue for them. The design record is in the module's documents:
`docs/protocol.md` ("Engine requirements": R1, R1b, R2, R3) and `docs/admin-channel.md` ("Engine interface": R4, R5,
R7). The module's README covers how to build it against this branch and how to make a release.

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
| Bots (R1) | `createInputPlayer`, `findSpawnGroup`, `listSpawnGroups` (`SpawnGroupInfo`), `restartRound`, `getGameStatus` | Creating a connection-less, AI-flagged player whose input the module writes, without the AI library. Finding the team's spawn groups for the spawn fallback. Round restarts. |
| Observers (R5) | `addDamageObserver`/`removeDamageObserver` (`DamageInfo`), `addProjectileObserver`/`remove…`, `getProjectileInfo` (`ProjectileInfo`), `addSpotObserver`/`remove…` (`SpotInfo`), `getBeginRoundState`, `getTimeToNextStateChange` | Read-only notification of damage, projectile launches and spotted messages, plus the round's begin state. They feed the bot channel's `Damage`, `Kill`, `ProjectileList` and `RoundState`, and the admin channel's events. |
| Projectile collisions | `addProjectileCollisionObserver`/`remove…` (`ProjectileCollisionInfo`), `getLagCompensation` | Read-only notification of every collision the server hands to a projectile: the object part touched (a vehicle's hull, a child part such as a rotor or a dummy missile, a soldier; null for terrain), the impact point, normal, relative speed and material, the shooter's lag-compensation latency, and whether the part was tested at its rewound pose. Damage is booked to the nearest armour up the parent chain, so `DamageInfo` cannot tell the parts apart. `getLagCompensation` returns a player's current lag compensation (`LatencyCompensation::getLatency`). They feed the admin channel's lag in `PlayerState`, `ProjectileCollision` and `ProjectileTrace`. The wire format and how the server decides a hit: openbf2-server-ext `docs/admin-channel.md`, "Hit/miss replay". |
| Stepped time (R3) | `setTickSource` (`ITickSource`) | Bot tests faster than real time: the module decides each dedicated-server frame's ticks (`serverext.cfg stepMode 1`, protocol v4 `Step`). Set once at load; the retail wall-clock loop runs when none is installed. |
| Client presentation (R7) | `sendHudEventRaw`, `sendRoundClock`, `issueSquadOrder` | HUD features the retail client already has: a whitelisted HUD event (`PostRemoteEvent` on ECHud) with up to 52 payload bytes (map markers, notices, help hints), a `BeginRoundEvent` that moves the client's round clock and banner without touching the server's state, and squad or commander orders through the retail `SquadManager`. They feed the admin channel's `HudEventRaw`, `RoundClock` and `SquadOrder`. |
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

## Local gameplay settings

These are local `serverext/r1` changes outside `IServerExtension`, `[CHOSEN, not recovered]`. Each is an engine
variable whose default keeps retail behaviour; set it with `vars.set <name> <value>` (console, rcon or a `.con` file).

| Variable | Site | Effect |
|---|---|---|
| `tv-aim-from-missile` (float, default 0) | `LPTargetComp::updateTargetObject` (`Game/Objects/Weapons/TargetComponents/LPTargetComp.cpp`) | The client's camera trails the server's missile by its lag, so the retail aim point (the first hit of the client's ray) can lie beside or behind the server's missile near a wall, and `DefaultFollowComp` stops turning. Above 0, the retail placement runs first; only when its aim point is less than two ticks of travel ahead of the missile is the ray cast again, in the client's direction from the server's missile. This applies only while the launcher watches the missile's camera and the client's eye lies within that many metres of the missile; a direction from farther away (a bot aiming from its vehicle) keeps the retail placement. In that case the target is placed twice in one update, so a lock warning can change twice. Tested with bots on a WSL server (at 50 m); a human client is untested. |
| `tv-turn-behind` (int, default 0) | `DefaultFollowComp::followTarget` (`Game/Objects/Projectiles/FollowComponents/DefaultFollowComp.cpp`) | Retail turns a missile only towards an aim point in front of it (local z > 0); a TV gunner who looks more than 90° away from the missile's nose gets a missile that flies straight until the look comes back. Non-zero keeps turning towards such a point at the same per-tick caps (`follow.maxYaw`/`maxPitch`, 1.5° for `agm114_hellfire_tv`), measuring its horizontal angle past 90°. Only for a missile from a TV-guided launcher (`TSTVGuided`) while the launcher watches the missile's camera; a missile nobody watches keeps the retail rule, so it does not turn back to its last aim point. Tested with bots on a WSL server: a look 110° to 150° away turns the missile at the caps (retail: no turn); turns in front are unchanged. A human client is untested. |
| `tv-keep-aim-on-reload` (int, default 0) | `LPTargetComp::reload` (`Game/Objects/Weapons/TargetComponents/LPTargetComp.cpp`) | A launcher's reload clears its target lock, which switches the aim point off (`GenericFireArm::doReload` calls every component's `reload`). A TV launcher reloads about a second after its shot, so the missile in flight stops turning until the next `TargetDirectionEvent` places the aim point again: two or three ticks with a client that steers, for good with one that does not. Non-zero leaves the aim point as it is while the launcher watches its missile's camera; a missile nobody watches keeps the retail behaviour. Seen in a trace of a retail client's flights on a live server (`BF2_TRACE_TV_MISSILES`): no rotation change in ticks 31 to 33 after every launch. Tested with bots on a WSL server (openbf2-server-ext `serverext_client.py --scenario tvreload`). A human client is untested. |
| `tv-input-queue` (int, default 0, at most 8) | `GameServer::simulatePlayerUpdate`, `GameServer::clearPlayerActions` (`Game/GameServer/GameServer.cpp`), `GenericProjectile::handleUpdate` | A client sets its clock to the tick of each control-object state plus the number of its own actions the server has not consumed yet (`GameClient::handleControlObjectState`), and draws remote objects at that clock minus its interpolation time. Every action that waits in the player's `ActionBuffer` therefore moves the player's view one tick closer to the present; retail leaves that depth to chance, from 0 to the 4 that `clearPlayerActions` keeps. Above 0, while a player's TV missile flies with its camera on, an update that finds no more than this many actions waiting replays the current action instead of consuming one (what retail does with an empty queue), at most that many updates in a row; a queue whose smallest depth over 15 updates is above the target loses its oldest action unexecuted, as the retail trim drops them; and the trim limit is the target plus 4. A `TargetDirectionEvent` is applied on arrival, so steering is not delayed by the queue; the gunner's other inputs are, for as long as the hold lasts. When the flight ends the retail limit of 4 returns, and the same rule brings the queue back to the depth it had when the flight began, one action per 15 updates. Measured with a retail client on 2026-11-16 (four flights at 3): the hold kept 3 to 4 actions waiting, and in the first flight the view was 3 ticks newer, tick for tick as described above. It does not last. The retail client sets its interpolation time anew after every 8 rounds of 32 delivered packets (about 8.5 s) to its average receive interval plus one tick for each action it holds unacknowledged, averaged over 8 samples (the auto-ping arm of `ClientConnection::handlePacketStatusNotification`, which this tree does not implement; the retail ELF initialises `m_useAutoPingAdjust` to 1). The next three flights had lost about half of the gain, and the client's interpolation time was 45 to 70 ms higher at the start of each of them, so between flights, with the queue back at its old depth, its view is that much older than without the setting. The setting cannot keep the view lag down; it remains as a measuring aid. It cannot run with bots, which have no `ActionBuffer`. |

## Changing it

- Find every site with `rg "openbf2-server-ext"` in `Code/BF2`.
- Change the module's contract first. A change to the engine surface is an R-item in openbf2-server-ext
  `docs/protocol.md` ("Engine requirements") or `docs/admin-channel.md` ("Engine interface"). The author approves it
  before it is implemented.
- Append methods and struct fields only where the module is rebuilt against the new header in the same step. The
  module's release build (`release/build.sh`) builds engine and module from the same pair of commits for this reason.
- Validate as [development.md](development.md) requires for a shared header. Then build the module against the
  checkout (`build-module.sh` in openbf2-server-ext) and run it on a server with representative content. A compile
  proves neither dispatch nor gameplay.
