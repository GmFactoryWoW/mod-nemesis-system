# NemesisTracker companion protocol

## Authority model

The server remains authoritative for Nemesis state. The addon only sends a transport handshake (`HELLO`) to confirm that the native addon channel is working and to request a fresh authoritative bootstrap.

## Transport

Communication uses the native WoW addon channel exclusively.

- Client -> core: `SendAddonMessage("Nemesis", payload, "WHISPER", UnitName("player"))`
- Core -> client: `SMSG_MESSAGECHAT` with `CHAT_MSG_WHISPER`, `LANG_ADDON` and addon prefix `Nemesis`
- Client receive event: `CHAT_MSG_ADDON(prefix, payload, "WHISPER", sender)`

No `CHAT_MSG_SYSTEM`, normal whisper, monster whisper, or visible chat transport is used.

At addon startup the client sends `V5:HELLO`. AzerothCore receives it through `PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE`, replies with `V5:HELLO_ACK`, then sends a fresh bootstrap. The addon sets `NemesisTracker.data.addonTransportVerified = true` after receiving the ACK.

Long payloads are fragmented as:

`V5:CHUNK:<chunkId>:<part>:<total>:<payload>`

## Snapshot

On player login, map changes, periodic snapshots, and a successful `HELLO`, the server sends:

- `V5:BOOTSTRAP_BEGIN:<count>:<serverTime>`
- one `V5:BOOTSTRAP_ENTRY:...` per active Nemesis
- `V5:BOOTSTRAP_END`

`BOOTSTRAP_BEGIN` clears the client cache before rebuilding it, so the snapshot is authoritative.

## Live updates

- `V5:UPSERT_VALIDATED:<entry fields>` for creation, rank changes, and authoritative updates
- `V5:REMOVE:<spawnId>:<reason>` when a Nemesis ceases to exist
- `V5:MAP_CLEAR:<mapId>` after an authoritative map purge

## Entry fields

After the opcode, fields are:

`spawnId:creatureEntry:unitGuid:name:mapId:x:y:level:rank:rankTier:affixMask:affixText:targetGuid:targetName:relation:rewardClass:threatClass`

The location contract is strictly `mapId + x + y`. No `zoneId`, `areaId`, `zoneName`, `z`, `mapX`, or `mapY` is transmitted. The addon projects world coordinates into Astrolabe coordinates locally.

`spawnId` is the persistent creature spawn identifier. `unitGuid` is the exact 3.3.5 client UnitGUID string constructed from creature entry + spawn counter and is required for target/mouseover matching.
