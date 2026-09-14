# NemesisTracker client addon

NemesisTracker supports two WoW 3.3.5a integration modes while keeping the same `0.4.1` codebase.

## Standard addon installation

Copy the `NemesisTracker` directory to:

`Interface/AddOns/NemesisTracker/`

The client loads `NemesisTracker.toc`, and `NemesisTrackerDB` is persisted through the normal `## SavedVariables` mechanism.

## FrameXML integration

Copy the same `NemesisTracker` directory to:

`Interface/FrameXML/NemesisTracker/`

Then load `NemesisTracker\NemesisTracker.xml` from the client's FrameXML load list. The XML loads the same Lua modules and embedded Astrolabe library as the addon TOC.

When loaded from FrameXML:

- addon-owned images resolve from `Interface\FrameXML\NemesisTracker\assets\`;
- `NemesisTrackerDB` is registered through `RegisterForSave` when available;
- the same Interface Options category is registered;
- the same server synchronization, World Map, WDM, tooltip, and target portrait features are used.

Do not load both the AddOns and FrameXML copies at the same time. Choose one integration mode.

## Configuration

NemesisTracker registers `Interface > AddOns > NemesisTracker` in both installation modes. The panel exposes:

- `Afficher les Nemesis sur la carte du monde`;
- `Masquer les Nemesis de bas niveau`.

The World Map quick menu controls the same settings, so changes remain synchronized regardless of where they are made.

## Server synchronization

AzerothCore remains the authoritative source of Nemesis data. The addon only sends the addon-channel handshake required to validate communication; Nemesis positions and state are supplied by the server.

WorldMap behavior:

- N1-N4: region/sub-zone/instance maps only;
- N5: region plus continent/world views;
- persistent visibility until the server removes the Nemesis;
- realm-scoped cached positions;
- cached icons remain hidden until the current server sends authoritative Nemesis data;
- 85% normal pin opacity, 100% on hover;
- dynamic pin size by Nemesis rank;
- `nemesis.blp` for N1-N4 and `nemesis-5.blp` for N5.

The addon also shows the Nemesis rank in creature tooltips and a Nemesis marker on the target portrait when the server-provided UnitGUID matches.

Dependencies: Astrolabe 0.4 is embedded. Ace3 is not required.
