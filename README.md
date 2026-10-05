# Nemesis System Module

![Nemesis System Banner](doc/assets/nemesis-banner.svg)

## Overview

`mod-nemesis-system` turns selected open-world PvE deaths into persistent revenge targets.
When an eligible creature kills a player, the creature is promoted into a Nemesis,
gains rank-based scaling, and is persisted in the characters database so the state
survives creature unloads and server restarts.


The current implementation includes:

- player-death trigger using `OnPlayerKilledByCreature`
- persistent `character_nemesis` storage in the characters database
- rank-based size, health, and melee/ranged damage scaling
- affix rolling with runtime behavior hooks
- configurable rank-based visual aura support for active nemeses
- re-application on `OnCreatureAddWorld`
- cleanup when a tracked nemesis dies
- decay for stale nemesis records
- direct revenge and bounty rewards on kill
- GM commands for testing and state control
- RP promotion messages sent only to the player killed by the promoting Nemesis
- anti-feed cooldowns for repeated promotions and same-victim farming
- companion addon transport over the native WoW addon channel for live nemesis tracking
- World Map Nemesis pins projected from server-authoritative `MapID`, `X`, `Y` coordinates
- WDM/RareScanner-style support for cave, mine, and other sub-zone maps through embedded Astrolabe geometry

![Nemesis System Emblem](doc/assets/nemesis-emblem.svg)

## Files

- `src/NemesisSystem.cpp`: initial gameplay and persistence logic
- `src/nemesis_system_loader.cpp`: module loader entrypoint
- `conf/mod_nemesis_system.conf.dist`: module configuration
- `data/sql/db-characters/base/nemesis_system.sql`: characters database schema
- `doc/companion-addon-spec.md`: companion addon design and message contract
- `ClientAddon/NemesisTracker/`: WoW 3.3.5a addon scaffold

## Installation

1. Build AzerothCore with the module enabled.
2. Import `data/sql/db-characters/base/nemesis_system.sql` into the characters database.
3. Copy `conf/mod_nemesis_system.conf.dist` to your server config directory if needed.
4. Restart `worldserver`.

## Current Behavior

- Nemeses only spawn from non-instance, non-battleground, non-raid kills.
- Only DB-backed creature spawns are eligible.
- Critters, pets, dungeon bosses, world bosses, and sanctuary deaths are excluded.
- Creature eligibility is configurable by absolute creature level, rank type, and player-versus-creature level windows.
- Initial ranks affect size, health, and weapon damage.
- Rank 1 rolls one affix. Rank 3+ rolls a second affix.
- Implemented affixes: `Vampiric`, `Swift`, `Juggernaut`, `Savage`, `Spellward`, `Enraged`, `Regenerating`.
- Rank 5+ rolls a third affix.
- Active nemeses also carry a configurable rank-based visual aura by default.

Additional affix behavior:

- `Enraged`: gains bonus damage below a configurable health threshold.
- `Regenerating`: restores health periodically while damaged.
- The original victim is stored as the current nemesis target.
- Base creature stats are persisted and protected by a stable per-spawn runtime snapshot so rank scaling cannot compound across deaths, respawns, clears, expiration, reloads, or server restarts.

## Eligibility Config

- `NemesisSystem.MinCreatureLevel`
- `NemesisSystem.MaxCreatureLevel`
- `NemesisSystem.AllowNormal`
- `NemesisSystem.AllowElite`
- `NemesisSystem.AllowRare`
- `NemesisSystem.AllowRareElite`
- `NemesisSystem.AllowWorldBoss`
- `NemesisSystem.PromotionLevelDiffMax`
- `NemesisSystem.TrivialKillLevelDelta`
- `NemesisSystem.VisualAuraSpell`
- `NemesisSystem.VisualAuraSpellRank1`
- `NemesisSystem.VisualAuraSpellRank2`
- `NemesisSystem.VisualAuraSpellRank3`
- `NemesisSystem.VisualAuraSpellRank4`
- `NemesisSystem.VisualAuraSpellRank5`

Default visual ladder:

- Rank 1: shield visual level 1
- Rank 2: shield visual level 2
- Rank 3: shield visual level 3
- Rank 4: shield visual level 3 + static lightning visual
- Rank 5+: shield visual level 3 + Thaddius lightning visual

## Anti-Feed Config

- `NemesisSystem.RankUpCooldownSeconds`
- `NemesisSystem.SameVictimCooldownSeconds`

Anti-feed cooldown state is now persisted with each nemesis record, so cooldowns survive server restarts.

## Rewards

- Revenge reward: granted when the original nemesis target or a member of their party kills the nemesis.
- Bounty reward: granted to other players who kill the nemesis.
- Rewards are configurable as direct item and gold grants.
- Item and gold rewards scale upward by nemesis rank.
- Rewards are granted to every eligible nearby party member, using AzerothCore's group reward distance.
- Reward scaling is based on the highest level among eligible nearby recipients.
- Overleveled kills scale rewards down linearly to zero.
- Underdog kills scale rewards up linearly to a configurable maximum multiplier.
- Gold always uses the level-based reward multiplier. Item quantity scaling can be enabled or disabled independently.
- `RewardItemCountMin/Max` define the random base item quantity; optional rank and level multipliers are applied afterwards.
- Automatic create/rank-up/kill chat announcements are disabled; technical addon map updates are sent separately and remain real-time.

