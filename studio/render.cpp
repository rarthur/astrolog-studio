// Copyright (C) 2026 Ryan Arthur. Part of Astrolog Studio, a modified
// version of Astrolog 8.00, which is Copyright (C) 1991-2026 by Walter D.
// Pullen (see README.md and the notices in the Astrolog source files).
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
//
// Astrolog Studio - Cairo renderers for the chart views. See render.h.

#include "render.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "theme.h"

using astro::Body;
using astro::Chart;
using theme::Rgb;

namespace render {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Norm(double d) { return std::fmod(std::fmod(d, 360.0) + 360.0, 360.0); }

enum Anchor { kCenter, kLeft, kRight };

// Draw a line of text centered (or anchored) at x, y. Returns its width.
double Text(cairo_t *cr, const std::string &s, double x, double y,
  double size, const Rgb &color, double alpha = 1.0, bool glyph = false,
  int weight = PANGO_WEIGHT_NORMAL, Anchor anchor = kCenter,
  const Rgb *halo = nullptr) {
  // The Part of Fortune symbol comes from a math font with a much larger
  // design size than the astrological glyphs; bring it in line.
  if (glyph && s.rfind("\u2297", 0) == 0) size *= 0.55;
  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *fd = pango_font_description_new();
  std::string fam = glyph ? theme::GlyphFamily() : theme::Current().font;
  pango_font_description_set_family(fd, fam.c_str());
  pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
  pango_font_description_set_weight(fd, (PangoWeight)weight);
  pango_layout_set_font_description(layout, fd);
  pango_layout_set_text(layout, s.c_str(), -1);
  if (!glyph) {
    // Tabular figures, matching the interface CSS.
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_font_features_new("tnum"));
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
  }
  PangoRectangle ink, logical;
  pango_layout_get_pixel_extents(layout, &ink, &logical);
  double w = logical.width, h = logical.height;
  double ox = anchor == kCenter ? x - w / 2 : (anchor == kLeft ? x : x - w);
  // Center vertically on the ink box for glyphs so symbols look balanced.
  double oy = glyph ? y - ink.y - ink.height / 2.0 : y - h / 2.0;
  if (halo) {
    // Outline in the background color so lines passing behind the text
    // don't cut through it.
    cairo_save(cr);
    cairo_move_to(cr, ox, oy);
    pango_cairo_layout_path(cr, layout);
    theme::SetSource(cr, *halo, 0.9);
    cairo_set_line_width(cr, std::max(2.5, size * 0.3));
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
    cairo_restore(cr);
  }
  theme::SetSource(cr, color, alpha);
  cairo_move_to(cr, ox, oy);
  pango_cairo_show_layout(cr, layout);
  pango_font_description_free(fd);
  g_object_unref(layout);
  return w;
}

void RoundRect(cairo_t *cr, double x, double y, double w, double h, double r) {
  r = std::min({r, w / 2, h / 2});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -kPi / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, kPi / 2);
  cairo_arc(cr, x + r, y + h - r, r, kPi / 2, kPi);
  cairo_arc(cr, x + r, y + r, r, kPi, 3 * kPi / 2);
  cairo_close_path(cr);
}

struct Geo {
  double cx, cy, asc;
  // Screen point for ecliptic longitude at radius r. The Ascendant sits at
  // the 9 o'clock position and the zodiac runs counterclockwise.
  void Pt(double lon, double r, double *x, double *y) const {
    double th = kPi + (lon - asc) * kPi / 180.0;
    *x = cx + r * std::cos(th);
    *y = cy - r * std::sin(th);
  }
  double Theta(double lon) const { return kPi + (lon - asc) * kPi / 180.0; }
};

