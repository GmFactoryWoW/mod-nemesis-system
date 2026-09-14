# Changelog

## 0.4.1

- Nemesis rank auras are now re-applied immediately after AzerothCore evade/reset processing, preventing a newly promoted Nemesis from losing its visual aura when it returns to its spawn point. - 2026-09-14
- Nemesis promotion aura is now applied synchronously with `AddAura()` when a creature kills a player; the periodic aura refresh is repair-only. - 2026-09-09

### Reward messaging and UI safety

- Corrected revenge messaging so the original Nemesis target is explicitly told when a group member delivered the killing blow instead of receiving text that implies they killed the Nemesis themselves.
- Kept distinct messages for personal revenge, group-assisted revenge, the killing group member, and other eligible participants.
- Restored the original WorldMap `EasyMenu` / `UIDropDownMenuTemplate` integration; the blocked-action issue was unrelated to the map menu.
- Detached the Nemesis target-rank overlay from the protected `TargetFrame` hierarchy while keeping it visually anchored to the target frame, preventing combat-time refreshes after a Nemesis kill from tainting protected Blizzard UI actions.

### AddOn & FrameXML integration

- Kept NemesisTracker on version `0.4.1` while making the client package loadable through either the standard `Interface/AddOns/NemesisTracker` path or an `Interface/FrameXML/NemesisTracker` integration.
- Added a native `Interface > AddOns > NemesisTracker` configuration panel in both loading modes.
- Exposed the existing World Map visibility and low-level Nemesis filtering settings through the Interface Options panel while keeping the World Map quick menu synchronized with the same settings.
- Made addon-owned textures resolve through the active AddOns or FrameXML root so Nemesis map and portrait assets work in either installation mode.
- Preserved `NemesisTrackerDB` persistence in standard addon mode and registered the same database for saving when loaded through FrameXML.
- Kept the same Lua module order for TOC and XML loading so both integration methods initialize the tracker consistently.


### World Map & cache isolation

- Scoped cached Nemesis positions to the current realm so switching realms cannot display positions learned on another realm.
- Kept addon display preferences global while storing Nemesis tracking data separately for each realm.
- Discarded the legacy unscoped position cache instead of assigning potentially foreign data to the current realm.
- Added a volatile session-only fallback when the realm name cannot be resolved, preventing cross-realm cache contamination.

### Server availability & authoritative display

- World Map icons now remain hidden until the current server sends an authoritative Nemesis data stream during the session.
- A server without `mod-nemesis-system` therefore shows no cached Nemesis icons even if data from an earlier session exists.
- Receiving an authoritative bootstrap confirms the server data source even when the realm currently has zero Nemeses, allowing the server to clear stale cached entries correctly.
- Live authoritative updates (`UPSERT`, `REMOVE`, and `MAP_CLEAR`) also validate the server data source before affecting World Map visibility.
- The addon-channel `HELLO_ACK` handshake alone does not authorize cached map icons; actual Nemesis synchronization data is required.

### Gameplay & Nemesis lifecycle

- Apply the Nemesis visual rank aura immediately when an eligible creature kills a player and is promoted, instead of waiting for a later periodic aura refresh.
- Preserve the Nemesis state until player/pet kill reward processing completes, preventing death cleanup from racing reward attribution.

### Rewards & player feedback

- Resolve the actual player responsible for a Nemesis kill through direct attacks and controlled units, including pets, demons, guardians/charmed units, and player-controlled vehicles.
- Added an idempotent per-Nemesis kill reward claim so overlapping death hooks cannot grant rewards more than once for the same death.
- Reset the reward claim only when the same spawn becomes/promotes as a Nemesis again, allowing future Nemesis lifecycles to reward normally.
- Evaluate revenge eligibility per recipient and always prioritize the revenge reward when a player also qualifies for the generic kill/bounty reward, guaranteeing a single reward per eligible player and kill.
- Added distinct role-play reward messages for personal revenge, a group member avenging the Nemesis target, and ordinary bounty kills.
- Personal revenge now explicitly tells the player that they have defeated their own Nemesis.
- When a party member kills another member's Nemesis, the Nemesis target, killer, and other eligible nearby party members receive context-appropriate messages.
- Restrict group Nemesis rewards to members within AzerothCore's normal group reward distance; dead members remain eligible when their corpse is within reward range.
- Exclude distant group members from both reward distribution and level scaling.
- Calculate the shared reward level multiplier from the highest-level player among the actually eligible nearby recipients only.
- Added a role-play reward notification when a player receives an item for defeating a Nemesis, naming both the slain Nemesis and the player being avenged.
- Check inventory capacity before granting the item reward instead of relying on a silent `AddItem` attempt.
- When the reward cannot fit in the player's bags, send the complete item reward by in-game mail and explicitly mention the mail delivery in the reward notification.
- Keep reward item delivery all-or-nothing so a full inventory cannot silently lose part of a configured reward.


## 0.4.0 - 2026-09-07

### World Map & tracking

