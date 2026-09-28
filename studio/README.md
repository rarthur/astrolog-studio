# Astrolog Studio

A modern GTK 4 front end for the Astrolog 8.00 engine, designed to sit
natively on an [Omarchy](https://omarchy.org) desktop.

## Build & run

```sh
make studio            # builds ./astrolog-studio (needs gtk4 dev files)
./astrolog-studio
make install-studio    # adds a launcher (Super+Space → "Astrolog Studio")
studio/install.sh --uninstall
```

The classic program still builds with plain `make`. The studio binary can
also act as the classic CLI: `./astrolog-studio --cli -qa 7 4 1990 14:30 ...`.

## What you get

- **Birth data form**: name, date, 24h time, and a birthplace search over
  Astrolog's 33,000 city atlas (type, then ↑/↓ and Enter). Coordinates accept
  `40.71`, `-74.006`, `40N43`, `74°00'W`, etc.
- **Automatic historical time zones**: picking a city sets its IANA zone. The
  offset and daylight saving for the birth date come from Astrolog's
  time zone history, falling back to Local Mean Time before standard time
  existed. Manual offset and LMT modes are also available.
- **Live recasting**: every edit recasts the chart instantly.
- **Wheel**: zodiac, houses, angles and aspect lines drawn in your theme's
  colors. Hover a planet (on the wheel or in the list) to trace its aspects.
  Toggle *Transits* to add an outer ring showing the sky right now.
- **Positions**: planets, points and house cusps, with latitude, speed and
  retrograde motion.
- **Aspects**: an aspect grid plus a list sorted by orb, with
  applying/separating markers and transit-to-natal aspects.
- **Balance**: elements, modalities, hemispheres, polarity and retrogrades.
- **Reports**: Astrolog's text reports (interpretation, midpoints, Arabic
  parts, influence, transits, progressions, astro-graph and more), with copy
  and save buttons.
- **Saved charts**: Ctrl+S saves, click to reopen. Stored in
  `~/.local/share/astrolog-studio/`.

Options: all 40 Astrolog house systems, tropical/sidereal zodiac, asteroids,
sensitive points, minor aspects, true/mean node, and a text size setting
(Small, Default, Large, Extra large) that scales the whole interface.

Shortcuts: Ctrl+S save, Ctrl+N now, Ctrl+L find city, Ctrl+1…5 switch views,
F9 toggle sidebar, Ctrl+Q quit.

## Omarchy integration

- **Colors** come from the active theme's `colors.toml`
  (`~/.local/state/omarchy/current/theme/`). Sign elements use the theme's
  red/green/yellow/blue, aspects use its accent/red/orange/blue/cyan, and
  light themes such as Catppuccin Latte are supported.
- **Live theme switching**: the app watches the theme directory and restyles
  itself as soon as `omarchy theme set …` runs. A toast confirms the change.
- **Fonts**: the interface uses Inter (falling back to the system
  sans-serif font), with tabular figures so columns of degrees line up.
  Reports use your `omarchy font` monospace font (JetBrainsMono by default),
  since Astrolog's text tables need fixed-width characters. Astrological
  glyphs use Noto Sans Symbols.
- **Icons** are bundled in `studio/icons/`, so they look the same under any
  icon theme.
- **Corner radius** follows Hyprland's `rounding` in
  `~/.config/hypr/looknfeel.lua`, so widgets match your window corners.
- Without Omarchy, a built-in Tokyo Night style palette is used.
  `ASTROLOG_STUDIO_THEME_STATE` can point at another directory containing
  `theme/colors.toml` and `theme.name`.

## Screenshots without a display

`ASTROLOG_STUDIO_SNAPSHOT=out.png` makes a separate, throwaway instance save
an image of its window and quit; it never touches saved preferences.
`ASTROLOG_STUDIO_SNAPSHOT_PAGE=0…4` picks the view. Combined with GTK's
headless Broadway backend this needs no visible window:

```sh
gtk4-broadwayd :5 &
GDK_BACKEND=broadway BROADWAY_DISPLAY=:5 ASTROLOG_STUDIO_SNAPSHOT=wheel.png ./astrolog-studio
```

## Layout of the code

| File | Purpose |
|------|---------|
| `engine.{h,cpp}` | The only code that includes Astrolog's headers: casting, aspects, atlas search, time zones, reports (run as `--cli` subprocesses) |
| `theme.{h,cpp}` | Omarchy palette/font/rounding, generated GTK CSS, live reload |
| `render.{h,cpp}` | Cairo drawing of the wheel, aspect grid and balance views |
| `store.{h,cpp}` | Saved charts and preferences (GKeyFile) |
| `app.cpp` | The GTK window |

Changes to the original sources are minimal. `astrolog.h` gained an
`ASTROLOG_NO_X11` guard so the engine can be compiled without X11. Also, the
engine sets `SE_EPHE_PATH` because Astrolog's composed ephemeris search path
overflows the Swiss Ephemeris 242 character limit when the install directory
is deep, which silently broke asteroid positions.