// Spread glyph positions apart so symbols never overlap, while keeping each
// as close as possible to its true longitude.
std::vector<double> Spread(const std::vector<double> &lons, double minSep) {
  int n = (int)lons.size();
  std::vector<double> out = lons;
  if (n < 2) return out;
  std::vector<int> order(n);
  for (int i = 0; i < n; i++) order[i] = i;
  std::sort(order.begin(), order.end(),
    [&](int a, int b) { return lons[a] < lons[b]; });
  std::vector<double> pos(n);
  for (int i = 0; i < n; i++) pos[i] = lons[order[i]];
  if (minSep * n > 360.0) minSep = 360.0 / n;
  for (int iter = 0; iter < 200; iter++) {
    bool moved = false;
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      double gap = pos[j] - pos[i];
      if (j == 0) gap += 360.0;
      if (gap < minSep - 1e-6) {
        double push = (minSep - gap) / 2.0 + 1e-4;
        pos[i] -= push;
        pos[j] += push;
        moved = true;
      }
    }
    // Pull slightly back toward the true positions to avoid drift.
    for (int i = 0; i < n; i++) {
      double d = lons[order[i]] - pos[i];
      d = std::fmod(d + 540.0, 360.0) - 180.0;
      pos[i] += d * 0.02;
    }
    if (!moved) break;
  }
  for (int i = 0; i < n; i++) out[order[i]] = pos[i];
  return out;
}

void Circle(cairo_t *cr, double cx, double cy, double r) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, cx, cy, r, 0, 2 * kPi);
}

// Draw a ring of body glyphs. Returns glyph centers for hit testing.
void DrawBodies(cairo_t *cr, const Geo &g, const Chart &c, double rTick,
  double rGlyph, double rLabel, double glyphSize, double tickDir,
  bool transit, int hover, std::vector<Hit> &hits) {
  const theme::Palette &p = theme::Current();
  std::vector<double> lons;
  std::vector<int> idx;
  for (size_t i = 0; i < c.bodies.size(); i++) {
    if (c.bodies[i].kind == astro::kAngle) continue;
    if (transit && c.bodies[i].kind == astro::kPoint) continue;
    lons.push_back(c.bodies[i].lon);
    idx.push_back((int)i);
  }
  // Each natal planet is a radial column: glyph, degrees, minutes, and a
  // retrograde mark. Separate columns by whichever part needs the most
  // angle: the glyph at its radius, or the degree label further in.
  double minSep = (glyphSize * 1.2) / rGlyph * 180.0 / kPi;
  if (!transit)
    minSep = std::max(minSep, (glyphSize * 1.2) / rLabel * 180.0 / kPi);
  std::vector<double> disp = Spread(lons, minSep);

  for (size_t k = 0; k < idx.size(); k++) {
    const Body &b = c.bodies[idx[k]];
    bool hot = idx[k] == hover;
    Rgb col = transit ? p.accent : theme::BodyColor(b.id);
    double x0, y0, x1, y1, xg, yg;
    // Exact position tick on the ring.
    g.Pt(b.lon, rTick, &x0, &y0);
    g.Pt(b.lon, rTick + tickDir * glyphSize * 0.35, &x1, &y1);
    theme::SetSource(cr, col, 0.9);
    cairo_set_line_width(cr, hot ? 2.2 : 1.4);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);
    // Leader from the tick to the (possibly displaced) glyph.
    g.Pt(disp[k], rGlyph, &xg, &yg);
    double xl, yl;
    g.Pt(disp[k], rGlyph - tickDir * glyphSize * 0.62, &xl, &yl);
    theme::SetSource(cr, p.dim, 0.35);
    cairo_set_line_width(cr, 0.8);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, xl, yl);
    cairo_stroke(cr);

    if (hot) {
      theme::SetSource(cr, p.accent, 0.22);
      Circle(cr, xg, yg, glyphSize * 0.78);
      cairo_fill(cr);
      theme::SetSource(cr, p.accent, 0.9);
      cairo_set_line_width(cr, 1.2);
      Circle(cr, xg, yg, glyphSize * 0.78);
      cairo_stroke(cr);
    }
    bool text = b.glyph.size() <= 2 && (unsigned char)b.glyph[0] < 0x80;  // "Vx", "EP"
    Text(cr, b.glyph, xg, yg, text ? glyphSize * 0.6 : glyphSize,
      hot ? p.fgBright : col, 1.0, !text,
      text ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);

    Hit h;
    h.x = xg;
    h.y = yg;
    h.r = glyphSize * 0.8;
    h.body = idx[k];
    h.transit = transit;
    hits.push_back(h);
    if (transit) {
      // The transit ring is narrow: mark retrograde motion just outside the
      // glyph, on the same radial line.
      if (b.retro) {
        double xr, yr;
        g.Pt(disp[k], rGlyph + glyphSize * 0.85, &xr, &yr);
        Text(cr, "℞", xr, yr, glyphSize * 0.5, p.red, 1.0, true);
      }
      continue;
    }

    // An upright label block (degrees over minutes) sits inward from the
    // glyph, so it reads the same all around the wheel. The retrograde mark
    // goes further in, with clear space from both.
    double deg = Norm(b.lon);
    int d = (int)std::fmod(deg, 30.0);
    int m = (int)((std::fmod(deg, 30.0) - d) * 60.0);
    char dbuf[16], mbuf[16];
    snprintf(dbuf, sizeof(dbuf), "%d°", d);
    snprintf(mbuf, sizeof(mbuf), "%02d′", m);
    double xb, yb;
    g.Pt(disp[k], rLabel, &xb, &yb);
    Text(cr, dbuf, xb, yb - glyphSize * 0.22, glyphSize * 0.48,
      hot ? p.fgBright : p.fg, 1.0, false, PANGO_WEIGHT_SEMIBOLD, kCenter,
      &p.bgDark);
    double ym = yb + glyphSize * 0.26;
    if (!b.retro) {
      Text(cr, mbuf, xb, ym, glyphSize * 0.37, hot ? p.fgBright : p.soft,
        1.0, false, PANGO_WEIGHT_NORMAL, kCenter, &p.bgDark);
    } else {
      // Retrograde: "12′ ℞" on the minutes line, inside this planet's own
      // label block and well away from the glyph.
      Text(cr, mbuf, xb + glyphSize * 0.1, ym, glyphSize * 0.37,
        hot ? p.fgBright : p.soft, 1.0, false, PANGO_WEIGHT_NORMAL, kRight,
        &p.bgDark);
      Text(cr, "℞", xb + glyphSize * 0.36, ym, glyphSize * 0.44, p.red, 1.0,
        true, PANGO_WEIGHT_NORMAL, kCenter, &p.bgDark);
    }
  }
}

}  // namespace