- Added direct Nemesis tracking on the World Map through the companion addon.
- Added rank-aware Nemesis pins and map tooltips for easier target identification.
- Added support for moving Nemesis creatures: when a tracked creature is loaded, its live world position can be reflected by subsequent map updates.
- Preserved the last persisted Nemesis position as a fallback when the creature is not currently loaded.
- Improved icon positioning while navigating between continents, zones, sub-zones, caves, mines, and interiors.
- Added support for WDM extended maps, including microdungeons such as Jasperlode Mine.
- Generalized WDM support so compatible cave, mine, and interior maps are detected automatically instead of being handled one by one.
- Embedded Astrolabe map geometry used by RareScanner/WDM-style maps to project Nemesis world positions onto custom sub-zone maps.
- Removed the previous hard-coded Jasperlode Mine compatibility mapping.

### Addon synchronization & communication

- Made AzerothCore authoritative for Nemesis state exposed to the companion addon.
- Added full authoritative snapshots on login and periodic refreshes, with live UPSERT, REMOVE, and MAP_CLEAR updates between snapshots.
- Restored immediate live updates when Nemesis creatures enter the world, including after respawn or server restart.
- Switched server/addon communication exclusively to WoW's native addon channel (`LANG_ADDON` / `CHAT_MSG_ADDON`).
- Removed normal chat, system-message, monster-whisper, and peer-to-peer synchronization paths from Nemesis tracking.
- Added a bidirectional `HELLO` / `HELLO_ACK` handshake to verify that addon messages are correctly sent and received in both directions.
- Added chunked addon payloads sized for the WoW 3.3.5 addon-message limit and corrected client-side chunk reassembly for larger snapshots.
- Simplified the location protocol to server-authoritative `MapID`, `X`, and `Y` coordinates only.
- Moved map-coordinate projection to the client so the server no longer needs to transmit derived zone/map coordinates.

### Unit identification & UI integration

- Switched live-unit matching to the full runtime creature GUID used by the WoW client.
- Added a direct `unitGuid -> Nemesis` index for exact target and mouseover identification.
- Improved portrait and tooltip lookups by using exact GUID matching instead of scanning all tracked Nemeses.
- Rebuild and maintain the GUID index across cached startup data, snapshots, UPSERT, REMOVE, and MAP_CLEAR events.
- Kept the World Map menu control isolated from WDM child-frame layout calculations for better addon compatibility.

### Position & persistence model

- Simplified persisted Nemesis locations to `map_id`, `pos_x`, and `pos_y` only.
- Removed obsolete `zone_id` and `pos_z` persistence columns.
- Removed server-side `zoneId`, `areaId`, and `homeZ` state that was no longer required for map tracking.
- Removed obsolete zone-name and decimal-rounding helpers left unused by the simplified coordinate model.
- Removed addon display dependencies on `zoneID` and `areaID`.

### Gameplay & Nemesis lifecycle

- Added periodic self-healing of rank visual auras so living Nemeses recover their expected visual after evade, reset, promotion, reload, or other aura loss.
- Extended rank progression so higher-rank Nemeses can roll additional affixes.
- Added and maintained runtime behavior for Nemesis affixes including Vampiric, Swift, Juggernaut, Savage, Spellward, Enraged, and Regenerating.
- Improved live promotion updates so the companion addon receives new Nemesis state without waiting for a full refresh.
- Removed the temporary CitySiege integration and its temporary-Nemesis path to keep the core Nemesis lifecycle self-contained.

### Rewards & configuration

- Added global minimum and maximum item reward count settings.
- Added optional level-based reward multiplier scaling for item quantities.
- Removed obsolete `RevengeRewardCount` and `BountyRewardCount` settings and their unused reward-count helper.
- Kept base reward quantity controlled by the unified reward item count range.

### Player-facing behavior

- Removed automatic visible chat announcements for Nemesis creation, rank changes, and kills where they were not needed for gameplay.
- Kept Nemesis tracking updates independent from visible chat messages.
- Simplified World Map tooltip presentation to show the Nemesis name, level, and rank with relative-level coloring.

### Compatibility & maintenance

- Refactored the companion addon into separate bootstrap, state/data, communication, lifecycle, and UI components.
- Expanded World Map compatibility for standard world maps and common custom-map layouts.
- Improved interoperability with WDM, RareScanner-style Astrolabe geometry, Questie, and other World Map controls without repositioning their frames.
- Updated the database schema and addon protocol to match the simplified location model.
- Updated README and companion-addon documentation for installation, synchronization, World Map tracking, moving creatures, and WDM sub-zone support.

## 0.3.1 - 2026-04-05

- added `character_nemesis_monthly_kills` tracking so websites can build monthly nemesis kill leaderboards directly from the characters database

## 0.3.0 - 2026-04-04

- fixed companion addon `V2:CHUNK` reassembly so larger bootstrap and upsert payloads are rebuilt correctly client-side
- added non-rank-5 live addon upsert broadcasts after nemesis promotion so trackers receive new sightings sooner
- expanded addon zone and texture mapping for real world map tile rendering across more zones and common subzones
- refactored the companion addon into modular files for core bootstrap, data/state, communication, lifecycle, and UI concerns

## 0.2.0 - 2026-03-22

- reworked the companion addon around an AceDB-backed local cache
- replaced player-safe full snapshot sync with filtered V2 bootstrap flow
- added addon sighting report validation and persisted last-seen nemesis locations
- restricted full addon sync to GM use only
- added peer sync scaffolding for guild, party, raid, and public sharing scopes
- updated tracker UI to show stale state, source, and refresh-based behavior
