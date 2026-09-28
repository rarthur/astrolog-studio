// Copyright (C) 2026 Ryan Arthur. Part of Astrolog Studio, a modified
// version of Astrolog 8.00, which is Copyright (C) 1991-2026 by Walter D.
// Pullen (see README.md and the notices in the Astrolog source files).
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
//
// Astrolog Studio - Omarchy theme integration. See theme.h.

#include "theme.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <regex>

namespace theme {

namespace {

Palette g_pal;
GtkCssProvider *g_provider = nullptr;
GFileMonitor *g_monitor = nullptr;
GFileMonitor *g_hyprMonitor = nullptr;
guint g_reloadSource = 0;
std::function<void()> g_onChange;

// The live Omarchy theme lives here. ASTROLOG_STUDIO_THEME_STATE can point
// elsewhere (a directory holding theme/colors.toml and theme.name).
std::string StateDir() {
  const char *env = g_getenv("ASTROLOG_STUDIO_THEME_STATE");
  if (env && *env) return env;
  return std::string(g_get_home_dir()) + "/.local/state/omarchy/current";
}

bool ParseHex(const std::string &s, Rgb *out) {
  unsigned r, g, b;
  const char *p = s.c_str();
  if (*p == '#') p++;
  if (strlen(p) < 6 || sscanf(p, "%2x%2x%2x", &r, &g, &b) != 3)
    return false;
  out->r = r / 255.0;
  out->g = g / 255.0;
  out->b = b / 255.0;
  return true;
}

double Luma(const Rgb &c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

// Minimal TOML reader for the flat `key = "value"` colors.toml format.
std::map<std::string, std::string> ReadToml(const std::string &path) {
  std::map<std::string, std::string> kv;
  gchar *text = nullptr;
  if (!g_file_get_contents(path.c_str(), &text, nullptr, nullptr))
    return kv;
  gchar **lines = g_strsplit(text, "\n", -1);
  for (gchar **l = lines; *l; l++) {
    std::string line = *l;
    size_t hash = line.find('#');
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    // A '#' before the '=' is a comment; after it, it's a color value.
    if (hash != std::string::npos && hash < eq) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    auto trim = [](std::string &t) {
      t.erase(0, t.find_first_not_of(" \t\"'"));
      size_t e = t.find_last_not_of(" \t\"'\r");
      t.erase(e == std::string::npos ? 0 : e + 1);
    };
    trim(k);
    trim(v);
    kv[k] = v;
  }
  g_strfreev(lines);
  g_free(text);
  return kv;
}

std::string RunLine(const char *cmd) {
  gchar *out = nullptr;
  std::string s;
  if (g_spawn_command_line_sync(cmd, &out, nullptr, nullptr, nullptr) && out) {
    s = out;
    s.erase(s.find_last_not_of(" \n\r\t") + 1);
    size_t nl = s.find('\n');
    if (nl != std::string::npos) s.erase(nl);
    size_t comma = s.find(',');
    if (comma != std::string::npos) s.erase(comma);
  }
  g_free(out);
  return s;
}

// Hyprland's decoration rounding, so our corners match the window corners.
int ReadRounding() {
  const std::string home = g_get_home_dir();
  const char *files[] = {"/.config/hypr/looknfeel.lua",
    "/.config/hypr/looknfeel.conf"};
  std::regex re("^\\s*rounding\\s*=\\s*(\\d+)");
  for (const char *f : files) {
    gchar *text = nullptr;
    if (!g_file_get_contents((home + f).c_str(), &text, nullptr, nullptr))
      continue;
    gchar **lines = g_strsplit(text, "\n", -1);
    int val = -1;
    for (gchar **l = lines; *l; l++) {
      std::cmatch m;
      if (std::regex_search(*l, m, re)) val = atoi(m[1].str().c_str());
    }
    g_strfreev(lines);
    g_free(text);
    if (val >= 0) return std::min(val, 16);
  }
  return 0;  // Omarchy's default is square corners.
}

void LoadPalette() {
  Palette p;
  // Fallback: a Tokyo Night flavored palette for systems without Omarchy.
  const std::map<std::string, std::string> defaults = {
    {"accent", "#7aa2f7"}, {"background", "#1a1b26"},
    {"foreground", "#c0caf5"}, {"selection", "#33467c"},
    {"muted", "#565f89"}, {"red", "#f7768e"}, {"yellow", "#e0af68"},
    {"orange", "#ff9e64"}, {"green", "#9ece6a"}, {"cyan", "#7dcfff"},
    {"blue", "#7aa2f7"}, {"magenta", "#bb9af7"}, {"brown", "#8c6c3e"},
  };
  std::string dir = StateDir();
  auto kv = ReadToml(dir + "/theme/colors.toml");
  p.fromOmarchy = !kv.empty();
  auto get = [&](const char *key, Rgb *out) -> bool {
    auto it = kv.find(key);
    if (it != kv.end() && ParseHex(it->second, out)) return true;
    auto d = defaults.find(key);
    if (d != defaults.end() && ParseHex(d->second, out)) return !p.fromOmarchy;
    return false;
  };
  get("background", &p.bg);
  get("foreground", &p.fg);
  auto m = kv.find("mode");
  p.dark = m != kv.end() ? m->second != "light" : Luma(p.bg) < 0.5;
  Rgb black{0, 0, 0}, white{1, 1, 1};
  Rgb deeper = p.dark ? black : Rgb{0.35, 0.35, 0.4};
  if (!get("accent", &p.accent)) get("blue", &p.accent);
  if (!get("dark_background", &p.bgDark)) p.bgDark = Mix(p.bg, deeper, 0.18);
  if (!get("darker_background", &p.bgDarker))
    p.bgDarker = Mix(p.bg, deeper, 0.32);
  if (!get("lighter_background", &p.bgLighter))
    p.bgLighter = Mix(p.bg, p.fg, 0.10);
  if (!get("dark_foreground", &p.fgDark)) p.fgDark = Mix(p.fg, p.bg, 0.3);
  if (!get("light_foreground", &p.fgLight)) p.fgLight = p.fg;
  if (!get("bright_foreground", &p.fgBright))
    p.fgBright = Mix(p.fg, p.dark ? white : black, 0.2);
  if (!get("selection", &p.selection)) p.selection = Mix(p.bg, p.accent, 0.3);
  if (!get("selection_foreground", &p.selectionFg)) p.selectionFg = p.fgBright;
  if (!get("muted", &p.muted)) p.muted = Mix(p.fg, p.bg, 0.5);
  get("red", &p.red);
  get("yellow", &p.yellow);
  get("orange", &p.orange);
  get("green", &p.green);
  get("cyan", &p.cyan);
  get("blue", &p.blue);
  get("magenta", &p.magenta);
  if (!get("brown", &p.brown)) p.brown = Mix(p.orange, p.bg, 0.3);
  if (!kv.count("orange")) p.orange = Mix(p.red, p.yellow, 0.5);

  p.soft = Mix(p.fg, p.bg, 0.15);
  p.dim = Mix(p.fg, p.bg, p.dark ? 0.33 : 0.30);
  // Pick black or white text on the accent, whichever contrasts more.
  auto lin = [](double c) {
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  double L = 0.2126 * lin(p.accent.r) + 0.7152 * lin(p.accent.g) +
    0.0722 * lin(p.accent.b);
  double cWhite = 1.05 / (L + 0.05), cBlack = (L + 0.05) / 0.05;
  p.onAccent = cWhite >= cBlack ? Rgb{1, 1, 1} : Rgb{0.07, 0.07, 0.08};

  gchar *name = nullptr;
  if (g_file_get_contents((dir + "/theme.name").c_str(), &name, nullptr,
    nullptr)) {
    std::string n = g_strstrip(name);
    // "tokyo-night" -> "Tokyo Night", like `omarchy theme current`.
    bool up = true;
    for (char &c : n) {
      if (c == '-') { c = ' '; up = true; }
      else if (up) { c = g_ascii_toupper(c); up = false; }
    }
    p.name = n;
  } else if (!p.fromOmarchy) {
    p.name = "Built-in";
  }
  g_free(name);

  // Reports keep the Omarchy monospace font (`omarchy font set`), since
  // Astrolog's text tables need fixed-width characters.
  std::string mono = RunLine("fc-match monospace -f '%{family}'");
  if (!mono.empty()) p.mono = mono;
  // The interface uses Inter, a typeface designed for screen legibility,
  // falling back to the system sans-serif font.
  std::string ui = RunLine("fc-match Inter -f '%{family}'");
  if (ui.rfind("Inter", 0) != 0)
    ui = RunLine("fc-match sans-serif -f '%{family}'");
  if (!ui.empty()) p.font = ui;
  p.radius = ReadRounding();
  g_pal = p;
}

std::string Rgba(const Rgb &c, double a) {
  char buf[64];
  snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.3f)", (int)lround(c.r * 255),
    (int)lround(c.g * 255), (int)lround(c.b * 255), a);
  return buf;
}

void Replace(std::string &s, const std::string &from, const std::string &to) {
  for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos;
    pos += to.size())
    s.replace(pos, from.size(), to);
}

std::string BuildCss() {
  const Palette &p = g_pal;
  std::string css = R"CSS(
* {
  font-family: "@FONT@", sans-serif;
  /* Tabular figures keep degrees and times aligned in columns. */
  font-feature-settings: "tnum";
}
window, .background {
  background-color: @BG@;
  color: @FG@;
}
window {
  font-size: 15px;
}

headerbar {
  background: @BGDARK@;
  color: @FG@;
  border-bottom: 1px solid @BORDER@;
  box-shadow: none;
  min-height: 44px;
  padding: 0 8px;
}
headerbar:backdrop { background: @BGDARK@; }
headerbar .app-title {
  font-weight: 800;
  letter-spacing: 1px;
  color: @ACCENT@;
}
headerbar .theme-chip {
  font-size: 13px;
  color: @MUTED@;
}

/* ---- Sidebar ------------------------------------------------------- */
.sidebar {
  background: @BGDARK@;
  border-right: 1px solid @BORDER@;
}
.sidebar-inner { padding: 18px 14px 24px 14px; }
.section-title {
  font-size: 12.5px;
  font-weight: 800;
  letter-spacing: 2px;
  color: @MUTED@;
  margin-top: 18px;
  margin-bottom: 6px;
}
.section-title.first { margin-top: 0; }
.field-label {
  font-size: 13px;
  color: @FGDARK@;
  margin-bottom: 2px;
}
.hint {
  font-size: 13px;
  color: @MUTED@;
}
.hint.error { color: @RED@; }
.zone-card {
  background: @BG@;
  border: 1px solid @BORDER@;
  border-radius: @R@px;
  padding: 8px 10px;
}
.zone-card .zone-name { font-weight: 700; color: @FG@; }

/* ---- Inputs -------------------------------------------------------- */
entry, spinbutton, dropdown > button, .search-entry {
  background: @BGINPUT@;
  color: @FG@;
  border: 1px solid @BORDER@;
  border-radius: @R@px;
  box-shadow: none;
  min-height: 32px;
  outline: none;
}
spinbutton { padding: 0; }
spinbutton > text { padding: 0 4px 0 8px; min-width: 0; }
spinbutton > button {
  border: none;
  background: transparent;
  color: @MUTED@;
  min-width: 16px;
  padding: 0;
  border-radius: 0;
}
spinbutton > button:hover { color: @ACCENT@; background: @HOVER@; }
entry:focus-within, spinbutton:focus-within {
  border-color: @ACCENT@;
  box-shadow: 0 0 0 1px @ACCENTSOFT@;
}
entry > text > placeholder { color: @MUTED@; }
entry image { color: @MUTED@; }
text selection, entry selection, textview text selection {
  background: @SELECTION@;
  color: @SELFG@;
}
dropdown > button {
  padding: 0 10px;
}
dropdown > button:hover { border-color: @MUTED@; }

button {
  background: @BGINPUT@;
  color: @FG@;
  border: 1px solid @BORDER@;
  border-radius: @R@px;
  box-shadow: none;
  text-shadow: none;
  -gtk-icon-shadow: none;
  min-height: 30px;
  padding: 2px 10px;
}
button:hover { background: @HOVER@; border-color: @MUTED@; }
button:active, button:checked {
  background: @ACCENTSOFT@;
  border-color: @ACCENT@;
  color: @FGBRIGHT@;
}
button.flat {
  background: transparent;
  border-color: transparent;
}
button.flat:hover { background: @HOVER@; }
menubutton.flat > button { background: transparent; border-color: transparent; }
menubutton.flat > button:hover, menubutton.flat > button:checked {
  background: @HOVER@;
}
flowbox, flowboxchild { background: transparent; padding: 0; }
flowboxchild:focus-visible { outline: none; }
button.suggested-action, button.primary {
  background: @ACCENT@;
  color: @ONACCENT@;
  border-color: @ACCENT@;
  font-weight: 700;
}
button.suggested-action:hover, button.primary:hover {
  background: @ACCENTHOVER@;
}
button.danger:hover { color: @RED@; }

switch {
  background: @BGINPUT@;
  border: 1px solid @BORDER@;
  border-radius: 999px;
  min-height: 18px;
}
switch:checked { background: @ACCENT@; border-color: @ACCENT@; }
switch > slider {
  background: @FG@;
  border-radius: 999px;
  min-width: 16px;
  min-height: 16px;
  margin: 1px;
  box-shadow: none;
  border: none;
}
switch:checked > slider { background: @ONACCENT@; }

checkbutton check {
  background: @BGINPUT@;
  border: 1px solid @BORDER@;
  border-radius: 4px;
}
checkbutton check:checked { background: @ACCENT@; color: @ONACCENT@; }

/* ---- Popovers & lists --------------------------------------------- */
popover > contents, popover.menu > contents {
  background: @BGDARK@;
  color: @FG@;
  border: 1px solid @BORDERSTRONG@;
  border-radius: @R@px;
  box-shadow: 0 4px 14px rgba(0,0,0,0.22);
  padding: 4px;
}
popover > arrow { background: @BGDARK@; border: 1px solid @BORDERSTRONG@; }
popover listview, popover list { background: transparent; }
popover listview > row, popover list > row {
  border-radius: @RS@px;
  padding: 6px 8px;
}
popover listview > row:hover, popover list > row:hover { background: @HOVER@; }
popover listview > row:selected, popover list > row:selected {
  background: @ACCENTSOFT@;
  color: @FGBRIGHT@;
}
modelbutton { border-radius: @RS@px; padding: 6px 10px; }
modelbutton:hover { background: @HOVER@; }

list, listview { background: transparent; color: @FG@; }
list > row { border-radius: @R@px; outline: none; }
list > row:hover { background: @HOVER@; }
list > row:selected { background: @ACCENTSOFT@; color: @FGBRIGHT@; }
list > row:focus-visible { box-shadow: inset 0 0 0 1px @ACCENT@; }
.city-row { padding: 6px 8px; }
.city-row .city-name { font-weight: 700; }
.city-row .city-meta { font-size: 13px; color: @MUTED@; }
.saved-row { padding: 7px 8px; }
.saved-row .saved-name { font-weight: 700; }
.saved-row .saved-meta { font-size: 13px; color: @MUTED@; }
.saved-row button { min-height: 22px; min-width: 22px; padding: 0 4px; opacity: 0.0; }
.saved-row:hover button { opacity: 1.0; }
.empty-state { color: @MUTED@; font-size: 13px; padding: 10px 4px; }

/* ---- View switcher -------------------------------------------------- */
stackswitcher { background: transparent; }
stackswitcher button {
  background: transparent;
  border: none;
  border-radius: @R@px;
  color: @FGDARK@;
  min-height: 28px;
  min-width: 0;
  padding: 0 12px;
  margin: 0 1px;
  font-weight: 600;
}
stackswitcher button:hover { color: @FG@; background: @HOVER@; }
stackswitcher button:checked {
  color: @ACCENT@;
  background: @ACCENTSOFT@;
}

/* ---- Content -------------------------------------------------------- */
.hero { padding: 18px 24px 6px 24px; }
.hero-name {
  font-size: 26px;
  font-weight: 800;
  color: @FGBRIGHT@;
}
.hero-meta { color: @FGDARK@; font-size: 14px; }
.chip {
  background: @BGDARK@;
  border: 1px solid @BORDER@;
  border-radius: 999px;
  padding: 3px 12px 3px 8px;
}
.chip .chip-glyph { font-size: 16px; color: @ACCENT@; }
.chip .chip-label { font-size: 13px; color: @MUTED@; }
.chip .chip-value { font-weight: 700; }

.card {
  background: @BGDARK@;
  border: 1px solid @BORDER@;
  border-radius: @R@px;
}
.card-title {
  font-size: 12.5px;
  font-weight: 800;
  letter-spacing: 2px;
  color: @MUTED@;
  padding: 12px 14px 4px 14px;
}
.table-head {
  font-size: 12.5px;
  font-weight: 700;
  letter-spacing: 1px;
  color: @MUTED@;
}
.table-cell { padding: 5px 8px; }
.table-row-alt { background: @ZEBRA@; }
.glyph {
  font-family: "Noto Sans Symbols", "Noto Sans Symbols 2", "@FONT@";
  font-size: 18px;
}
.retro { color: @RED@; font-weight: 700; }
.muted { color: @MUTED@; }
.accent { color: @ACCENT@; }
.strong { font-weight: 700; color: @FGBRIGHT@; }
.big-number { font-size: 28px; font-weight: 800; color: @FGBRIGHT@; }

.body-row { padding: 3px 10px; border-radius: @RS@px; }
.body-row:hover { background: @HOVER@; }
.body-row.focused { background: @ACCENTSOFT@; }

levelbar trough, progressbar trough {
  background: @BGINPUT@;
  border: none;
  border-radius: 999px;
  min-height: 8px;
}
levelbar block.filled, levelbar block.low, levelbar block.high,
levelbar block.full, progressbar progress {
  background: @ACCENT@;
  border-radius: 999px;
}

textview, textview text {
  background: @BGDARK@;
  color: @FG@;
}
textview { font-size: 14px; font-family: "@MONO@", monospace; }
.report-view { border-radius: @R@px; }
.report-list > row { padding: 8px 10px; }
.report-list .report-title { font-weight: 700; }
.report-list .report-desc { font-size: 13px; color: @MUTED@; }

scrollbar { background: transparent; border: none; }
scrollbar slider {
  background: @SCROLL@;
  border-radius: 999px;
  min-width: 4px;
  min-height: 4px;
  margin: 2px;
  border: none;
}
scrollbar trough { background: transparent; }
scrollbar slider:hover { background: @MUTED@; }

/* Pane handles: a wide invisible grab area with a thin line that lights
   up on hover so it's easy to find and drag. */
paned > separator {
  min-width: 9px;
  min-height: 9px;
  background: none;
  background-image: linear-gradient(@BORDER@, @BORDER@);
  background-size: 1px 40%;
  background-position: center;
  background-repeat: no-repeat;
}
paned.vertical > separator { background-size: 40% 1px; }
paned > separator:hover {
  background-image: linear-gradient(@ACCENT@, @ACCENT@);
  background-size: 3px 60%;
}
paned.vertical > separator:hover { background-size: 60% 3px; }

tooltip, tooltip.background {
  background: @BGDARKER@;
  color: @FG@;
  border: 1px solid @BORDERSTRONG@;
  border-radius: @RS@px;
}
.toast {
  background: @BGDARKER@;
  color: @FGBRIGHT@;
  border: 1px solid @ACCENT@;
  border-radius: @R@px;
  padding: 8px 16px;
  margin: 16px;
}
calendar {
  background: @BGDARK@;
  color: @FG@;
  border: none;
}
calendar > grid > label.day-number:selected {
  background: @ACCENT@;
  color: @ONACCENT@;
  border-radius: @RS@px;
}
)CSS";
  bool dark = p.dark;
  Rgb onAccent = p.onAccent;
  Replace(css, "@FONT@", p.font);
  Replace(css, "@MONO@", p.mono);
  Replace(css, "@BGDARKER@", Hex(p.bgDarker));
  Replace(css, "@BGDARK@", Hex(p.bgDark));
  Replace(css, "@BGINPUT@", Hex(dark ? p.bgDarker : Mix(p.bg, Rgb{1,1,1}, 0.6)));
  Replace(css, "@BG@", Hex(p.bg));
  Replace(css, "@FGBRIGHT@", Hex(p.fgBright));
  Replace(css, "@FGDARK@", Hex(p.soft));
  Replace(css, "@FG@", Hex(p.fg));
  Replace(css, "@MUTED@", Hex(p.dim));
  Replace(css, "@ACCENTSOFT@", Rgba(p.accent, 0.18));
  Replace(css, "@ACCENTHOVER@", Hex(Mix(p.accent, p.fgBright, 0.15)));
  Replace(css, "@ACCENT@", Hex(p.accent));
  Replace(css, "@ONACCENT@", Hex(onAccent));
  Replace(css, "@SELECTION@", Hex(p.selection));
  Replace(css, "@SELFG@", Hex(p.selectionFg));
  Replace(css, "@BORDERSTRONG@", Rgba(p.fg, 0.22));
  Replace(css, "@BORDER@", Rgba(p.fg, 0.10));
  Replace(css, "@HOVER@", Rgba(p.fg, 0.07));
  Replace(css, "@ZEBRA@", Rgba(p.fg, 0.03));
  Replace(css, "@SCROLL@", Rgba(p.fg, 0.25));
  Replace(css, "@RED@", Hex(p.red));
  Replace(css, "@RS@", std::to_string(std::max(0, p.radius - 3)));
  Replace(css, "@R@", std::to_string(p.radius));
  return css;
}