void DrawWheel(cairo_t *cr, int width, int height, const Chart &c,
  const Chart *tr, const std::vector<astro::Aspect> *trAsp, WheelState &st) {
  const theme::Palette &p = theme::Current();
  st.hits.clear();
  double S = std::min(width, height);
  Geo g{width / 2.0, height / 2.0, c.asc};
  double R = S / 2.0 - std::max(28.0, S * 0.05);
  if (R < 60) return;

  bool withTr = tr != nullptr && tr->ok;
  double base = withTr ? R * 0.83 : R;        // Outer edge of the zodiac.
  double rZo = base, rZi = base * 0.86;       // Zodiac ring.
  double rHi = base * 0.79;                   // House number ring.
  double rA = base * 0.40;                    // Aspect circle.
  double ts = theme::TextScale();
  double glyph = std::clamp(base * 0.095 * ts, 15.0 * ts, 38.0 * ts);
  double rGlyph = rHi - glyph * 0.95;
  double rLabel = rGlyph - glyph * 1.15;  // Degree label block; ℞ inside.

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  // Soft backdrop disc.
  theme::SetSource(cr, p.bgDark, 1.0);
  Circle(cr, g.cx, g.cy, rZo);
  cairo_fill(cr);

  // Transit ring background.
  if (withTr) {
    theme::SetSource(cr, p.accent, 0.05);
    Circle(cr, g.cx, g.cy, R);
    Circle(cr, g.cx, g.cy, rZo);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    theme::SetSource(cr, p.accent, 0.35);
    cairo_set_line_width(cr, 1.0);
    Circle(cr, g.cx, g.cy, R);
    cairo_stroke(cr);
    Text(cr, "TRANSITS", g.cx, g.cy - R - 9, 9, p.accent, 0.8, false,
      PANGO_WEIGHT_BOLD);
  }

  // Zodiac signs.
  for (int s = 0; s < 12; s++) {
    double a0 = g.Theta(s * 30.0), a1 = g.Theta(s * 30.0 + 30.0);
    Rgb ec = theme::ElementColor(astro::SignElement(s));
    cairo_new_path(cr);
    // Cairo arcs run clockwise in screen space; our angles are
    // counterclockwise, so negate.
    cairo_arc_negative(cr, g.cx, g.cy, rZo, -a0, -a1);
    cairo_arc(cr, g.cx, g.cy, rZi, -a1, -a0);
    cairo_close_path(cr);
    theme::SetSource(cr, ec, s % 2 ? 0.10 : 0.16);
    cairo_fill(cr);
    double x, y;
    g.Pt(s * 30.0 + 15.0, (rZo + rZi) / 2, &x, &y);
    Text(cr, astro::SignGlyph(s) + "︎", x, y, (rZo - rZi) * 0.62, ec,
      1.0, true);
  }
  // Sign boundaries and degree ticks.
  cairo_set_line_width(cr, 1.0);
  for (int d = 0; d < 360; d++) {
    double len = d % 30 == 0 ? (rZo - rZi) : d % 10 == 0 ? (rZo - rZi) * 0.3
      : d % 5 == 0 ? (rZo - rZi) * 0.2 : (rZo - rZi) * 0.1;
    double x0, y0, x1, y1;
    g.Pt(d, rZi, &x0, &y0);
    g.Pt(d, rZi + len, &x1, &y1);
    theme::SetSource(cr, p.fg, d % 30 == 0 ? 0.35 : 0.22);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);
  }
  theme::SetSource(cr, p.fg, 0.28);
  cairo_set_line_width(cr, 1.2);
  Circle(cr, g.cx, g.cy, rZo);
  cairo_stroke(cr);
  Circle(cr, g.cx, g.cy, rZi);
  cairo_stroke(cr);
  theme::SetSource(cr, p.fg, 0.12);
  cairo_set_line_width(cr, 1.0);
  Circle(cr, g.cx, g.cy, rHi);
  cairo_stroke(cr);

  // Inner aspect disc.
  theme::SetSource(cr, p.bg, 1.0);
  Circle(cr, g.cx, g.cy, rA);
  cairo_fill(cr);
  theme::SetSource(cr, p.fg, 0.18);
  Circle(cr, g.cx, g.cy, rA);
  cairo_stroke(cr);

  // House cusps and numbers.
  for (int h = 1; h <= 12; h++) {
    double lon = c.cusps[h];
    bool axis = h == 1 || h == 4 || h == 7 || h == 10;
    double x0, y0, x1, y1;
    g.Pt(lon, rA, &x0, &y0);
    g.Pt(lon, axis ? rZi : rZi, &x1, &y1);
    theme::SetSource(cr, axis ? p.fg : p.fg, axis ? 0.55 : 0.16);
    cairo_set_line_width(cr, axis ? 1.6 : 1.0);
    if (!axis) {
      double dash[] = {2.0, 4.0};
      cairo_set_dash(cr, dash, 2, 0);
    }
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
    double next = c.cusps[h % 12 + 1];
    double span = Norm(next - lon);
    double x, y;
    g.Pt(lon + span / 2.0, (rHi + rZi) / 2.0, &x, &y);
    Text(cr, std::to_string(h), x, y, (rZi - rHi) * 0.52, p.dim, 0.9, false,
      PANGO_WEIGHT_BOLD);
  }

  // The four angles, extending out past the zodiac with labels.
  struct { double lon; const char *label; } axes[] = {
    {c.asc, "AC"}, {c.asc + 180.0, "DC"}, {c.mc, "MC"}, {c.mc + 180.0, "IC"}};
  for (auto &ax : axes) {
    double x0, y0, x1, y1, xt, yt;
    bool major = ax.label[0] == 'A' || ax.label[0] == 'M';
    g.Pt(ax.lon, rZo, &x0, &y0);
    g.Pt(ax.lon, rZo + (withTr ? 6 : 10), &x1, &y1);
    theme::SetSource(cr, major ? p.accent : p.fg, major ? 1.0 : 0.6);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);
    if (!withTr) {
      g.Pt(ax.lon, rZo + 20, &xt, &yt);
      Text(cr, ax.label, xt, yt, 12.5, major ? p.accent : p.soft, 1.0, false,
        PANGO_WEIGHT_BOLD);
    }
  }

  // Aspect lines.
  bool focus = st.hoverBody >= 0 || st.hoverTransit >= 0;
  auto major = [](const Body &b) {
    return b.kind == astro::kLuminary || b.kind == astro::kPlanet ||
      b.kind == astro::kAngle || b.id == 16;  // North Node
  };
  for (const auto &a : c.aspects) {
    const Body &A = c.bodies[a.a], &B = c.bodies[a.b];
    bool involved = st.hoverBody == a.a || st.hoverBody == a.b;
    // Keep the wheel readable: asteroid and point aspects only appear
    // when one of the pair is focused.
    if (!involved && (!major(A) || !major(B))) continue;
    if (st.hoverTransit >= 0) involved = false;
    double strength = a.maxOrb > 0 ? 1.0 - a.orb / a.maxOrb : 0.5;
    double alpha = 0.25 + 0.6 * strength;
    if (focus) alpha = involved ? 1.0 : 0.06;
    if (a.type == 1) {
      // Conjunctions: highlight the arc between the pair on the disc edge.
      double t0 = g.Theta(A.lon), t1 = g.Theta(B.lon);
      double d = std::fmod(B.lon - A.lon + 540.0, 360.0) - 180.0;
      theme::SetSource(cr, theme::AspectColor(1), alpha);
      cairo_set_line_width(cr, involved ? 4.0 : 3.0);
      cairo_new_path(cr);
      if (d > 0) cairo_arc_negative(cr, g.cx, g.cy, rA - 3, -t0, -t1);
      else cairo_arc(cr, g.cx, g.cy, rA - 3, -t0, -t1);
      cairo_stroke(cr);
      continue;
    }
    double x0, y0, x1, y1;
    g.Pt(A.lon, rA, &x0, &y0);
    g.Pt(B.lon, rA, &x1, &y1);
    theme::SetSource(cr, theme::AspectColor(a.type), alpha);
    cairo_set_line_width(cr, (involved ? 2.2 : 1.0) + strength * 1.0);
    if (astro::AspectNature(a.type) == 3) {
      double dash[] = {4.0, 3.0};
      cairo_set_dash(cr, dash, 2, 0);
    }
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
  }
  // Transit-to-natal aspect lines, shown when a transit body is focused.
  if (withTr && trAsp && st.hoverTransit >= 0) {
    for (const auto &a : *trAsp) {
      if (a.a != st.hoverTransit) continue;
      double x0, y0, x1, y1;
      g.Pt(tr->bodies[a.a].lon, rA, &x0, &y0);
      g.Pt(c.bodies[a.b].lon, rA, &x1, &y1);
      theme::SetSource(cr, theme::AspectColor(a.type), 1.0);
      cairo_set_line_width(cr, 2.4);
      double dash[] = {6.0, 3.0};
      cairo_set_dash(cr, dash, 2, 0);
      cairo_move_to(cr, x0, y0);
      cairo_line_to(cr, x1, y1);
      cairo_stroke(cr);
      cairo_set_dash(cr, nullptr, 0, 0);
      theme::SetSource(cr, p.accent, 1.0);
      Circle(cr, x0, y0, 3.5);
      cairo_fill(cr);
    }
  }
  // Dots where each body meets the aspect disc.
  for (const auto &b : c.bodies) {
    double x, y;
    g.Pt(b.lon, rA, &x, &y);
    theme::SetSource(cr, b.kind == astro::kAngle ? p.accent :
      theme::BodyColor(b.id), 0.9);
    Circle(cr, x, y, 2.2);
    cairo_fill(cr);
  }

  // Natal bodies.
  DrawBodies(cr, g, c, rZi, rGlyph, rLabel, glyph, -1.0, false, st.hoverBody,
    st.hits);

  // Transit bodies, outside the zodiac.
  if (withTr) {
    double tGlyph = std::clamp((R - rZo) * 0.42, 10.0, 22.0);
    DrawBodies(cr, g, *tr, rZo, rZo + (R - rZo) * 0.55, 0.0, tGlyph, 1.0,
      true, st.hoverTransit, st.hits);
  }
}

