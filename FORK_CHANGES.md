# Fork changes

This fork ([Crow74/frontend-sdl-cpp](https://github.com/Crow74/frontend-sdl-cpp), branch
`ambiviz-features`) adds a set of DJ/VJ-oriented features on top of stock projectMSDL:
favorites, playlists, a preset browser, a slow-motion "ambient" mode, and a media
transport bar. It was built with AI assistance (Claude, Anthropic) — I don't write C++
myself, so please ask if anything about the implementation is unclear rather than
assuming intent that isn't there.

Everything below is additive; no existing stock behavior, settings, or file formats
were removed or repurposed.

## New features

### Media bar

A bottom transport bar (Prev / Play-Pause / Next / Random / Shuffle / Favorite) for
mouse-driven control without opening a menu. Like the rest of the UI, it's hidden until **Escape** shows the UI/menu.

### Favorites

Press **f** (or the star button in the Preset Browser / media bar) to favorite the
current preset. Favorites are stored in `projectMSDL.favorites.json` next to your
other projectM settings and can be cycled as their own scope (see below), browsed in
the Preset Browser, and exported/imported.

### Playlists

Create multiple named playlists, add presets to them from the Preset Browser, and
play through a specific playlist as its own cycle scope. Stored in
`projectMSDL.playlists.json`.

### Preset Browser

A new window (**b** to open) with three tabs:

- **Browse** — the full preset tree, single-click to select, double-click to play.
- **Favorites** — your starred presets.
- **Playlists** — manage and play your playlists.

### Cycle scope

Both the Preset Browser and Settings expose a **Cycle Scope** control:
**All** / **Folder** / **Favorites** / **Playlist** — determines what pool of presets
`n`/`p`/random/auto-advance draw from. Defaults to **All**, matching stock behavior.

### Favorites & playlist import/export

Export/import buttons on the Favorites and Playlists tabs. Favorites export as a
single file (merged and de-duplicated on import); playlists export one file each
(named after the imported filename, auto-suffixed on a name collision). Imported
preset paths — which are absolute and machine-specific — are re-linked by filename
against your current preset tree, using the same logic that already heals presets
after they've been moved or rescanned. Both dialogs remember their last-used
directory across restarts.

### Ambient Speed

A new **Ambient Speed** dial (Settings, range 0.1–1.0, default 1.0 = stock behavior)
slows preset motion (zoom/warp/rotation) and audio-reactivity smoothing
(bass/mid/treb attack-decay) down together, uniformly, without editing any preset
files. It works by dilating projectM's internal preset clock rather than scaling
individual preset parameters. Display/Transition/Hard Cut Duration keep meaning real
seconds regardless of the setting — they're automatically compensated.

This required `projectm_set_frame_time()`, an API added to libprojectM after the
v4.1.7 release that vcpkg currently builds against. Rather than tracking unreleased
upstream, that one function was backported onto v4.1.7 via a vcpkg overlay port (see
[Build notes](#build-notes) below).

A second, related fix: `PresetTransition` (libprojectM) measured its own crossfade
duration against the wall clock instead of the same dilated clock used everywhere
else, which caused every preset switch after the first to freeze permanently once
Ambient Speed was below 1.0. Fixed via a second overlay-port patch that makes it use
the shared, dilated timing source instead. (Upstream `master` later arrived at an
equivalent fix independently, entangled with an unrelated rendering refactor — this
fork keeps the smaller, standalone patch.)

## Keyboard shortcut changes

| Action | Shortcut | Notes |
|---|---|---|
| Open Preset Browser | `b` | new |
| Toggle Favorite | `f` | new (previously unused) |
| Open Settings | `Ctrl+S` | was shown in the menu but never actually wired up in stock |

All other shortcuts are unchanged from stock — see the in-app Quick Help (Keyboard
Shortcuts tab) for the full list.

## Other fixes picked up along the way

- Two `FileChooser` bugs, surfaced by building the import/export dialogs: an inverted
  extension-filter condition, and a stale file listing when reopening a dialog
  already sitting in a given folder.

## Build notes

This fork's `vcpkg-configuration.json` adds `overlay-ports/projectm/`, which patches
the vcpkg-built libprojectM v4.1.7 with the two backports described under
[Ambient Speed](#ambient-speed) above. No other build step changes — the normal
`CMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake` configure picks it up automatically.

## Compatibility

Config files, presets, favorites, and playlists all live under your normal projectM
user config directory and use plain JSON/properties formats — nothing here is a
one-way migration. A stock projectMSDL build will simply ignore the new files it
doesn't know about.
