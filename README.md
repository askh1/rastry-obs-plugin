# Rastry for OBS

The OBS plugin RastryControl ships and installs on request: what RastryControl needs from OBS that obs-websocket does not offer. It opens nothing on screen; everything is data in, data out, as obs-websocket vendor requests of the vendor `rastry` (`CallVendorRequest`).

Version 0.3.0, built against the OBS 31.1.1 SDK (`buildspec.json`), Windows x64. Licensed under the GNU GPL, version 2 or later (`LICENSE`), like OBS itself.

## Requests

| Request | File | What |
|---|---|---|
| `GetVersion` | `requests.c` | `{ version, api, obsVersion }`, so RastryControl can update it |
| `GetProperties`, `PressPropertyButton` | `properties.c` | A source's exact property form, and its buttons |
| `GetSceneItemExtras`, `SetSceneItemScaleFilter`, `SetSceneItemTransition`, `GetTransitionKinds` | `sceneitems.c` | Scale filtering and show / hide transitions per scene item |
| `CreateGroup` | `sceneitems.c` | Puts sources of a scene into a new group (obs-websocket cannot) |
| `InteractMouse`, `InteractKey` | `interact.c` | Interacting with browser sources |
| `GetPreviewEncoders`, `StartPreviewFeed`, `StopPreviewFeed` | `feed.c` | The moderators' preview: a scene rendered by OBS and encoded on the GPU by the best encoder the PC really has, over a named pipe only this Windows user can open; it stops by itself when nobody reads it |

## Build (Windows)

Visual Studio 2022 with C++ and CMake 3.30 or later:

```bash
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

The first configure downloads the OBS sources and their dependencies (`.deps`, regenerated when deleted).

## Install

RastryControl copies it to `%ProgramData%\obs-studio\plugins\rastry-obs\` (`bin\64bit`, `data`) when the streamer asks for it, and updates it with OBS open. By hand: copy the built `rastry-obs.dll` and `data/` to that folder and restart OBS. Its log lines start with `[rastry-obs]`.

## Source of the copy in RastryControl

RastryControl (Microsoft Store) carries a built copy of this plugin. Its source is this repository: each version RastryControl ships is tagged here (`v0.3.0` and later), and building that tag with the steps above gives it. The plugin talks to RastryControl only through obs-websocket and its own named pipe; RastryControl is a separate program, not covered by this license.

Questions: support@rastry.com