void Apply() {
  std::string css = BuildCss();
  gtk_css_provider_load_from_string(g_provider, css.c_str());
  GtkSettings *settings = gtk_settings_get_default();
  if (settings)
    g_object_set(settings, "gtk-application-prefer-dark-theme",
      (gboolean)g_pal.dark, NULL);
}

gboolean ReloadNow(gpointer) {
  g_reloadSource = 0;
  LoadPalette();
  Apply();
  if (g_onChange) g_onChange();
  return G_SOURCE_REMOVE;
}

void OnChanged(GFileMonitor *, GFile *, GFile *, GFileMonitorEvent ev,
  gpointer) {
  if (ev == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED) return;
  // `omarchy theme set` swaps several files; wait for it to settle.
  if (g_reloadSource) g_source_remove(g_reloadSource);
  g_reloadSource = g_timeout_add(350, ReloadNow, nullptr);
}

}  // namespace

const Palette &Current() { return g_pal; }

void Install(std::function<void()> onChange) {
  g_onChange = std::move(onChange);
  LoadPalette();
  g_provider = gtk_css_provider_new();
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
    GTK_STYLE_PROVIDER(g_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
  Apply();

  GFile *dir = g_file_new_for_path(StateDir().c_str());
  g_monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_WATCH_MOVES,
    nullptr, nullptr);
  if (g_monitor)
    g_signal_connect(g_monitor, "changed", G_CALLBACK(OnChanged), nullptr);
  g_object_unref(dir);

  std::string hypr = std::string(g_get_home_dir()) + "/.config/hypr";
  GFile *hdir = g_file_new_for_path(hypr.c_str());
  g_hyprMonitor = g_file_monitor_directory(hdir, G_FILE_MONITOR_NONE, nullptr,
    nullptr);
  if (g_hyprMonitor)
    g_signal_connect(g_hyprMonitor, "changed", G_CALLBACK(OnChanged), nullptr);
  g_object_unref(hdir);
}

