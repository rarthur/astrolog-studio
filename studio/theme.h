// Copyright (C) 2026 Ryan Arthur. Part of Astrolog Studio, a modified
// version of Astrolog 8.00, which is Copyright (C) 1991-2026 by Walter D.
// Pullen (see README.md and the notices in the Astrolog source files).
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
//
// Astrolog Studio - Omarchy theme integration.
//
// The palette is read from the active Omarchy theme
// (~/.local/state/omarchy/current/theme/colors.toml), the interface font is
// Inter (when installed), reports use `omarchy font` (fontconfig's monospace
// family), and the corner radius follows
// Hyprland's decoration rounding. The theme directory is watched so the app
// restyles itself live whenever `omarchy theme set` runs.

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>

namespace theme {

struct Rgb {
  double r = 0, g = 0, b = 0;
};

struct Palette {
  std::string name = "Default";
  bool dark = true;
  bool fromOmarchy = false;
  std::string font = "sans-serif";  // Interface text.
  std::string mono = "monospace";   // Reports and fixed-width text.
  int radius = 8;
  Rgb accent, selection, selectionFg, muted;
  Rgb bg, bgDark, bgDarker, bgLighter;
  Rgb fg, fgDark, fgLight, fgBright;
  Rgb red, yellow, orange, green, cyan, blue, magenta, brown;
  // Derived text tones that read well in both light and dark themes
  // (Omarchy's muted/dark_foreground are lighter than fg in light themes).
  Rgb soft;    // Secondary text.
  Rgb dim;     // Tertiary text, captions.
  Rgb onAccent;
};

const Palette &Current();

// Load the palette and install the application CSS. Call once after GTK is
// initialized; onChange runs after every live theme reload.
void Install(std::function<void()> onChange);

std::string Hex(const Rgb &c);
Rgb Mix(const Rgb &a, const Rgb &b, double t);  // t=0 -> a, t=1 -> b
void SetSource(cairo_t *cr, const Rgb &c, double alpha = 1.0);

// Colors with astrological meaning, derived from the palette.
Rgb ElementColor(int element);  // 0 fire, 1 earth, 2 air, 3 water
Rgb AspectColor(int type);
Rgb BodyColor(int id);

// Text size multiplier for the whole interface (CSS, glyph widgets and the
// Cairo-drawn views). The user setting (e.g. 1.0 or 1.15) is applied on top
// of a base enlargement.
double TextScale();
void SetTextScale(double userScale);  // Restyles the app immediately.

// Font family list to use for astrological glyphs.
std::string GlyphFamily();

}  // namespace theme