Reward scaling config:

- `NemesisSystem.RewardOverlevelDiffMax`
- `NemesisSystem.RewardUnderlevelDiffMax`
- `NemesisSystem.RewardUnderdogMaxMultiplier`
- `NemesisSystem.RewardItemCountMin`
- `NemesisSystem.RewardItemCountMax`
- `NemesisSystem.RewardApplyMultiplierToItemCount`
- `NemesisSystem.RewardApplyRankMultiplierToItemCount`


## GM Commands

- `.nemesis debug`: inspect the selected creature
- `.nemesis info <spawnId>`: inspect a nemesis directly by spawn id
- `.nemesis mark [rank]`: create or set a nemesis on the selected creature
- `.nemesis reroll`: reroll affixes on the selected nemesis
- `.nemesis list`: list active nemeses on the current map
- `.nemesis clear`: clear the selected creature's nemesis state
- `.nemesis mapclear`: clear all active nemeses on the current map
- `.nemesis clearall`: clear all stored nemesis records
- `.nemesis reload`: reload module config

## Companion Addon

The module includes the WoW 3.3.5a addon `ClientAddon/NemesisTracker/`. The same 0.4.1 client code can be loaded as a standard addon from `Interface/AddOns/NemesisTracker/` or integrated from `Interface/FrameXML/NemesisTracker/` through `NemesisTracker.xml`. In FrameXML mode, addon assets automatically resolve from the FrameXML root and `NemesisTrackerDB` is registered for persistence through `RegisterForSave` when available. Do not load both integration modes simultaneously.

NemesisTracker registers a native `Interface > AddOns > NemesisTracker` configuration category in both modes. The panel controls World Map visibility and low-level Nemesis filtering, using the same settings as the quick menu on the World Map.

NemesisTracker has no standalone tracker window. It renders server-authoritative Nemesis pins directly on the Blizzard World Map. N1-N4 remain restricted to detailed region/sub-zone maps; N5 can also be displayed on continent/world views.

### Addon communication

Communication is performed exclusively through the native WoW addon channel:

- client to server: `SendAddonMessage` / `LANG_ADDON`
- server to client: `LANG_ADDON` / `CHAT_MSG_ADDON`
- `HELLO` -> `HELLO_ACK` handshake validates bidirectional communication
- full snapshots are sent on initialization and periodically
- live `UPSERT`, `REMOVE`, and `MAP_CLEAR` messages keep client state synchronized
- large payloads are chunked below the WoW 3.3.5 addon-message size limit

Normal system messages and monster whispers are not used as a transport.

### Position model

The map protocol intentionally uses only:

- `MapID`
- world `X`
- world `Y`

`zoneID`, `areaID`, `pos_z`, zone names, and precomputed map coordinates are not required for pin placement. The client derives the displayed position from the world coordinates and Astrolabe geometry.

For a loaded Nemesis creature, a snapshot can use its current live `X/Y` position. When the creature is not loaded, the persisted position is used as the fallback. Consequently, a roaming creature's icon can move between snapshots without a rank change.

### WDM sub-zone maps

NemesisTracker supports the additional cave, mine, and interior maps introduced by WDM. The implementation follows the same principle used by RareScanner: the embedded Astrolabe dataset contains geometry for the WDM micro-maps and the addon identifies the currently displayed map through `GetMapInfo()`.

This allows a Nemesis pin to be projected directly on maps such as Jasperlode Mine instead of being limited to the parent Elwynn Forest map. No hard-coded `zoneID` or `areaID` mapping is required for these sub-zones.

Current addon file layout:

- `Bootstrap.lua`: AddOns/FrameXML path and persistence bootstrap
- `Core.lua`: shared state plus native event/timer helpers
- `Data.lua`: server-authoritative visibility helpers
- `Comm.lua`: addon-channel transport, payload parsing and chunk reassembly
- `Lifecycle.lua`: SavedVariables, slash command and event lifecycle
- `MapData.lua`: map metadata used by projection helpers
- `WorldMap.lua`: WorldMap pins, Astrolabe projection, WDM sub-zone handling, tooltips and map menu
- `UnitUI.lua`: target/mouseover Nemesis identification, unit tooltip rank and target portrait marker
- `Libs/Astrolabe/`: embedded Astrolabe runtime and WDM-compatible map geometry

## Next Steps

1. Add additional affixes and spell-driven visuals.
2. Add richer reward presentation and optional reward messaging.
3. Integrate with optional autobalance hooks.

## Branding Assets

![Nemesis System Icon](doc/assets/nemesis-icon.svg)

![Nemesis System Promo](doc/assets/nemesis-promo.svg)