void DrawAspectGrid(cairo_t *cr, int width, int height, const Chart &c,
  GridState &st) {
  const theme::Palette &p = theme::Current();
  int n = (int)c.bodies.size();
  st.count = n;
  if (n == 0) return;
  double cell = std::floor(std::min((width - 16.0) / (n + 0.5),
    (height - 16.0) / (n + 0.5)));
  double ts = theme::TextScale();
  cell = std::clamp(cell, 30.0 * ts, 56.0 * ts);
  st.cell = cell;
  st.originX = std::floor((width - cell * n) / 2.0);
  st.originY = std::floor((height - cell * n) / 2.0);
  double ox = st.originX, oy = st.originY;

  // Lookup table of aspects by pair.
  std::vector<const astro::Aspect *> grid(n * n, nullptr);
  for (const auto &a : c.aspects) {
    grid[a.a * n + a.b] = &a;
    grid[a.b * n + a.a] = &a;
  }
  double rr = std::max(0, p.radius - 4);
  for (int row = 0; row < n; row++) {
    for (int col = 0; col <= row; col++) {
      double x = ox + col * cell, y = oy + row * cell;
      bool hot = (row == st.hoverRow && col == st.hoverCol);
      bool lane = row == st.hoverRow || col == st.hoverRow ||
        row == st.hoverCol || col == st.hoverCol;
      if (col == row) {
        const Body &b = c.bodies[row];
        RoundRect(cr, x + 1.5, y + 1.5, cell - 3, cell - 3, rr);
        theme::SetSource(cr, p.accent, lane ? 0.28 : 0.12);
        cairo_fill(cr);
        bool text = (unsigned char)b.glyph[0] < 0x80;
        Text(cr, b.glyph, x + cell / 2, y + cell / 2,
          text ? cell * 0.4 : cell * 0.74,
          b.kind == astro::kAngle ? p.accent : theme::BodyColor(b.id), 1.0,
          !text, PANGO_WEIGHT_BOLD);
        continue;
      }
      RoundRect(cr, x + 1.5, y + 1.5, cell - 3, cell - 3, rr);
      theme::SetSource(cr, hot ? p.accent : p.fg, hot ? 0.25 :
        (lane && st.hoverRow >= 0 ? 0.07 : 0.035));
      cairo_fill(cr);
      const astro::Aspect *a = grid[row * n + col];
      if (!a) continue;
      Rgb ac = theme::AspectColor(a->type);
      double strength = a->maxOrb > 0 ? 1.0 - a->orb / a->maxOrb : 0.5;
      RoundRect(cr, x + 1.5, y + 1.5, cell - 3, cell - 3, rr);
      theme::SetSource(cr, ac, 0.10 + 0.22 * strength);
      cairo_fill(cr);
      std::string g = astro::AspectGlyph(a->type);
      bool text = (unsigned char)g[0] < 0x80;
      bool orbLine = cell >= 40;  // Otherwise the orb is in the tooltip.
      Text(cr, g, x + cell / 2, y + cell * (orbLine ? 0.40 : 0.5),
        text ? cell * 0.38 : cell * 0.7, ac, 1.0, !text, PANGO_WEIGHT_BOLD);
      char buf[16];
      snprintf(buf, sizeof(buf), "%d°%02d", (int)a->orb,
        (int)((a->orb - (int)a->orb) * 60));
      if (orbLine)
        Text(cr, buf, x + cell / 2, y + cell * 0.79, cell * 0.21, p.soft,
          0.9);
    }
  }
}