std::string Hex(const Rgb &c) {
  char buf[16];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x",
    (int)lround(std::clamp(c.r, 0.0, 1.0) * 255),
    (int)lround(std::clamp(c.g, 0.0, 1.0) * 255),
    (int)lround(std::clamp(c.b, 0.0, 1.0) * 255));
  return buf;
}

Rgb Mix(const Rgb &a, const Rgb &b, double t) {
  return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
    a.b + (b.b - a.b) * t};
}

void SetSource(cairo_t *cr, const Rgb &c, double alpha) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

Rgb ElementColor(int element) {
  switch (element) {
  case 0: return g_pal.red;
  case 1: return g_pal.green;
  case 2: return g_pal.yellow;
  default: return g_pal.blue;
  }
}

Rgb AspectColor(int type) {
  switch (type) {
  case 1: return g_pal.accent;   // Conjunction
  case 2: return g_pal.red;      // Opposition
  case 3: return g_pal.orange;   // Square
  case 4: return g_pal.blue;     // Trine
  case 5: return g_pal.cyan;     // Sextile
  default: return g_pal.magenta; // Minor aspects
  }
}

Rgb BodyColor(int id) {
  switch (id) {
  case 1: return g_pal.yellow;   // Sun
  case 2: return g_pal.fgBright; // Moon
  case 3: return g_pal.cyan;     // Mercury
  case 4: return g_pal.green;    // Venus
  case 5: return g_pal.red;      // Mars
  case 6: return g_pal.orange;   // Jupiter
  case 7: return g_pal.brown;    // Saturn
  case 8: return g_pal.cyan;     // Uranus
  case 9: return g_pal.blue;     // Neptune
  case 10: return g_pal.magenta; // Pluto
  default: return g_pal.soft;
  }
}

std::string GlyphFamily() {
  return "Noto Sans Symbols, Noto Sans Symbols 2, " + g_pal.font;
}

}  // namespace theme
