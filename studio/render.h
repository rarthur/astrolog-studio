// Copyright (C) 2026 Ryan Arthur. Part of Astrolog Studio, a modified
// version of Astrolog 8.00, which is Copyright (C) 1991-2026 by Walter D.
// Pullen (see README.md and the notices in the Astrolog source files).
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
//
// Astrolog Studio - Cairo renderers for the chart views.

#pragma once

#include <cairo.h>

#include <vector>

#include "engine.h"
#include "theme.h"

namespace render {

struct Hit {
  double x = 0, y = 0, r = 0;
  int body = -1;        // Index into Chart::bodies.
  bool transit = false; // Index refers to the transit chart instead.
};

struct WheelState {
  int hoverBody = -1;          // Natal body index under the pointer.
  int hoverTransit = -1;       // Transit body index under the pointer.
  bool showHouses = true;
  std::vector<Hit> hits;       // Filled in by DrawWheel.
};

void DrawWheel(cairo_t *cr, int width, int height, const astro::Chart &c,
  const astro::Chart *transits, const std::vector<astro::Aspect> *transitAsp,
  WheelState &st);

struct GridState {
  int hoverRow = -1, hoverCol = -1;
  double originX = 0, originY = 0, cell = 0;  // Filled in by DrawAspectGrid.
  int count = 0;
};

void DrawAspectGrid(cairo_t *cr, int width, int height,
  const astro::Chart &c, GridState &st);

void DrawBalance(cairo_t *cr, int width, int height, const astro::Chart &c);

// Draw a single astrological glyph (or short text like "AC") centered on its
// ink box at cx, cy. Used by the small glyph widgets in lists and tables.
void DrawGlyph(cairo_t *cr, double cx, double cy, const std::string &glyph,
  const theme::Rgb &color, double size);

void DrawEmptyState(cairo_t *cr, int width, int height, const char *text);

}  // namespace render
