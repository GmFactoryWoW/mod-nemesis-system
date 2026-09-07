# NemesisTracker client addon

Copy `NemesisTracker` into `Interface/AddOns/`, or include `NemesisTracker.xml` from FrameXML.

The addon is receive-only: AzerothCore is the single source of truth. The server sends authoritative snapshots and live UPSERT/REMOVE/MAP_CLEAR events; the addon never reports sightings, requests syncs, or peer-syncs with other clients.

WorldMap behavior:
- N1-N4: region/sub-zone/instance maps only
- N5: region plus continent/world views
- persistent visibility until the server removes the Nemesis
- 85% normal pin opacity, 100% on hover
- dynamic pin size by Nemesis rank
- `nemesis.blp` for N1-N4 and `nemesis-5.blp` for N5
- menu option `Afficher les Némésis` persisted in `NemesisTrackerDB`

The addon also shows the Nemesis rank in creature tooltips and a Nemesis marker on the target portrait when the server-provided UnitGUID matches.

Dependencies: Astrolabe 0.4 is embedded. Ace3 is not required.