void DrawBalance(cairo_t *cr, int width, int height, const Chart &c) {
  const theme::Palette &p = theme::Current();
  // Weighted count: personal points matter more.
  auto weight = [](const Body &b) {
    if (b.id == 1 || b.id == 2 || b.kind == astro::kAngle) return 2;
    if (b.kind == astro::kPlanet) return 1;
    return 0;
  };
  double elem[4] = {0}, mode[3] = {0}, hemiE = 0, hemiW = 0, hemiN = 0,
    hemiS = 0, total = 0;
  std::string elemG[4], modeG[3];
  int retro = 0;
  std::string retroG;
  for (const auto &b : c.bodies) {
    int w = weight(b);
    if (!w) continue;
    int s = (int)(Norm(b.lon) / 30.0);
    elem[astro::SignElement(s)] += w;
    mode[astro::SignMode(s)] += w;
    elemG[astro::SignElement(s)] += b.glyph + " ";
    modeG[astro::SignMode(s)] += b.glyph + " ";
    total += w;
    if (b.kind != astro::kAngle) {
      int h = b.house;
      if (h >= 10 || h <= 3) hemiE += w; else hemiW += w;
      if (h <= 6) hemiN += w; else hemiS += w;
    }
    if (b.retro) { retro++; retroG += b.glyph + " "; }
  }
  if (total <= 0) return;

  // The balance view is laid out in fixed units; scale it as a whole.
  double ts = theme::TextScale();
  cairo_scale(cr, ts, ts);
  width = (int)(width / ts);

  double pad = 24;
  double colW = std::min(760.0, width - pad * 2);
  double x0 = (width - colW) / 2, y = pad;
  double r = p.radius;

  auto panel = [&](double h, const char *title) {
    RoundRect(cr, x0, y, colW, h, r);
    theme::SetSource(cr, p.bgDark, 1.0);
    cairo_fill_preserve(cr);
    theme::SetSource(cr, p.fg, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    Text(cr, title, x0 + 18, y + 20, 11.5, p.dim, 1.0, false,
      PANGO_WEIGHT_HEAVY, kLeft);
  };

  auto bars = [&](int n, const double *vals, const char *const *names,
    const std::string *glyphs, const Rgb *colors) {
    double inner = colW - 36;
    double cw = inner / n;
    double maxV = 0;
    for (int i = 0; i < n; i++) maxV = std::max(maxV, vals[i]);
    for (int i = 0; i < n; i++) {
      double cx = x0 + 18 + cw * i;
      int pct = (int)std::lround(vals[i] * 100.0 / total);
      Text(cr, names[i], cx, y + 48, 13.5, colors[i], 1.0, false,
        PANGO_WEIGHT_BOLD, kLeft);
      Text(cr, std::to_string(pct) + "%", cx + cw - 14, y + 48, 13.5,
        p.fgBright, 1.0, false, PANGO_WEIGHT_BOLD, kRight);
      double bw = cw - 14;
      RoundRect(cr, cx, y + 62, bw, 8, 4);
      theme::SetSource(cr, p.fg, 0.08);
      cairo_fill(cr);
      if (vals[i] > 0) {
        RoundRect(cr, cx, y + 62, std::max(8.0, bw * vals[i] / maxV), 8, 4);
        theme::SetSource(cr, colors[i], 0.9);
        cairo_fill(cr);
      }
      Text(cr, glyphs[i].empty() ? "—" : glyphs[i], cx, y + 90, 17,
        p.soft, 1.0, true, PANGO_WEIGHT_NORMAL, kLeft);
    }
  };

  const char *en[] = {"Fire", "Earth", "Air", "Water"};
  Rgb ec[4];
  for (int i = 0; i < 4; i++) ec[i] = theme::ElementColor(i);
  panel(112, "ELEMENTS");
  bars(4, elem, en, elemG, ec);
  y += 112 + 16;

  const char *mn[] = {"Cardinal", "Fixed", "Mutable"};
  Rgb mc[3] = {p.accent, p.magenta, p.cyan};
  panel(112, "MODALITIES");
  bars(3, mode, mn, modeG, mc);
  y += 112 + 16;

  // Hemispheres & polarity.
  double h3 = 150;
  panel(h3, "HEMISPHERES & POLARITY");
  double dcx = x0 + 18 + 55, dcy = y + 30 + (h3 - 30) / 2, dr = 50;
  double hemTot = hemiE + hemiW;
  // Quadrant disc: upper = southern (above horizon).
  auto quad = [&](double a0, double a1, double v) {
    cairo_move_to(cr, dcx, dcy);
    cairo_arc(cr, dcx, dcy, dr, a0, a1);
    cairo_close_path(cr);
    theme::SetSource(cr, p.accent, 0.08 + 0.6 * (hemTot > 0 ? v / hemTot : 0));
    cairo_fill(cr);
  };
  quad(kPi, 2 * kPi, hemiS);   // Top half: above the horizon.
  quad(0, kPi, hemiN);         // Bottom half: below the horizon.
  theme::SetSource(cr, p.fg, 0.4);
  cairo_set_line_width(cr, 1.0);
  Circle(cr, dcx, dcy, dr);
  cairo_stroke(cr);
  cairo_move_to(cr, dcx - dr - 6, dcy);
  cairo_line_to(cr, dcx + dr + 6, dcy);
  cairo_stroke(cr);
  Text(cr, "AC", dcx - dr - 16, dcy, 9, p.accent, 1, false, PANGO_WEIGHT_BOLD);
  Text(cr, std::to_string((int)hemiS), dcx, dcy - dr / 2, 14, p.fgBright, 1,
    false, PANGO_WEIGHT_BOLD);
  Text(cr, std::to_string((int)hemiN), dcx, dcy + dr / 2, 14, p.fgBright, 1,
    false, PANGO_WEIGHT_BOLD);

  double tx = x0 + 18 + 140;
  double tw = colW - (tx - x0) - 18;
  auto line = [&](double ly, const char *l, const char *rlab, double a,
    double b, Rgb ca, Rgb cb) {
    Text(cr, l, tx, ly, 12.5, ca, 1, false, PANGO_WEIGHT_BOLD, kLeft);
    Text(cr, rlab, tx + tw, ly, 12.5, cb, 1, false, PANGO_WEIGHT_BOLD, kRight);
    double sum = a + b;
    double f = sum > 0 ? a / sum : 0.5;
    RoundRect(cr, tx, ly + 11, tw, 8, 4);
    theme::SetSource(cr, cb, 0.75);
    cairo_fill(cr);
    RoundRect(cr, tx, ly + 11, std::max(8.0, tw * f), 8, 4);
    theme::SetSource(cr, ca, 0.95);
    cairo_fill(cr);
  };
  char l1[64], r1[64], l2[64], r2[64], l3[64], r3[64];
  snprintf(l1, sizeof(l1), "East · self %d", (int)hemiE);
  snprintf(r1, sizeof(r1), "%d · others West", (int)hemiW);
  snprintf(l2, sizeof(l2), "Above · public %d", (int)hemiS);
  snprintf(r2, sizeof(r2), "%d · private Below", (int)hemiN);
  double yang = elem[0] + elem[2], yin = elem[1] + elem[3];
  snprintf(l3, sizeof(l3), "Yang · active %d", (int)yang);
  snprintf(r3, sizeof(r3), "%d · receptive Yin", (int)yin);
  line(y + 44, l1, r1, hemiE, hemiW, p.yellow, p.blue);
  line(y + 80, l2, r2, hemiS, hemiN, p.accent, p.dim);
  line(y + 116, l3, r3, yang, yin, p.red, p.cyan);
  y += h3 + 16;

  panel(64, "RETROGRADE");
  Text(cr, retro ? retroG : "None — every planet is direct", x0 + 18,
    y + 44, retro ? 16 : 12, retro ? p.red : p.soft, 1.0, retro,
    PANGO_WEIGHT_NORMAL, kLeft);
  y += 64;
  (void)height;
}

void DrawGlyph(cairo_t *cr, double cx, double cy, const std::string &glyph,
  const Rgb &color, double size) {
  bool text = !glyph.empty() && (unsigned char)glyph[0] < 0x80;
  if (text)
    Text(cr, glyph, cx, cy, size * 0.62, color, 1.0, false, PANGO_WEIGHT_BOLD);
  else
    Text(cr, glyph + "\uFE0E", cx, cy, size, color, 1.0, true);
}

void DrawEmptyState(cairo_t *cr, int width, int height, const char *text) {
  const theme::Palette &p = theme::Current();
  Text(cr, text, width / 2.0, height / 2.0, 14.5, p.dim);
}

}  // namespace render
