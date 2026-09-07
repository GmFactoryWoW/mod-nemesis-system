# Changelog

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
