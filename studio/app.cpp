// Copyright (C) 2026 Ryan Arthur. Part of Astrolog Studio, a modified
// version of Astrolog 8.00, which is Copyright (C) 1991-2026 by Walter D.
// Pullen (see README.md and the notices in the Astrolog source files).
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
//
// Astrolog Studio - a GTK 4 front end for the Astrolog 8.00 engine.
//
// app.cpp: the main window. A sidebar holds the birth data form (with atlas
// city search and automatic historical time zones), chart options and the
// saved chart library. The main area shows the chart as a wheel, position
// tables, aspect grid, element balance and Astrolog's text reports. Every
// edit recasts the chart live.

#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "engine.h"
#include "render.h"
#include "store.h"
#include "theme.h"

using astro::Aspect;
using astro::BirthData;
using astro::Body;
using astro::Chart;
using astro::City;

// G_CALLBACK for lambdas: variadic so commas inside a lambda body are safe.
#define CB(...) ((GCallback)(__VA_ARGS__))

namespace {

const char *const kMonths[] = {"January", "February", "March", "April", "May",
  "June", "July", "August", "September", "October", "November", "December",
  nullptr};
const char *const kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday"};

enum ZoneMode { kZoneAuto = 0, kZoneManual = 1, kZoneLmt = 2 };

struct ReportDef {
  const char *title;
  const char *desc;
  std::vector<std::string> sw;
  bool prose = false;  // Reflowing text (let GTK wrap) vs. a fixed table.
};

const std::vector<ReportDef> &Reports() {
  static const std::vector<ReportDef> v = {
    {"Interpretation", "What each placement means",
      {"-v", "-I", ":I", "200"}, true},
    {"Aspect meanings", "Interpretation of every aspect",
      {"-a", "-I", ":I", "200"}, true},
    {"Chart listing", "Astrolog's classic -v summary", {"-v"}},
    {"Aspect list", "All aspects with orbs and power", {"-a"}},
    {"Midpoints", "Midpoints between every pair", {"-m"}},
    {"Arabic parts", "Lots and Arabic parts", {"-P"}},
    {"Influence", "Planetary power and dominance", {"-j"}},
    {"Esoteric", "Rays and esoteric rulerships", {"-7"}},
    {"Transits (month)", "Transits to natal this month", {"-t"}},
    {"Progressions", "Secondary progressed aspects", {"-tp"}},
    {"Day events", "Exact aspects on the birth day", {"-d"}},
    {"Rising & setting", "Planet rise/set/culmination", {"-Zd"}},
    {"Horizon", "Altitude & azimuth of each body", {"-Z"}},
    {"Astro-graph", "Planetary lines on the globe", {"-L"}},
    {"Solar system", "Heliocentric orbit view", {"-S"}},
    {"Text wheel", "The classic ASCII chart wheel", {"-w"}},
  };
  return v;
}

struct App {
  GtkApplication *app = nullptr;
  GtkWindow *win = nullptr;

  // Birth data form.
  GtkEntry *name = nullptr;
  GtkSpinButton *day = nullptr, *year = nullptr;
  GtkDropDown *month = nullptr;
  GtkSpinButton *hour = nullptr, *minute = nullptr, *second = nullptr;
  GtkEntry *city = nullptr;
  GtkPopover *cityPop = nullptr;
  GtkListBox *cityList = nullptr;
  std::vector<City> cityResults;
  GtkEntry *lat = nullptr, *lon = nullptr;
  GtkDropDown *zoneMode = nullptr;
  GtkWidget *manualBox = nullptr;
  GtkSpinButton *offset = nullptr;
  GtkSwitch *dstSwitch = nullptr;
  GtkLabel *zoneName = nullptr, *zoneDetail = nullptr;
  GtkLabel *status = nullptr;

  // Options.
  GtkDropDown *house = nullptr, *zodiac = nullptr;
  GtkSwitch *asteroids = nullptr, *points = nullptr, *minor = nullptr,
    *trueNode = nullptr, *transits = nullptr;
  GtkDropDown *textSize = nullptr;

  // Library.
  GtkListBox *savedList = nullptr;
  std::vector<BirthData> saved;

  // Content.
  GtkRevealer *sidebar = nullptr;
  GtkStack *stack = nullptr;
  GtkLabel *heroName = nullptr, *heroMeta = nullptr;
  GtkFlowBox *chips = nullptr;
  GtkDrawingArea *wheel = nullptr, *grid = nullptr, *balance = nullptr;
  GtkBox *bodyList = nullptr;
  std::vector<GtkWidget *> bodyRows;
  GtkBox *focusBox = nullptr;
  GtkGrid *posGrid = nullptr, *cuspGrid = nullptr;
  GtkBox *aspectList = nullptr;
  GtkListBox *reportList = nullptr;
  GtkTextView *reportView = nullptr;
  GtkLabel *reportTitle = nullptr;
  GtkSpinner *reportSpin = nullptr;
  int reportIndex = 0;
  guint reportSerial = 0;
  GtkRevealer *toast = nullptr;
  // Responsive layout: pages stack vertically when the content is narrow.
  GtkWidget *wheelPage = nullptr, *wheelSide = nullptr, *bodyScroll = nullptr;
  GtkWidget *aspPage = nullptr, *aspCard = nullptr, *gridScroll = nullptr;
  int narrow = -1;
  bool wheelPaneSet = false, aspPaneSet = false;
  guint prefsSource = 0;
  GtkLabel *toastLabel = nullptr;
  guint toastSource = 0;

  Chart chart;
  Chart transit;
  std::vector<Aspect> trAsp;
  render::WheelState ws;
  render::GridState gs;

  store::Prefs prefs;
  BirthData current;   // Last birth data read from the form.
  City chosen;         // Location picked from the atlas.
  bool loading = false;
  guint recastSource = 0;
  guint searchSource = 0;
  bool pointerInCityPop = false;
  bool startNow = false;  // --now: open with a chart for this moment, here.
  std::string lastTheme;
};

App A;

// ---------------------------------------------------------------------------
// Small widget helpers.

std::string Esc(const std::string &s) {
  gchar *e = g_markup_escape_text(s.c_str(), -1);
  std::string r = e;
  g_free(e);
  return r;
}

GtkWidget *Label(const std::string &text, const char *cls = nullptr,
  float xalign = 0.0f) {
  GtkWidget *l = gtk_label_new(text.c_str());
  gtk_label_set_xalign(GTK_LABEL(l), xalign);
  if (cls) gtk_widget_add_css_class(l, cls);
  return l;
}

GtkWidget *Markup(const std::string &markup, const char *cls = nullptr,
  float xalign = 0.0f) {
  GtkWidget *l = gtk_label_new(nullptr);
  gtk_label_set_markup(GTK_LABEL(l), markup.c_str());
  gtk_label_set_xalign(GTK_LABEL(l), xalign);
  if (cls) gtk_widget_add_css_class(l, cls);
  return l;
}

// Astrological glyphs are drawn in small Cairo widgets rather than put in
// labels: symbol fonts have tall line metrics that would make rows uneven.
struct GlyphData {
  std::string glyph;
  theme::Rgb color;
  double px;
};

GtkWidget *GlyphW(const std::string &glyph, const theme::Rgb &color,
  double px) {
  px *= theme::TextScale();
  auto *d = new GlyphData{glyph, color, px};
  bool text = !glyph.empty() && (unsigned char)glyph[0] < 0x80;
  GtkWidget *da = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da),
    (int)std::ceil(px * (text ? 1.5 : 1.3)));
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da),
    (int)std::ceil(px * 1.2));
  gtk_widget_set_valign(da, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), [](GtkDrawingArea *,
    cairo_t *cr, int w, int h, gpointer p) {
    auto *g = (GlyphData *)p;
    render::DrawGlyph(cr, w / 2.0, h / 2.0, g->glyph, g->color, g->px);
  }, d, [](gpointer p) { delete (GlyphData *)p; });
  return da;
}

GtkWidget *SignW(double lon, double px = 17) {
  int s = (int)(std::fmod(lon + 360.0, 360.0) / 30.0);
  return GlyphW(astro::SignGlyph(s),
    theme::ElementColor(astro::SignElement(s)), px);
}

GtkWidget *BodyW(const Body &b, double px = 19, bool transit = false) {
  theme::Rgb c = transit || b.kind == astro::kAngle ?
    theme::Current().accent : theme::BodyColor(b.id);
  return GlyphW(b.glyph, c, px);
}

// "12° [sign] 09'" position, with the sign drawn as a colored glyph.
GtkWidget *PositionW(double lon) {
  lon = std::fmod(lon + 360.0, 360.0);
  int s = (int)(lon / 30.0);
  double d = lon - s * 30.0;
  int deg = (int)d, min = (int)((d - deg) * 60.0);
  char a[16], b[16];
  snprintf(a, sizeof(a), "%2d°", deg);
  snprintf(b, sizeof(b), "%02d'", min);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_append(GTK_BOX(box), Label(a, "strong"));
  gtk_box_append(GTK_BOX(box), SignW(lon, 17));
  gtk_box_append(GTK_BOX(box), Label(b));
  return box;
}

void ClearChildren(GtkWidget *w) {
  GtkWidget *c;
  while ((c = gtk_widget_get_first_child(w)) != nullptr) {
    if (GTK_IS_GRID(w)) gtk_grid_remove(GTK_GRID(w), c);
    else if (GTK_IS_BOX(w)) gtk_box_remove(GTK_BOX(w), c);
    else if (GTK_IS_LIST_BOX(w)) gtk_list_box_remove(GTK_LIST_BOX(w), c);
    else if (GTK_IS_FLOW_BOX(w)) gtk_flow_box_remove(GTK_FLOW_BOX(w), c);
    else gtk_widget_unparent(c);
  }
}

GtkWidget *Section(const char *title, bool first = false) {
  GtkWidget *l = Label(title, "section-title");
  if (first) gtk_widget_add_css_class(l, "first");
  return l;
}

GtkWidget *Field(const char *label, GtkWidget *child) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_append(GTK_BOX(box), Label(label, "field-label"));
  gtk_box_append(GTK_BOX(box), child);
  return box;
}

GtkWidget *SwitchRow(const char *label, const char *hint, GtkSwitch **out) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(text, TRUE);
  gtk_box_append(GTK_BOX(text), Label(label));
  if (hint) gtk_box_append(GTK_BOX(text), Label(hint, "hint"));
  GtkWidget *sw = gtk_switch_new();
  gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(row), text);
  gtk_box_append(GTK_BOX(row), sw);
  gtk_widget_set_margin_top(row, 6);
  *out = GTK_SWITCH(sw);
  return row;
}

GtkWidget *Card(const char *title, GtkWidget *child) {
  GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(card, "card");
  if (title) gtk_box_append(GTK_BOX(card), Label(title, "card-title"));
  gtk_box_append(GTK_BOX(card), child);
  return card;
}

GtkWidget *Scrolled(GtkWidget *child, bool hscroll = false) {
  GtkWidget *sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
    hscroll ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), child);
  gtk_widget_set_vexpand(sw, TRUE);
  return sw;
}

void Toast(const std::string &text) {
  gtk_label_set_text(A.toastLabel, text.c_str());
  gtk_revealer_set_reveal_child(A.toast, TRUE);
  if (A.toastSource) g_source_remove(A.toastSource);
  A.toastSource = g_timeout_add(2400, [](gpointer) -> gboolean {
    gtk_revealer_set_reveal_child(A.toast, FALSE);
    A.toastSource = 0;
    return G_SOURCE_REMOVE;
  }, nullptr);
}

// ---------------------------------------------------------------------------
// Parsing and formatting.

// Parse a latitude or longitude: "40.7128", "-74.006", "40N43", "74°00'W",
// "40 43 12 N". Returns false on bad input.
bool ParseCoord(const std::string &s, double maxAbs, char posCh, char negCh,
  double *out) {
  std::vector<double> nums;
  int sign = 1;
  bool hemi = false;
  std::string cur;
  auto flush = [&]() {
    if (!cur.empty()) {
      nums.push_back(g_ascii_strtod(cur.c_str(), nullptr));
      cur.clear();
    }
  };
  for (size_t i = 0; i < s.size(); i++) {
    char ch = s[i];
    if (g_ascii_isdigit(ch) || ch == '.') {
      cur += ch;
    } else {
      flush();
      char up = g_ascii_toupper(ch);
      if (ch == '-' && nums.empty()) sign = -1;
      else if (up == negCh) { sign = -1; hemi = true; }
      else if (up == posCh) { hemi = true; }
    }
  }
  flush();
  if (nums.empty() || nums.size() > 3) return false;
  double v = nums[0];
  if (nums.size() > 1) v += nums[1] / 60.0;
  if (nums.size() > 2) v += nums[2] / 3600.0;
  (void)hemi;
  v *= sign;
  if (std::fabs(v) > maxAbs) return false;
  *out = v;
  return true;
}

std::string LongDate(const BirthData &b) {
  GDateTime *dt = g_date_time_new_utc(std::clamp(b.year, 1, 9999), b.month,
    b.day, 12, 0, 0);
  std::string wd;
  if (dt) {
    wd = kWeekdays[g_date_time_get_day_of_week(dt) % 7];
    g_date_time_unref(dt);
  }
  char buf[128];
  snprintf(buf, sizeof(buf), "%s%s%d %s %d", wd.c_str(), wd.empty() ? "" : ", ",
    b.day, kMonths[b.month - 1], b.year);
  return buf;
}

std::string ZoneSummary(const BirthData &b) {
  if (b.lmt) return "Local Mean Time";
  std::string s = astro::FormatOffset(b.utcOffset + b.dst);
  if (b.dst != 0.0) s += " · daylight time";
  return s;
}

// ---------------------------------------------------------------------------
// Form <-> BirthData.

void ScheduleRecast();
void Recast();
void UpdateViews();
void ApplyTextSize();
void SchedulePrefsSave();

BirthData ReadForm(std::string *error) {
  BirthData b;
  b.name = gtk_editable_get_text(GTK_EDITABLE(A.name));
  if (b.name.empty()) b.name = "Untitled";
  b.day = gtk_spin_button_get_value_as_int(A.day);
  b.month = (int)gtk_drop_down_get_selected(A.month) + 1;
  b.year = gtk_spin_button_get_value_as_int(A.year);
  b.hour = gtk_spin_button_get_value_as_int(A.hour);
  b.minute = gtk_spin_button_get_value_as_int(A.minute);
  b.second = gtk_spin_button_get_value_as_int(A.second);
  b.location = gtk_editable_get_text(GTK_EDITABLE(A.city));
  std::string lat = gtk_editable_get_text(GTK_EDITABLE(A.lat));
  std::string lon = gtk_editable_get_text(GTK_EDITABLE(A.lon));
  if (!ParseCoord(lat, 90.0, 'N', 'S', &b.lat)) {
    if (error) *error = "Latitude not understood — try 40.71 or 40N43";
  }
  if (!ParseCoord(lon, 180.0, 'E', 'W', &b.lon)) {
    if (error && error->empty())
      *error = "Longitude not understood — try -74.0 or 74W00";
  }
  b.zoneIndex = A.chosen.zoneIndex;
  b.zoneName = A.chosen.zoneName;
  int mode = (int)gtk_drop_down_get_selected(A.zoneMode);
  if (mode == kZoneAuto && b.zoneIndex >= 0) {
    if (!astro::ResolveZone(b)) mode = kZoneManual;
  } else if (mode == kZoneAuto) {
    mode = kZoneManual;
  }
  if (mode == kZoneManual) {
    b.lmt = false;
    b.utcOffset = gtk_spin_button_get_value(A.offset);
    b.dst = gtk_switch_get_active(A.dstSwitch) ? 1.0 : 0.0;
  } else if (mode == kZoneLmt) {
    b.lmt = true;
    b.dst = 0.0;
    b.utcOffset = b.lon / 15.0;
  }
  return b;
}

std::string CoordText(double v, char pos, char neg) {
  return astro::FormatDegree(v, pos, neg);
}

void SetCoords(double lat, double lon) {
  gtk_editable_set_text(GTK_EDITABLE(A.lat), CoordText(lat, 'N', 'S').c_str());
  gtk_editable_set_text(GTK_EDITABLE(A.lon), CoordText(lon, 'E', 'W').c_str());
}

void SyncZoneWidgets(const BirthData &b) {
  int mode = (int)gtk_drop_down_get_selected(A.zoneMode);
  bool autoOk = A.chosen.zoneIndex >= 0;
  gtk_widget_set_visible(A.manualBox, mode == kZoneManual ||
    (mode == kZoneAuto && !autoOk));
  std::string title, detail;
  if (mode == kZoneLmt) {
    title = "Local Mean Time";
    detail = "Clock time from longitude (" +
      astro::FormatOffset(b.lon / 15.0) + ")";
  } else if (mode == kZoneAuto && autoOk) {
    title = A.chosen.zoneName;
    detail = b.lmt ? "Before standard time — using Local Mean Time" :
      ZoneSummary(b) + " on this date";
  } else if (mode == kZoneAuto) {
    title = "No time zone yet";
    detail = "Pick a city, or set the offset below";
  } else {
    title = "Manual offset";
    detail = ZoneSummary(b);
  }
  gtk_label_set_text(A.zoneName, title.c_str());
  gtk_label_set_text(A.zoneDetail, detail.c_str());
  if (mode == kZoneAuto && autoOk && !b.lmt) {
    // Keep manual controls in step so switching modes starts from here.
    A.loading = true;
    gtk_spin_button_set_value(A.offset, b.utcOffset);
    gtk_switch_set_active(A.dstSwitch, b.dst != 0.0);
    A.loading = false;
  }
}

void LoadIntoForm(const BirthData &b) {
  A.loading = true;
  gtk_editable_set_text(GTK_EDITABLE(A.name), b.name.c_str());
  gtk_spin_button_set_value(A.year, b.year);
  gtk_drop_down_set_selected(A.month, std::clamp(b.month, 1, 12) - 1);
  gtk_spin_button_set_value(A.day, b.day);
  gtk_spin_button_set_value(A.hour, b.hour);
  gtk_spin_button_set_value(A.minute, b.minute);
  gtk_spin_button_set_value(A.second, b.second);
  gtk_editable_set_text(GTK_EDITABLE(A.city), b.location.c_str());
  SetCoords(b.lat, b.lon);
  A.chosen = City();
  A.chosen.display = b.location;
  A.chosen.zoneIndex = b.zoneIndex;
  A.chosen.zoneName = b.zoneName;
  A.chosen.lat = b.lat;
  A.chosen.lon = b.lon;
  gtk_spin_button_set_value(A.offset, b.utcOffset);
  gtk_switch_set_active(A.dstSwitch, b.dst != 0.0);
  gtk_drop_down_set_selected(A.zoneMode, b.lmt && b.zoneIndex < 0 ? kZoneLmt :
    (b.zoneIndex >= 0 ? kZoneAuto : kZoneManual));
  A.loading = false;
  gtk_widget_set_visible(GTK_WIDGET(A.cityPop), FALSE);
  Recast();
}

// Set only the date and time fields to the current moment (the sidebar clock
// button), keeping the rest of the chart as entered.
void SetTimeNow() {
  GDateTime *now = g_date_time_new_now_local();
  A.loading = true;
  gtk_spin_button_set_value(A.year, g_date_time_get_year(now));
  gtk_drop_down_set_selected(A.month, g_date_time_get_month(now) - 1);
  gtk_spin_button_set_value(A.day, g_date_time_get_day_of_month(now));
  gtk_spin_button_set_value(A.hour, g_date_time_get_hour(now));
  gtk_spin_button_set_value(A.minute, g_date_time_get_minute(now));
  gtk_spin_button_set_value(A.second, g_date_time_get_second(now));
  A.loading = false;
  g_date_time_unref(now);
  Recast();
}

// The system's IANA time zone, e.g. "America/Detroit".
std::string SystemZone() {
  const char *tz = g_getenv("TZ");
  if (tz && *tz && *tz != ':') return tz;
  gchar *link = g_file_read_link("/etc/localtime", nullptr);
  std::string s = link ? link : "";
  g_free(link);
  size_t p = s.find("zoneinfo/");
  return p == std::string::npos ? "" : s.substr(p + 9);
}

// Where "now" is. Omarchy's weather location
// (~/.local/state/omarchy/settings/weather.json, set with
// `omarchy weather location`) gives exact coordinates; otherwise use the
// largest atlas city in the system time zone. The time zone is always the
// system's, since that is the clock "now" is read from. No network lookups.
void CurrentPlace(BirthData *b) {
  std::string zone = SystemZone();
  City c;
  bool have = false;
  std::string file = std::string(g_get_home_dir()) +
    "/.local/state/omarchy/settings/weather.json";
  gchar *text = nullptr;
  if (g_file_get_contents(file.c_str(), &text, nullptr, nullptr)) {
    // Tiny reader for the flat {"name", "latitude", "longitude"} object.
    auto field = [&](const char *key) -> std::string {
      std::string t = text, k = std::string("\"") + key + "\"";
      size_t p = t.find(k);
      if (p == std::string::npos) return "";
      p = t.find(':', p);
      if (p == std::string::npos) return "";
      p = t.find_first_not_of(" \t\n", p + 1);
      if (p == std::string::npos) return "";
      if (t[p] == '"') {
        size_t e = t.find('"', p + 1);
        return e == std::string::npos ? "" : t.substr(p + 1, e - p - 1);
      }
      size_t e = t.find_first_of(",}\n", p);
      return t.substr(p, e - p);
    };
    std::string lat = field("latitude"), lon = field("longitude");
    std::string name = field("name");
    if (!lat.empty() && !lon.empty()) {
      c.lat = g_ascii_strtod(lat.c_str(), nullptr);
      c.lon = g_ascii_strtod(lon.c_str(), nullptr);
      City near;
      // Use the atlas name (with region and country) if a city is close by.
      if (astro::NearestCity(c.lat, c.lon, 15.0, &near))
        c.display = near.display;
      else
        c.display = name.empty() ? "Current location" : name;
      have = true;
    }
    g_free(text);
  }
  if (!have && !astro::CityForZone(zone, &c)) {
    c.display = "Greenwich, United Kingdom";
    c.lat = 51.4779;
    c.lon = -0.0015;
    zone = "Europe/London";
  }
  b->location = c.display;
  b->lat = c.lat;
  b->lon = c.lon;
  b->zoneName = zone;
  b->zoneIndex = astro::ZoneIndex(zone);
}

// Cast a chart for this moment, here: named "Now", at the current location
// and time zone.
void CastNow() {
  BirthData b;
  b.name = "Now";
  CurrentPlace(&b);
  GDateTime *now = g_date_time_new_now_local();
  b.year = g_date_time_get_year(now);
  b.month = g_date_time_get_month(now);
  b.day = g_date_time_get_day_of_month(now);
  b.hour = g_date_time_get_hour(now);
  b.minute = g_date_time_get_minute(now);
  b.second = g_date_time_get_second(now);
  b.utcOffset = g_date_time_get_utc_offset(now) / 3.6e9;  // Fallback only.
  g_date_time_unref(now);
  LoadIntoForm(b);
}

// ---------------------------------------------------------------------------
// Casting.

astro::Settings ReadSettings() {
  astro::Settings s;
  s.houseSystem = (int)gtk_drop_down_get_selected(A.house);
  s.sidereal = gtk_drop_down_get_selected(A.zodiac) == 1;
  s.asteroids = gtk_switch_get_active(A.asteroids);
  s.points = gtk_switch_get_active(A.points);
  s.minorAspects = gtk_switch_get_active(A.minor);
  s.trueNode = gtk_switch_get_active(A.trueNode);
  return s;
}

void Recast() {
  if (A.recastSource) {
    g_source_remove(A.recastSource);
    A.recastSource = 0;
  }
  std::string err;
  BirthData b = ReadForm(&err);
  SyncZoneWidgets(b);
  if (b.day > g_date_get_days_in_month((GDateMonth)b.month,
    (GDateYear)std::clamp(b.year, 1, 9999)))
    err = "That month doesn't have that many days";
  if (!err.empty()) {
    gtk_label_set_text(A.status, err.c_str());
    gtk_widget_add_css_class(GTK_WIDGET(A.status), "error");
    return;
  }
  astro::Settings s = ReadSettings();
  Chart c = astro::Cast(b, s);
  if (!c.ok) {
    gtk_label_set_text(A.status, c.error.c_str());
    gtk_widget_add_css_class(GTK_WIDGET(A.status), "error");
    return;
  }
  gtk_widget_remove_css_class(GTK_WIDGET(A.status), "error");
  char jd[64];
  snprintf(jd, sizeof(jd), "Julian day %.5f", c.julianDay);
  gtk_label_set_text(A.status, jd);
  A.current = b;
  A.chart = c;

  A.transit = Chart();
  A.trAsp.clear();
  if (gtk_switch_get_active(A.transits)) {
    GDateTime *now = g_date_time_new_now_utc();
    BirthData t = b;
    t.name = "Transits";
    t.year = g_date_time_get_year(now);
    t.month = g_date_time_get_month(now);
    t.day = g_date_time_get_day_of_month(now);
    t.hour = g_date_time_get_hour(now);
    t.minute = g_date_time_get_minute(now);
    t.second = g_date_time_get_second(now);
    t.utcOffset = 0.0;
    t.dst = 0.0;
    t.lmt = false;
    g_date_time_unref(now);
    astro::Settings ts = s;
    ts.points = false;
    A.transit = astro::Cast(t, ts);
    if (A.transit.ok) A.trAsp = astro::CrossAspects(A.transit, A.chart, s);
  }
  A.ws.hoverBody = A.ws.hoverTransit = -1;
  UpdateViews();
}

void ScheduleRecast() {
  if (A.loading) return;
  if (A.recastSource) g_source_remove(A.recastSource);
  A.recastSource = g_timeout_add(90, [](gpointer) -> gboolean {
    A.recastSource = 0;
    Recast();
    return G_SOURCE_REMOVE;
  }, nullptr);
}

void OnFormChanged(GObject *, gpointer) { ScheduleRecast(); }
void OnFormNotify(GObject *, GParamSpec *, gpointer) { ScheduleRecast(); }

// ---------------------------------------------------------------------------
// City search.

void ChooseCity(int i) {
  if (i < 0 || i >= (int)A.cityResults.size()) return;
  A.chosen = A.cityResults[i];
  A.pointerInCityPop = false;
  A.loading = true;
  gtk_editable_set_text(GTK_EDITABLE(A.city), A.chosen.display.c_str());
  gtk_editable_set_position(GTK_EDITABLE(A.city), -1);
  SetCoords(A.chosen.lat, A.chosen.lon);
  gtk_drop_down_set_selected(A.zoneMode, kZoneAuto);
  A.loading = false;
  gtk_popover_popdown(A.cityPop);
  Recast();
}

void RunCitySearch() {
  std::string q = gtk_editable_get_text(GTK_EDITABLE(A.city));
  ClearChildren(GTK_WIDGET(A.cityList));
  A.cityResults = astro::SearchCities(q, 9);
  if (A.cityResults.empty() || q == A.chosen.display) {
    gtk_popover_popdown(A.cityPop);
    return;
  }
  const theme::Palette &p = theme::Current();
  for (const auto &c : A.cityResults) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_add_css_class(row, "city-row");
    std::string nm = c.display, rest;
    size_t comma = nm.find(',');
    if (comma != std::string::npos) {
      rest = nm.substr(comma + 2);
      nm = nm.substr(0, comma);
    }
    gtk_box_append(GTK_BOX(row), Label(nm, "city-name"));
    char meta[256];
    snprintf(meta, sizeof(meta), "%s  ·  %s %s  ·  %s", rest.c_str(),
      astro::FormatDegree(c.lat, 'N', 'S').c_str(),
      astro::FormatDegree(c.lon, 'E', 'W').c_str(), c.zoneName.c_str());
    gtk_box_append(GTK_BOX(row), Label(meta, "city-meta"));
    gtk_list_box_append(A.cityList, row);
  }
  (void)p;
  gtk_list_box_select_row(A.cityList, gtk_list_box_get_row_at_index(
    A.cityList, 0));
  gtk_popover_popup(A.cityPop);
}

void OnCityChanged(GtkEditable *, gpointer) {
  if (A.loading) return;
  if (A.searchSource) g_source_remove(A.searchSource);
  A.searchSource = g_timeout_add(120, [](gpointer) -> gboolean {
    A.searchSource = 0;
    RunCitySearch();
    return G_SOURCE_REMOVE;
  }, nullptr);
}

gboolean OnCityKey(GtkEventControllerKey *, guint key, guint, GdkModifierType,
  gpointer) {
  if (!gtk_widget_get_visible(GTK_WIDGET(A.cityPop))) return FALSE;
  GtkListBoxRow *sel = gtk_list_box_get_selected_row(A.cityList);
  int i = sel ? gtk_list_box_row_get_index(sel) : -1;
  int n = (int)A.cityResults.size();
  switch (key) {
  case GDK_KEY_Down:
    i = std::min(i + 1, n - 1);
    break;
  case GDK_KEY_Up:
    i = std::max(i - 1, 0);
    break;
  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
  case GDK_KEY_Tab:
    ChooseCity(i < 0 ? 0 : i);
    return key != GDK_KEY_Tab;
  case GDK_KEY_Escape:
    gtk_popover_popdown(A.cityPop);
    return TRUE;
  default:
    return FALSE;
  }
  gtk_list_box_select_row(A.cityList,
    gtk_list_box_get_row_at_index(A.cityList, i));
  return TRUE;
}

// ---------------------------------------------------------------------------
// Saved charts.

void RefreshSaved();

void SaveCurrent() {
  BirthData b = A.current;
  bool replaced = false;
  for (auto &s : A.saved)
    if (s.name == b.name) {
      s = b;
      replaced = true;
    }
  if (!replaced) A.saved.insert(A.saved.begin(), b);
  store::SaveCharts(A.saved);
  RefreshSaved();
  Toast(replaced ? "Updated “" + b.name + "”" : "Saved “" + b.name + "”");
}

void RefreshSaved() {
  ClearChildren(GTK_WIDGET(A.savedList));
  if (A.saved.empty()) {
    GtkWidget *e = Label("No saved charts yet. Press Ctrl+S to keep this one.",
      "empty-state");
    gtk_label_set_wrap(GTK_LABEL(e), TRUE);
    gtk_list_box_append(A.savedList, e);
    gtk_list_box_row_set_activatable(gtk_list_box_get_row_at_index(
      A.savedList, 0), FALSE);
    return;
  }
  for (size_t i = 0; i < A.saved.size(); i++) {
    const BirthData &b = A.saved[i];
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(row, "saved-row");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *nm = Label(b.name, "saved-name");
    gtk_label_set_ellipsize(GTK_LABEL(nm), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(text), nm);
    char meta[256];
    std::string loc = b.location.substr(0, b.location.find(','));
    snprintf(meta, sizeof(meta), "%04d-%02d-%02d %02d:%02d · %s", b.year,
      b.month, b.day, b.hour, b.minute, loc.c_str());
    GtkWidget *ml = Label(meta, "saved-meta");
    gtk_label_set_ellipsize(GTK_LABEL(ml), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(text), ml);
    gtk_box_append(GTK_BOX(row), text);
    GtkWidget *del = gtk_button_new_from_icon_name("astrolog-trash-symbolic");
    gtk_widget_add_css_class(del, "flat");
    gtk_widget_add_css_class(del, "danger");
    gtk_widget_set_valign(del, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(del, "Delete");
    g_object_set_data(G_OBJECT(del), "index", GINT_TO_POINTER(i));
    g_signal_connect(del, "clicked", CB(+[](GtkButton *btn, gpointer) {
      size_t idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "index"));
      if (idx >= A.saved.size()) return;
      std::string nm = A.saved[idx].name;
      A.saved.erase(A.saved.begin() + idx);
      store::SaveCharts(A.saved);
      RefreshSaved();
      Toast("Deleted “" + nm + "”");
    }), nullptr);
    gtk_box_append(GTK_BOX(row), del);
    gtk_list_box_append(A.savedList, row);
  }
}

// ---------------------------------------------------------------------------
// View updates.

void UpdateHero() {
  const Chart &c = A.chart;
  const BirthData &b = c.data;
  gtk_label_set_text(A.heroName, b.name.c_str());
  char time[64];
  snprintf(time, sizeof(time), "%02d:%02d:%02d", b.hour, b.minute, b.second);
  std::string meta = LongDate(b) + "  ·  " + time + " " + ZoneSummary(b) +
    "\n" + (b.location.empty() ? std::string("Custom location") : b.location) +
    "  ·  " + astro::FormatDegree(b.lat, 'N', 'S') + " " +
    astro::FormatDegree(b.lon, 'E', 'W');
  gtk_label_set_text(A.heroMeta, meta.c_str());

  ClearChildren(GTK_WIDGET(A.chips));
  auto chip = [&](GtkWidget *g, const std::string &label,
    const std::string &value) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(box, "chip");
    gtk_box_append(GTK_BOX(box), g);
    GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(t), Label(label, "chip-label"));
    gtk_box_append(GTK_BOX(t), Label(value, "chip-value"));
    gtk_box_append(GTK_BOX(box), t);
    gtk_flow_box_append(A.chips, box);
  };
  for (const auto &bd : c.bodies) {
    const char *role = bd.id == 1 ? "SUN" : bd.id == 2 ? "MOON" :
      bd.kind == astro::kAngle && bd.name == "Ascendant" ? "RISING" : nullptr;
    if (!role) continue;
    int s = (int)(std::fmod(bd.lon + 360.0, 360.0) / 30.0);
    chip(BodyW(bd, 23), role, astro::SignName(s));
  }
  const theme::Palette &p = theme::Current();
  chip(GlyphW("⌂", p.accent, 20), "HOUSES", c.houseSystem);
  chip(GlyphW("♈", p.accent, 20), "ZODIAC", c.zodiac);
}

void HighlightBodyRows() {
  for (size_t i = 0; i < A.bodyRows.size(); i++) {
    if ((int)i == A.ws.hoverBody)
      gtk_widget_add_css_class(A.bodyRows[i], "focused");
    else
      gtk_widget_remove_css_class(A.bodyRows[i], "focused");
  }
}

GtkWidget *AspectW(const Aspect &a, const Chart &ca, const Chart &cb,
  bool transit) {
  const Body &A1 = ca.bodies[a.a], &B1 = cb.bodies[a.b];
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_append(GTK_BOX(row), BodyW(A1, 18, transit));
  gtk_box_append(GTK_BOX(row), GlyphW(astro::AspectGlyph(a.type),
    theme::AspectColor(a.type), 16));
  gtk_box_append(GTK_BOX(row), BodyW(B1, 18));
  char orb[32];
  snprintf(orb, sizeof(orb), "%d°%02d'", (int)a.orb,
    (int)((a.orb - (int)a.orb) * 60));
  // Names may be cut short when the panel is narrow; the orb never is.
  GtkWidget *l = Markup(" " + Esc(A1.name) + " <span alpha=\"60%\">" +
    Esc(astro::AspectName(a.type)) + "</span> " + Esc(B1.name));
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(l, TRUE);
  gtk_box_append(GTK_BOX(row), l);
  GtkWidget *o = Label(std::string(orb) + (a.applying ? " ↘" : " ↗"),
    "muted", 1.0f);
  gtk_widget_set_margin_start(o, 6);
  gtk_widget_set_tooltip_text(o, a.applying ? "Orb · applying (tightening)" :
    "Orb · separating (loosening)");
  gtk_box_append(GTK_BOX(row), o);
  return row;
}

// A small pill showing how exact an aspect is, in the aspect's color.
GtkWidget *StrengthW(double frac, const theme::Rgb &color) {
  struct Data { double f; theme::Rgb c; };
  GtkWidget *da = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da), 56);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da), 6);
  gtk_widget_set_valign(da, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), [](GtkDrawingArea *,
    cairo_t *cr, int w, int h, gpointer p) {
    auto *d = (Data *)p;
    auto pill = [&](double width) {
      double r = h / 2.0;
      cairo_new_sub_path(cr);
      cairo_arc(cr, r, r, r, M_PI / 2, 3 * M_PI / 2);
      cairo_arc(cr, width - r, r, r, -M_PI / 2, M_PI / 2);
      cairo_close_path(cr);
    };
    pill(w);
    theme::SetSource(cr, theme::Current().fg, 0.08);
    cairo_fill(cr);
    pill(std::max((double)h, w * d->f));
    theme::SetSource(cr, d->c, 0.9);
    cairo_fill(cr);
  }, new Data{frac, color}, [](gpointer p) { delete (Data *)p; });
  return da;
}

GtkWidget *FocusHeader(const Body &b, bool transit) {
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(h), BodyW(b, 24, transit));
  gtk_box_append(GTK_BOX(h), Markup("<b>" + Esc(b.name) + "</b>" +
    (transit ? " <span alpha=\"60%\">transiting</span>" : ""), "strong"));
  gtk_box_append(GTK_BOX(h), PositionW(b.lon));
  return h;
}

GtkWidget *Wrapped(const std::string &text, const char *cls) {
  GtkWidget *l = Label(text, cls);
  gtk_label_set_wrap(GTK_LABEL(l), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(l), 30);
  return l;
}

void UpdateFocus() {
  ClearChildren(GTK_WIDGET(A.focusBox));
  const Chart &c = A.chart;
  const theme::Palette &p = theme::Current();
  if (A.ws.hoverTransit >= 0 && A.transit.ok) {
    const Body &b = A.transit.bodies[A.ws.hoverTransit];
    gtk_box_append(A.focusBox, FocusHeader(b, true));
    if (b.retro)
      gtk_box_append(A.focusBox, Markup("<span foreground=\"" +
        theme::Hex(p.red) + "\">℞ retrograde</span>"));
    int n = 0;
    for (const auto &a : A.trAsp)
      if (a.a == A.ws.hoverTransit) {
        gtk_box_append(A.focusBox, AspectW(a, A.transit, c, true));
        n++;
      }
    if (!n) gtk_box_append(A.focusBox, Wrapped("No close transits to natal.",
      "muted"));
    return;
  }
  if (A.ws.hoverBody < 0 || A.ws.hoverBody >= (int)c.bodies.size()) {
    gtk_box_append(A.focusBox, Wrapped("Hover a planet on the wheel or in the "
      "list to trace its aspects.", "muted"));
    char buf[128];
    int nMaj = 0;
    for (const auto &a : c.aspects) if (astro::AspectNature(a.type) != 3) nMaj++;
    snprintf(buf, sizeof(buf), "%zu aspects · %d major · %zu transits",
      c.aspects.size(), nMaj, A.trAsp.size());
    gtk_box_append(A.focusBox, Label(buf, "hint"));
    return;
  }
  const Body &b = c.bodies[A.ws.hoverBody];
  gtk_box_append(A.focusBox, FocusHeader(b, false));
  int s = (int)(std::fmod(b.lon + 360.0, 360.0) / 30.0);
  char buf[160];
  snprintf(buf, sizeof(buf), "%s in %s · house %d%s", b.name.c_str(),
    astro::SignName(s).c_str(), b.house, b.retro ? " · retrograde" : "");
  gtk_box_append(A.focusBox, Wrapped(buf, "muted"));
  int n = 0;
  for (const auto &a : c.aspects) {
    if (a.a != A.ws.hoverBody && a.b != A.ws.hoverBody) continue;
    Aspect x = a;
    if (x.b == A.ws.hoverBody) std::swap(x.a, x.b);
    gtk_box_append(A.focusBox, AspectW(x, c, c, false));
    n++;
  }
  for (const auto &a : A.trAsp) {
    if (a.b != A.ws.hoverBody) continue;
    gtk_box_append(A.focusBox, AspectW(a, A.transit, c, true));
    n++;
  }
  if (!n) gtk_box_append(A.focusBox, Label("Unaspected.", "muted"));
}

void SetHover(int body, int transit) {
  if (A.ws.hoverBody == body && A.ws.hoverTransit == transit) return;
  A.ws.hoverBody = body;
  A.ws.hoverTransit = transit;
  gtk_widget_queue_draw(GTK_WIDGET(A.wheel));
  HighlightBodyRows();
  UpdateFocus();
}

void UpdateBodyList() {
  ClearChildren(GTK_WIDGET(A.bodyList));
  A.bodyRows.clear();
  const Chart &c = A.chart;
  const theme::Palette &p = theme::Current();
  for (size_t i = 0; i < c.bodies.size(); i++) {
    const Body &b = c.bodies[i];
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "body-row");
    GtkWidget *g = BodyW(b, 19);
    gtk_widget_set_size_request(g, 26, -1);
    gtk_box_append(GTK_BOX(row), g);
    GtkWidget *nm = Label(b.name);
    gtk_widget_set_hexpand(nm, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(nm), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(row), nm);
    gtk_box_append(GTK_BOX(row), PositionW(b.lon));
    GtkWidget *rx = GlyphW("℞", p.red, 14);
    gtk_widget_set_opacity(rx, b.retro ? 1.0 : 0.0);
    gtk_box_append(GTK_BOX(row), rx);
    GtkWidget *h = Label(std::to_string(b.house), "muted", 1.0f);
    gtk_widget_set_size_request(h, 22, -1);
    gtk_box_append(GTK_BOX(row), h);

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_object_set_data(G_OBJECT(motion), "index", GINT_TO_POINTER(i));
    g_signal_connect(motion, "enter", CB(+[](
      GtkEventControllerMotion *m, double, double, gpointer) {
      SetHover(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(m), "index")), -1);
    }), nullptr);
    gtk_widget_add_controller(row, motion);
    gtk_box_append(A.bodyList, row);
    A.bodyRows.push_back(row);
  }
}

GtkWidget *CellW(GtkWidget *child, float xalign, bool alt) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(box, "table-cell");
  if (alt) gtk_widget_add_css_class(box, "table-row-alt");
  gtk_widget_set_halign(child, xalign > 0.9f ? GTK_ALIGN_END :
    (xalign > 0.1f ? GTK_ALIGN_CENTER : GTK_ALIGN_START));
  gtk_widget_set_hexpand(child, TRUE);
  gtk_box_append(GTK_BOX(box), child);
  return box;
}

GtkWidget *Cell(const std::string &markup, float xalign, bool alt,
  const char *extra = nullptr) {
  GtkWidget *l = Markup(markup, "table-cell", xalign);
  gtk_widget_set_hexpand(l, xalign < 0.01f);
  if (alt) gtk_widget_add_css_class(l, "table-row-alt");
  if (extra) gtk_widget_add_css_class(l, extra);
  return l;
}

void UpdatePositions() {
  ClearChildren(GTK_WIDGET(A.posGrid));
  const Chart &c = A.chart;
  const theme::Palette &p = theme::Current();
  const char *heads[] = {"", "BODY", "POSITION", "HOUSE", "LATITUDE", "SPEED",
    "MOTION"};
  for (int i = 0; i < 7; i++) {
    GtkWidget *h = Label(heads[i], "table-head", i == 1 ? 0.0f : 1.0f);
    gtk_widget_add_css_class(h, "table-cell");
    gtk_grid_attach(A.posGrid, h, i, 0, 1, 1);
  }
  int r = 1;
  for (const auto &b : c.bodies) {
    bool alt = r % 2 == 0;
    char buf[64];
    gtk_grid_attach(A.posGrid, CellW(BodyW(b, 19), 0.5f, alt), 0, r, 1, 1);
    gtk_grid_attach(A.posGrid, Cell("<b>" + Esc(b.name) + "</b>", 0.0f, alt), 1,
      r, 1, 1);
    gtk_grid_attach(A.posGrid, CellW(PositionW(b.lon), 1.0f, alt), 2, r, 1, 1);
    gtk_grid_attach(A.posGrid, Cell(std::to_string(b.house), 1.0f, alt), 3, r,
      1, 1);
    gtk_grid_attach(A.posGrid, Cell(b.kind == astro::kAngle ? "—" :
      astro::FormatDegree(b.lat, 'N', 'S'), 1.0f, alt), 4, r, 1, 1);
    if (b.kind == astro::kAngle) snprintf(buf, sizeof(buf), "—");
    else snprintf(buf, sizeof(buf), "%+.3f°/d", b.speed);
    gtk_grid_attach(A.posGrid, Cell(buf, 1.0f, alt), 5, r, 1, 1);
    std::string motion;
    if (b.kind == astro::kAngle) motion = "—";
    else if (b.retro)
      motion = "<span foreground=\"" + theme::Hex(p.red) +
        "\"><b>Retrograde</b></span>";
    else if (std::fabs(b.speed) < 0.003 && b.kind != astro::kNode)
      motion = "<span foreground=\"" + theme::Hex(p.yellow) +
        "\">Stationary</span>";
    else motion = "<span alpha=\"60%\">Direct</span>";
    gtk_grid_attach(A.posGrid, Cell(motion, 1.0f, alt), 6, r, 1, 1);
    r++;
  }

  ClearChildren(GTK_WIDGET(A.cuspGrid));
  const char *ch[] = {"HOUSE", "CUSP", "SIZE"};
  for (int i = 0; i < 3; i++) {
    GtkWidget *h = Label(ch[i], "table-head", i == 0 ? 0.0f : 1.0f);
    gtk_widget_add_css_class(h, "table-cell");
    gtk_grid_attach(A.cuspGrid, h, i, 0, 1, 1);
  }
  const char *names[] = {"", "1 · Self", "2 · Resources", "3 · Mind",
    "4 · Home", "5 · Joy", "6 · Work", "7 · Partners", "8 · Depths",
    "9 · Beliefs", "10 · Career", "11 · Friends", "12 · Hidden"};
  for (int h = 1; h <= 12; h++) {
    bool alt = h % 2 == 0;
    double size = std::fmod(c.cusps[h % 12 + 1] - c.cusps[h] + 720.0, 360.0);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f°", size);
    gtk_grid_attach(A.cuspGrid, Cell(names[h], 0.0f, alt,
      h == 1 || h == 4 || h == 7 || h == 10 ? "strong" : nullptr), 0, h, 1, 1);
    gtk_grid_attach(A.cuspGrid, CellW(PositionW(c.cusps[h]), 1.0f, alt), 1, h,
      1, 1);
    gtk_grid_attach(A.cuspGrid, Cell(buf, 1.0f, alt, "muted"), 2, h, 1, 1);
  }
}

void UpdateAspectList() {
  ClearChildren(GTK_WIDGET(A.aspectList));
  const Chart &c = A.chart;
  auto group = [&](const char *title, const std::vector<Aspect> &v,
    const Chart &ca, const Chart &cb, bool transit) {
    gtk_box_append(A.aspectList, Label(title, "card-title"));
    if (v.empty()) {
      GtkWidget *e = Label("None within orb.", "empty-state");
      gtk_widget_set_margin_start(e, 14);
      gtk_box_append(A.aspectList, e);
    }
    for (const auto &a : v) {
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      gtk_widget_add_css_class(row, "body-row");
      gtk_widget_set_margin_start(row, 6);
      gtk_widget_set_margin_end(row, 6);
      GtkWidget *l = AspectW(a, ca, cb, transit);
      gtk_widget_set_hexpand(l, TRUE);
      gtk_box_append(GTK_BOX(row), l);
      GtkWidget *bar = StrengthW(a.maxOrb > 0 ?
        std::clamp(1.0 - a.orb / a.maxOrb, 0.05, 1.0) : 0.5,
        theme::AspectColor(a.type));
      gtk_widget_set_tooltip_text(bar, "Strength (tightness of orb)");
      gtk_box_append(GTK_BOX(row), bar);
      gtk_box_append(A.aspectList, row);
    }
  };
  if (A.transit.ok) group("TRANSITS TO NATAL · NOW", A.trAsp, A.transit, c,
    true);
  group("NATAL ASPECTS · TIGHTEST FIRST", c.aspects, c, c, false);
}

void RunReport();

void SavePrefsNow() {
  if (A.prefsSource) {
    g_source_remove(A.prefsSource);
    A.prefsSource = 0;
  }
  if (!A.house || g_getenv("ASTROLOG_STUDIO_SNAPSHOT")) return;
  A.prefs.settings = ReadSettings();
  A.prefs.transits = gtk_switch_get_active(A.transits);
  A.prefs.textSize = (int)gtk_drop_down_get_selected(A.textSize);
  if (A.narrow == 0) {
    if (A.wheelPaneSet && gtk_widget_get_width(A.wheelSide) > 0)
      A.prefs.wheelPanel = gtk_widget_get_width(A.wheelSide) + 8;
    if (A.aspPaneSet && gtk_widget_get_width(A.aspCard) > 0)
      A.prefs.aspectPanel = gtk_widget_get_width(A.aspCard) + 8;
  }
  store::SavePrefs(A.prefs);
}

// Preferences are saved shortly after any change, so nothing is lost if the
// app is killed rather than closed.
void SchedulePrefsSave() {
  if (A.prefsSource) g_source_remove(A.prefsSource);
  A.prefsSource = g_timeout_add(800, [](gpointer) -> gboolean {
    A.prefsSource = 0;
    SavePrefsNow();
    return G_SOURCE_REMOVE;
  }, nullptr);
}

void UpdateViews() {
  if (!A.chart.ok) return;
  UpdateHero();
  UpdateBodyList();
  UpdateFocus();
  UpdatePositions();
  UpdateAspectList();
  gtk_widget_queue_draw(GTK_WIDGET(A.wheel));
  // The grid never shrinks below readable cells; it scrolls instead.
  int n = (int)A.chart.bodies.size();
  int cellMin = (int)std::ceil(31 * theme::TextScale());
  gtk_drawing_area_set_content_width(A.grid, n * cellMin + 16);
  gtk_drawing_area_set_content_height(A.grid, n * cellMin + 16);
  gtk_drawing_area_set_content_height(A.balance,
    (int)(620 * theme::TextScale()));
  gtk_widget_queue_draw(GTK_WIDGET(A.grid));
  gtk_widget_queue_draw(GTK_WIDGET(A.balance));
  const char *page = gtk_stack_get_visible_child_name(A.stack);
  if (page && strcmp(page, "reports") == 0) RunReport();
  else A.reportSerial++;  // Mark report stale; rerun when shown.
  A.prefs.last = A.current;
  A.prefs.haveLast = true;
  SchedulePrefsSave();
}

// Text size setting: restyle and rebuild the glyph widgets at the new size.
void ApplyTextSize() {
  static const double kScales[] = {0.9, 1.0, 1.15, 1.3};
  int i = std::clamp((int)gtk_drop_down_get_selected(A.textSize), 0, 3);
  theme::SetTextScale(kScales[i]);
  if (A.chart.ok) UpdateViews();
}

// ---------------------------------------------------------------------------
// Reports.

struct ReportJob {
  BirthData bd;
  astro::Settings s;
  std::vector<std::string> sw;
  std::string text, err;
  guint serial;
  bool prose;
};

void RunReport() {
  if (!A.chart.ok) return;
  const auto &defs = Reports();
  int i = std::clamp(A.reportIndex, 0, (int)defs.size() - 1);
  gtk_label_set_text(A.reportTitle, defs[i].title);
  gtk_spinner_start(A.reportSpin);
  gtk_widget_set_visible(GTK_WIDGET(A.reportSpin), TRUE);
  auto *job = new ReportJob{A.current, A.chart.settings, defs[i].sw, "", "",
    ++A.reportSerial, defs[i].prose};
  GTask *task = g_task_new(nullptr, nullptr, +[](GObject *, GAsyncResult *res,
    gpointer) {
    auto *j = (ReportJob *)g_task_get_task_data(G_TASK(res));
    if (j->serial == A.reportSerial) {
      gtk_spinner_stop(A.reportSpin);
      gtk_widget_set_visible(GTK_WIDGET(A.reportSpin), FALSE);
      std::string t = j->text.empty() ? "No output.\n" + j->err : j->text;
      if (j->prose) {
        // Astrolog hard-wraps paragraphs, indenting continuation lines by
        // two spaces. Rejoin them so GTK can reflow the text to the window,
        // and separate paragraphs with a blank line.
        std::string out;
        size_t pos = 0;
        while (pos < t.size()) {
          size_t nl = t.find('\n', pos);
          std::string line = t.substr(pos, nl == std::string::npos ?
            std::string::npos : nl - pos);
          pos = nl == std::string::npos ? t.size() : nl + 1;
          if (line.rfind("  ", 0) == 0 && !out.empty() && out.back() == '\n') {
            out.pop_back();
            out.pop_back();
            out += " " + line.substr(line.find_first_not_of(' '));
          } else {
            out += line;
          }
          out += "\n\n";
        }
        t = out;
      }
      gtk_text_view_set_wrap_mode(A.reportView,
        j->prose ? GTK_WRAP_WORD : GTK_WRAP_NONE);
      GtkTextBuffer *buf = gtk_text_view_get_buffer(A.reportView);
      gtk_text_buffer_set_text(buf, t.c_str(), -1);
      GtkTextIter start;
      gtk_text_buffer_get_start_iter(buf, &start);
      gtk_text_buffer_place_cursor(buf, &start);
      gtk_text_view_scroll_to_iter(A.reportView, &start, 0, FALSE, 0, 0);
    }
  }, nullptr);
  g_task_set_task_data(task, job, [](gpointer p) { delete (ReportJob *)p; });
  g_task_run_in_thread(task, +[](GTask *t, gpointer, gpointer data,
    GCancellable *) {
    auto *j = (ReportJob *)data;
    j->text = astro::Report(j->bd, j->s, j->sw, &j->err);
    g_task_return_boolean(t, TRUE);
  });
  g_object_unref(task);
}

std::string CommandLine() {
  std::string cmd = "astrolog";
  for (auto &a : astro::ChartArgs(A.current, A.chart.settings)) {
    gchar *q = g_shell_quote(a.c_str());
    bool needs = a.find_first_of(" '\"$`\\") != std::string::npos;
    cmd += " ";
    cmd += needs ? q : a;
    g_free(q);
  }
  return cmd;
}

// ---------------------------------------------------------------------------
// Drawing area callbacks.

void DrawWheelCb(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer) {
  if (!A.chart.ok) {
    render::DrawEmptyState(cr, w, h, "Enter birth data to cast a chart");
    return;
  }
  render::DrawWheel(cr, w, h, A.chart, A.transit.ok ? &A.transit : nullptr,
    &A.trAsp, A.ws);
}

void WheelMotion(GtkEventControllerMotion *, double x, double y, gpointer) {
  int body = -1, tr = -1;
  double best = 1e9;
  for (const auto &hit : A.ws.hits) {
    double d = std::hypot(hit.x - x, hit.y - y);
    if (d <= hit.r && d < best) {
      best = d;
      body = hit.transit ? -1 : hit.body;
      tr = hit.transit ? hit.body : -1;
    }
  }
  if (body >= 0 || tr >= 0) SetHover(body, tr);
  else if (A.ws.hoverBody >= 0 || A.ws.hoverTransit >= 0) SetHover(-1, -1);
}

void DrawGridCb(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer) {
  if (A.chart.ok) render::DrawAspectGrid(cr, w, h, A.chart, A.gs);
}

void GridMotion(GtkEventControllerMotion *, double x, double y, gpointer) {
  int col = A.gs.cell > 0 ? (int)std::floor((x - A.gs.originX) / A.gs.cell)
    : -1;
  int row = A.gs.cell > 0 ? (int)std::floor((y - A.gs.originY) / A.gs.cell)
    : -1;
  if (row < 0 || col < 0 || row >= A.gs.count || col > row) row = col = -1;
  if (row != A.gs.hoverRow || col != A.gs.hoverCol) {
    A.gs.hoverRow = row;
    A.gs.hoverCol = col;
    gtk_widget_queue_draw(GTK_WIDGET(A.grid));
    std::string tip;
    if (row >= 0 && A.chart.ok) {
      const Body &a = A.chart.bodies[row], &b = A.chart.bodies[col];
      tip = a.name;
      if (row != col) {
        tip += " – " + b.name;
        for (const auto &asp : A.chart.aspects)
          if ((asp.a == row && asp.b == col) || (asp.a == col && asp.b == row)) {
            char buf[96];
            snprintf(buf, sizeof(buf), "\n%s, orb %.2f° %s",
              astro::AspectName(asp.type).c_str(), asp.orb,
              asp.applying ? "applying" : "separating");
            tip += buf;
          }
      } else {
        tip += "\n" + astro::FormatZodiac(a.lon);
      }
    }
    gtk_widget_set_tooltip_text(GTK_WIDGET(A.grid),
      tip.empty() ? nullptr : tip.c_str());
  }
}

void DrawBalanceCb(GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer) {
  if (A.chart.ok) render::DrawBalance(cr, w, h, A.chart);
}

// ---------------------------------------------------------------------------
// Building the window.

GtkSpinButton *Spin(double lo, double hi, double step, int digits = 0,
  bool wrap = false) {
  GtkWidget *s = gtk_spin_button_new_with_range(lo, hi, step);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), digits);
  gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(s), wrap);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(s), TRUE);
  gtk_editable_set_width_chars(GTK_EDITABLE(s), 2);
  gtk_widget_set_hexpand(s, TRUE);
  g_signal_connect(s, "value-changed", G_CALLBACK(OnFormChanged), nullptr);
  return GTK_SPIN_BUTTON(s);
}

// Two digit display for time spinners ("07").
gboolean PadOutput(GtkSpinButton *s, gpointer) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d", gtk_spin_button_get_value_as_int(s));
  if (strcmp(buf, gtk_editable_get_text(GTK_EDITABLE(s))))
    gtk_editable_set_text(GTK_EDITABLE(s), buf);
  return TRUE;
}

// UTC offset display for the manual zone spinner ("UTC-05:00").
gboolean OffsetOutput(GtkSpinButton *s, gpointer) {
  std::string t = astro::FormatOffset(gtk_spin_button_get_value(s));
  if (t != gtk_editable_get_text(GTK_EDITABLE(s)))
    gtk_editable_set_text(GTK_EDITABLE(s), t.c_str());
  return TRUE;
}

gint OffsetInput(GtkSpinButton *s, double *out, gpointer) {
  std::string t = gtk_editable_get_text(GTK_EDITABLE(s));
  double v;
  size_t pos = t.find_first_of("+-0123456789");
  if (pos == std::string::npos) return GTK_INPUT_ERROR;
  if (!ParseCoord(t.substr(pos), 14.0, '+', '-', &v)) return GTK_INPUT_ERROR;
  if (t.find('-', pos) != std::string::npos && v > 0) v = -v;
  *out = v;
  return TRUE;
}

GtkWidget *BuildSidebar() {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(box, "sidebar-inner");

  gtk_box_append(GTK_BOX(box), Section("BIRTH DATA", true));
  A.name = GTK_ENTRY(gtk_entry_new());
  gtk_entry_set_placeholder_text(A.name, "Name");
  g_signal_connect(A.name, "changed", G_CALLBACK(OnFormChanged), nullptr);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(A.name));

  // Date: day / month / year.
  GtkWidget *date = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  A.day = Spin(1, 31, 1);
  A.month = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(kMonths));
  gtk_widget_set_hexpand(GTK_WIDGET(A.month), TRUE);
  g_signal_connect(A.month, "notify::selected", G_CALLBACK(OnFormNotify),
    nullptr);
  A.year = Spin(-3000, 3000, 1);
  gtk_editable_set_width_chars(GTK_EDITABLE(A.year), 4);
  gtk_box_append(GTK_BOX(date), GTK_WIDGET(A.day));
  gtk_box_append(GTK_BOX(date), GTK_WIDGET(A.month));
  gtk_box_append(GTK_BOX(date), GTK_WIDGET(A.year));
  gtk_box_append(GTK_BOX(box), Field("Date", date));

  // Time: hh : mm : ss plus a "Now" shortcut.
  GtkWidget *time = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  A.hour = Spin(0, 23, 1, 0, true);
  A.minute = Spin(0, 59, 1, 0, true);
  A.second = Spin(0, 59, 1, 0, true);
  for (GtkSpinButton *s : {A.hour, A.minute, A.second})
    g_signal_connect(s, "output", G_CALLBACK(PadOutput), nullptr);
  gtk_box_append(GTK_BOX(time), GTK_WIDGET(A.hour));
  gtk_box_append(GTK_BOX(time), Label(":", "muted"));
  gtk_box_append(GTK_BOX(time), GTK_WIDGET(A.minute));
  gtk_box_append(GTK_BOX(time), Label(":", "muted"));
  gtk_box_append(GTK_BOX(time), GTK_WIDGET(A.second));
  GtkWidget *now = gtk_button_new_from_icon_name("astrolog-now-symbolic");
  gtk_widget_set_tooltip_text(now, "Use the current date and time");
  g_signal_connect(now, "clicked", CB(+[](GtkButton *, gpointer) {
    SetTimeNow();
  }), nullptr);
  gtk_box_append(GTK_BOX(time), now);
  gtk_box_append(GTK_BOX(box), Field("Local time (24h)", time));

  // Location search with atlas popover.
  A.city = GTK_ENTRY(gtk_entry_new());
  gtk_entry_set_placeholder_text(A.city, "Search 33,000 cities…");
  gtk_entry_set_icon_from_icon_name(A.city, GTK_ENTRY_ICON_PRIMARY,
    "astrolog-search-symbolic");
  g_signal_connect(A.city, "changed", G_CALLBACK(OnCityChanged), nullptr);
  GtkEventController *key = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(key, GTK_PHASE_CAPTURE);
  g_signal_connect(key, "key-pressed", G_CALLBACK(OnCityKey), nullptr);
  gtk_widget_add_controller(GTK_WIDGET(A.city), key);
  A.cityPop = GTK_POPOVER(gtk_popover_new());
  gtk_popover_set_autohide(A.cityPop, FALSE);
  gtk_popover_set_has_arrow(A.cityPop, FALSE);
  gtk_popover_set_position(A.cityPop, GTK_POS_BOTTOM);
  gtk_widget_set_parent(GTK_WIDGET(A.cityPop), GTK_WIDGET(A.city));
  A.cityList = GTK_LIST_BOX(gtk_list_box_new());
  gtk_list_box_set_selection_mode(A.cityList, GTK_SELECTION_BROWSE);
  gtk_widget_set_size_request(GTK_WIDGET(A.cityList), 300, -1);
  g_signal_connect(A.cityList, "row-activated", CB(+[](GtkListBox *,
    GtkListBoxRow *row, gpointer) {
    ChooseCity(gtk_list_box_row_get_index(row));
  }), nullptr);
  gtk_popover_set_child(A.cityPop, GTK_WIDGET(A.cityList));
  // Track the pointer so a click on a suggestion isn't lost when the entry
  // loses focus and the popover would otherwise close first.
  GtkEventController *popMotion = gtk_event_controller_motion_new();
  g_signal_connect(popMotion, "enter", CB(+[](GtkEventControllerMotion *,
    double, double, gpointer) { A.pointerInCityPop = true; }), nullptr);
  g_signal_connect(popMotion, "leave", CB(+[](GtkEventControllerMotion *,
    gpointer) { A.pointerInCityPop = false; }), nullptr);
  gtk_widget_add_controller(GTK_WIDGET(A.cityPop), popMotion);
  // Hide suggestions when focus leaves the entry.
  GtkEventController *focus = gtk_event_controller_focus_new();
  g_signal_connect(focus, "leave", CB(+[](GtkEventControllerFocus *,
    gpointer) {
    g_timeout_add(150, [](gpointer) -> gboolean {
      if (!A.pointerInCityPop) gtk_popover_popdown(A.cityPop);
      return G_SOURCE_REMOVE;
    }, nullptr);
  }), nullptr);
  gtk_widget_add_controller(GTK_WIDGET(A.city), focus);
  gtk_box_append(GTK_BOX(box), Field("Birthplace", GTK_WIDGET(A.city)));

  GtkWidget *coords = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_set_homogeneous(GTK_BOX(coords), TRUE);
  A.lat = GTK_ENTRY(gtk_entry_new());
  A.lon = GTK_ENTRY(gtk_entry_new());
  gtk_entry_set_placeholder_text(A.lat, "40°43'N");
  gtk_entry_set_placeholder_text(A.lon, "74°00'W");
  gtk_editable_set_width_chars(GTK_EDITABLE(A.lat), 8);
  gtk_editable_set_width_chars(GTK_EDITABLE(A.lon), 8);
  g_signal_connect(A.lat, "changed", G_CALLBACK(OnFormChanged), nullptr);
  g_signal_connect(A.lon, "changed", G_CALLBACK(OnFormChanged), nullptr);
  gtk_box_append(GTK_BOX(coords), Field("Latitude", GTK_WIDGET(A.lat)));
  gtk_box_append(GTK_BOX(coords), Field("Longitude", GTK_WIDGET(A.lon)));
  gtk_box_append(GTK_BOX(box), coords);

  // Time zone.
  const char *modes[] = {"Automatic (from city)", "Manual offset",
    "Local Mean Time", nullptr};
  A.zoneMode = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(modes));
  g_signal_connect(A.zoneMode, "notify::selected", G_CALLBACK(OnFormNotify),
    nullptr);
  gtk_box_append(GTK_BOX(box), Field("Time zone", GTK_WIDGET(A.zoneMode)));
  GtkWidget *zc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_add_css_class(zc, "zone-card");
  A.zoneName = GTK_LABEL(Label("", "zone-name"));
  A.zoneDetail = GTK_LABEL(Label("", "hint"));
  gtk_label_set_wrap(A.zoneDetail, TRUE);
  gtk_box_append(GTK_BOX(zc), GTK_WIDGET(A.zoneName));
  gtk_box_append(GTK_BOX(zc), GTK_WIDGET(A.zoneDetail));
  gtk_box_append(GTK_BOX(box), zc);
  A.manualBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  A.offset = Spin(-12, 14, 0.25, 2);
  gtk_editable_set_width_chars(GTK_EDITABLE(A.offset), 9);
  gtk_spin_button_set_numeric(A.offset, FALSE);
  g_signal_connect(A.offset, "output", G_CALLBACK(OffsetOutput), nullptr);
  g_signal_connect(A.offset, "input", G_CALLBACK(OffsetInput), nullptr);
  gtk_box_append(GTK_BOX(A.manualBox), GTK_WIDGET(A.offset));
  GtkWidget *dstBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append(GTK_BOX(dstBox), Label("DST", "field-label"));
  A.dstSwitch = GTK_SWITCH(gtk_switch_new());
  gtk_widget_set_valign(GTK_WIDGET(A.dstSwitch), GTK_ALIGN_CENTER);
  g_signal_connect(A.dstSwitch, "notify::active", G_CALLBACK(OnFormNotify),
    nullptr);
  gtk_box_append(GTK_BOX(dstBox), GTK_WIDGET(A.dstSwitch));
  gtk_box_append(GTK_BOX(A.manualBox), dstBox);
  gtk_box_append(GTK_BOX(box), A.manualBox);

  A.status = GTK_LABEL(Label("", "hint"));
  gtk_label_set_wrap(A.status, TRUE);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(A.status));

  GtkWidget *save = gtk_button_new_with_label("Save chart");
  gtk_widget_add_css_class(save, "suggested-action");
  gtk_widget_set_margin_top(save, 4);
  g_signal_connect(save, "clicked", CB(+[](GtkButton *, gpointer) {
    SaveCurrent();
  }), nullptr);
  gtk_box_append(GTK_BOX(box), save);

  // Options.
  gtk_box_append(GTK_BOX(box), Section("CHART OPTIONS"));
  std::vector<const char *> hs;
  for (const auto &s : astro::HouseSystems()) hs.push_back(s.c_str());
  hs.push_back(nullptr);
  A.house = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(hs.data()));
  gtk_drop_down_set_enable_search(A.house, TRUE);
  g_signal_connect(A.house, "notify::selected", G_CALLBACK(OnFormNotify),
    nullptr);
  const char *zod[] = {"Tropical", "Sidereal (Fagan-Bradley)", nullptr};
  A.zodiac = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(zod));
  g_signal_connect(A.zodiac, "notify::selected", G_CALLBACK(OnFormNotify),
    nullptr);
  gtk_box_append(GTK_BOX(box), Field("House system", GTK_WIDGET(A.house)));
  gtk_box_append(GTK_BOX(box), Field("Zodiac", GTK_WIDGET(A.zodiac)));
  gtk_box_append(GTK_BOX(box), SwitchRow("Transits", "Overlay the sky right now",
    &A.transits));
  gtk_box_append(GTK_BOX(box), SwitchRow("Asteroids",
    "Chiron, Ceres, Pallas, Juno, Vesta", &A.asteroids));
  gtk_box_append(GTK_BOX(box), SwitchRow("Sensitive points",
    "Lilith, Fortune, Vertex", &A.points));
  gtk_box_append(GTK_BOX(box), SwitchRow("Minor aspects",
    "Quincunx, semisextile, quintiles…", &A.minor));
  gtk_box_append(GTK_BOX(box), SwitchRow("True node", "Instead of mean node",
    &A.trueNode));
  const char *sizes[] = {"Small", "Default", "Large", "Extra large", nullptr};
  A.textSize = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sizes));
  gtk_drop_down_set_selected(A.textSize, 1);
  g_signal_connect(A.textSize, "notify::selected", CB(+[](GObject *,
    GParamSpec *, gpointer) {
    ApplyTextSize();
    if (!A.loading) SchedulePrefsSave();
  }), nullptr);
  GtkWidget *ts = Field("Text size", GTK_WIDGET(A.textSize));
  gtk_widget_set_margin_top(ts, 8);
  gtk_box_append(GTK_BOX(box), ts);
  for (GtkSwitch *s : {A.transits, A.asteroids, A.points, A.minor, A.trueNode})
    g_signal_connect(s, "notify::active", G_CALLBACK(OnFormNotify), nullptr);

  // Library.
  gtk_box_append(GTK_BOX(box), Section("SAVED CHARTS"));
  A.savedList = GTK_LIST_BOX(gtk_list_box_new());
  gtk_list_box_set_selection_mode(A.savedList, GTK_SELECTION_NONE);
  g_signal_connect(A.savedList, "row-activated", CB(+[](GtkListBox *,
    GtkListBoxRow *row, gpointer) {
    int i = gtk_list_box_row_get_index(row);
    if (i >= 0 && i < (int)A.saved.size()) {
      LoadIntoForm(A.saved[i]);
      Toast("Opened “" + A.saved[i].name + "”");
    }
  }), nullptr);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(A.savedList));

  GtkWidget *scroll = Scrolled(box);
  gtk_widget_add_css_class(scroll, "sidebar");
  gtk_widget_set_size_request(scroll, 300, -1);
  // Children (spin buttons) expand within the sidebar; the sidebar itself
  // must not compete with the chart for extra window width.
  gtk_widget_set_hexpand(scroll, FALSE);
  return scroll;
}

// A split pane whose end (right) child keeps its width as the window resizes
// and can be dragged narrower or wider from the handle.
GtkWidget *SplitPane() {
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);
  gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
  gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
  return paned;
}

GtkWidget *BuildWheelPage() {
  GtkWidget *page = SplitPane();
  A.wheelPage = page;
  gtk_widget_set_margin_start(page, 16);
  gtk_widget_set_margin_end(page, 20);
  gtk_widget_set_margin_bottom(page, 16);

  A.wheel = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_widget_set_hexpand(GTK_WIDGET(A.wheel), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(A.wheel), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(A.wheel), 300, 300);
  gtk_drawing_area_set_draw_func(A.wheel, DrawWheelCb, nullptr, nullptr);
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(WheelMotion), nullptr);
  g_signal_connect(motion, "leave", CB(+[](GtkEventControllerMotion *,
    gpointer) { SetHover(-1, -1); }), nullptr);
  gtk_widget_add_controller(GTK_WIDGET(A.wheel), motion);
  gtk_widget_set_margin_end(GTK_WIDGET(A.wheel), 8);
  gtk_paned_set_start_child(GTK_PANED(page), GTK_WIDGET(A.wheel));

  GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start(side, 8);
  A.wheelSide = side;
  A.bodyList = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  gtk_widget_set_margin_start(GTK_WIDGET(A.bodyList), 6);
  gtk_widget_set_margin_end(GTK_WIDGET(A.bodyList), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(A.bodyList), 8);
  GtkEventController *leave = gtk_event_controller_motion_new();
  g_signal_connect(leave, "leave", CB(+[](GtkEventControllerMotion *,
    gpointer) { SetHover(-1, -1); }), nullptr);
  gtk_widget_add_controller(GTK_WIDGET(A.bodyList), leave);
  A.bodyScroll = Scrolled(GTK_WIDGET(A.bodyList));
  GtkWidget *bodies = Card("PLACEMENTS", A.bodyScroll);
  gtk_widget_set_vexpand(bodies, TRUE);
  gtk_box_append(GTK_BOX(side), bodies);

  A.focusBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
  gtk_widget_set_margin_start(GTK_WIDGET(A.focusBox), 14);
  gtk_widget_set_margin_end(GTK_WIDGET(A.focusBox), 14);
  gtk_widget_set_margin_bottom(GTK_WIDGET(A.focusBox), 14);
  GtkWidget *focusScroll = Scrolled(GTK_WIDGET(A.focusBox));
  gtk_scrolled_window_set_min_content_height(
    GTK_SCROLLED_WINDOW(focusScroll), 190);
  gtk_widget_set_vexpand(focusScroll, FALSE);
  gtk_box_append(GTK_BOX(side), Card("FOCUS", focusScroll));
  gtk_paned_set_end_child(GTK_PANED(page), side);
  return Scrolled(page);
}

GtkWidget *BuildPositionsPage() {
  GtkWidget *page = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(page), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(page), 2);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(page), 16);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(page), 16);
  gtk_widget_set_valign(page, GTK_ALIGN_START);
  gtk_widget_set_margin_start(page, 24);
  gtk_widget_set_margin_end(page, 24);
  gtk_widget_set_margin_bottom(page, 24);
  gtk_widget_set_margin_top(page, 6);
  A.posGrid = GTK_GRID(gtk_grid_new());
  gtk_widget_set_margin_start(GTK_WIDGET(A.posGrid), 8);
  gtk_widget_set_margin_end(GTK_WIDGET(A.posGrid), 8);
  gtk_widget_set_margin_bottom(GTK_WIDGET(A.posGrid), 10);
  GtkWidget *c1 = Card("PLANETS & POINTS", GTK_WIDGET(A.posGrid));
  gtk_widget_set_hexpand(c1, TRUE);
  gtk_widget_set_valign(c1, GTK_ALIGN_START);
  A.cuspGrid = GTK_GRID(gtk_grid_new());
  gtk_widget_set_margin_start(GTK_WIDGET(A.cuspGrid), 8);
  gtk_widget_set_margin_end(GTK_WIDGET(A.cuspGrid), 8);
  gtk_widget_set_margin_bottom(GTK_WIDGET(A.cuspGrid), 10);
  GtkWidget *c2 = Card("HOUSE CUSPS", GTK_WIDGET(A.cuspGrid));
  gtk_widget_set_size_request(c2, 330, -1);
  gtk_widget_set_valign(c2, GTK_ALIGN_START);
  gtk_flow_box_append(GTK_FLOW_BOX(page), c1);
  gtk_flow_box_append(GTK_FLOW_BOX(page), c2);
  return Scrolled(page, true);
}

GtkWidget *BuildAspectsPage() {
  GtkWidget *page = SplitPane();
  A.aspPage = page;
  gtk_widget_set_margin_start(page, 16);
  gtk_widget_set_margin_end(page, 20);
  gtk_widget_set_margin_bottom(page, 16);
  A.grid = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_widget_set_hexpand(GTK_WIDGET(A.grid), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(A.grid), TRUE);
  gtk_drawing_area_set_draw_func(A.grid, DrawGridCb, nullptr, nullptr);
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(GridMotion), nullptr);
  g_signal_connect(motion, "leave", CB(+[](GtkEventControllerMotion *,
    gpointer) { GridMotion(nullptr, -1, -1, nullptr); }), nullptr);
  gtk_widget_add_controller(GTK_WIDGET(A.grid), motion);
  A.gridScroll = Scrolled(GTK_WIDGET(A.grid), true);
  gtk_widget_set_hexpand(A.gridScroll, TRUE);
  gtk_widget_set_margin_end(A.gridScroll, 8);
  gtk_paned_set_start_child(GTK_PANED(page), A.gridScroll);
  A.aspectList = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  gtk_widget_set_margin_bottom(GTK_WIDGET(A.aspectList), 10);
  A.aspCard = Card(nullptr, Scrolled(GTK_WIDGET(A.aspectList)));
  gtk_widget_set_margin_start(A.aspCard, 8);
  gtk_paned_set_end_child(GTK_PANED(page), A.aspCard);
  return Scrolled(page);
}

GtkWidget *BuildBalancePage() {
  A.balance = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_drawing_area_set_content_height(A.balance, 620);
  gtk_widget_set_hexpand(GTK_WIDGET(A.balance), TRUE);
  gtk_drawing_area_set_draw_func(A.balance, DrawBalanceCb, nullptr, nullptr);
  return Scrolled(GTK_WIDGET(A.balance));
}

GtkWidget *BuildReportsPage() {
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_set_margin_start(page, 16);
  gtk_widget_set_margin_end(page, 20);
  gtk_widget_set_margin_bottom(page, 16);

  A.reportList = GTK_LIST_BOX(gtk_list_box_new());
  gtk_widget_add_css_class(GTK_WIDGET(A.reportList), "report-list");
  gtk_widget_set_margin_start(GTK_WIDGET(A.reportList), 6);
  gtk_widget_set_margin_end(GTK_WIDGET(A.reportList), 6);
  gtk_widget_set_margin_bottom(GTK_WIDGET(A.reportList), 6);
  for (const auto &r : Reports()) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_box_append(GTK_BOX(row), Label(r.title, "report-title"));
    gtk_box_append(GTK_BOX(row), Label(r.desc, "report-desc"));
    gtk_list_box_append(A.reportList, row);
  }
  g_signal_connect(A.reportList, "row-selected", CB(+[](GtkListBox *,
    GtkListBoxRow *row, gpointer) {
    if (!row) return;
    A.reportIndex = gtk_list_box_row_get_index(row);
    RunReport();
  }), nullptr);
  GtkWidget *listCard = Card("ASTROLOG REPORTS",
    Scrolled(GTK_WIDGET(A.reportList)));
  gtk_widget_set_size_request(listCard, 260, -1);
  gtk_box_append(GTK_BOX(page), listCard);

  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(right, "card");
  gtk_widget_set_hexpand(right, TRUE);
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start(bar, 14);
  gtk_widget_set_margin_end(bar, 8);
  gtk_widget_set_margin_top(bar, 8);
  gtk_widget_set_margin_bottom(bar, 4);
  A.reportTitle = GTK_LABEL(Label("", "strong"));
  gtk_widget_set_hexpand(GTK_WIDGET(A.reportTitle), TRUE);
  gtk_box_append(GTK_BOX(bar), GTK_WIDGET(A.reportTitle));
  A.reportSpin = GTK_SPINNER(gtk_spinner_new());
  gtk_box_append(GTK_BOX(bar), GTK_WIDGET(A.reportSpin));
  GtkWidget *copy = gtk_button_new_from_icon_name("astrolog-copy-symbolic");
  gtk_widget_add_css_class(copy, "flat");
  gtk_widget_set_tooltip_text(copy, "Copy report");
  g_signal_connect(copy, "clicked", CB(+[](GtkButton *, gpointer) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(A.reportView);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    gchar *t = gtk_text_buffer_get_text(buf, &s, &e, FALSE);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(A.win)), t);
    g_free(t);
    Toast("Report copied to clipboard");
  }), nullptr);
  gtk_box_append(GTK_BOX(bar), copy);
  GtkWidget *saveBtn = gtk_button_new_from_icon_name("astrolog-save-symbolic");
  gtk_widget_add_css_class(saveBtn, "flat");
  gtk_widget_set_tooltip_text(saveBtn, "Save report as text");
  g_signal_connect(saveBtn, "clicked", CB(+[](GtkButton *, gpointer) {
    GtkFileDialog *dlg = gtk_file_dialog_new();
    std::string fn = A.current.name + " - " +
      Reports()[A.reportIndex].title + ".txt";
    gtk_file_dialog_set_initial_name(dlg, fn.c_str());
    gtk_file_dialog_save(dlg, A.win, nullptr, +[](GObject *src,
      GAsyncResult *res, gpointer) {
      GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res,
        nullptr);
      if (!f) return;
      GtkTextBuffer *buf = gtk_text_view_get_buffer(A.reportView);
      GtkTextIter s, e;
      gtk_text_buffer_get_bounds(buf, &s, &e);
      gchar *t = gtk_text_buffer_get_text(buf, &s, &e, FALSE);
      if (g_file_replace_contents(f, t, strlen(t), nullptr, FALSE,
        G_FILE_CREATE_NONE, nullptr, nullptr, nullptr))
        Toast("Report saved");
      g_free(t);
      g_object_unref(f);
    }, nullptr);
    g_object_unref(dlg);
  }), nullptr);
  gtk_box_append(GTK_BOX(bar), saveBtn);
  gtk_box_append(GTK_BOX(right), bar);

  A.reportView = GTK_TEXT_VIEW(gtk_text_view_new());
  gtk_text_view_set_editable(A.reportView, FALSE);
  gtk_text_view_set_monospace(A.reportView, TRUE);
  gtk_text_view_set_wrap_mode(A.reportView, GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_left_margin(A.reportView, 16);
  gtk_text_view_set_right_margin(A.reportView, 16);
  gtk_text_view_set_top_margin(A.reportView, 8);
  gtk_text_view_set_bottom_margin(A.reportView, 16);
  gtk_widget_add_css_class(GTK_WIDGET(A.reportView), "report-view");
  gtk_box_append(GTK_BOX(right), Scrolled(GTK_WIDGET(A.reportView), true));
  gtk_box_append(GTK_BOX(page), right);
  return page;
}

// Switch between side-by-side and stacked page layouts.
void ApplyLayout(bool narrow) {
  if (A.narrow == (int)narrow) return;
  A.narrow = narrow;
  GtkOrientation o = narrow ? GTK_ORIENTATION_VERTICAL :
    GTK_ORIENTATION_HORIZONTAL;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(A.wheelPage), o);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(A.aspPage), o);
  gtk_widget_set_size_request(GTK_WIDGET(A.wheel), narrow ? -1 : 300,
    narrow ? 460 : 300);
  gtk_widget_set_size_request(A.wheelSide, narrow ? -1 : 220, -1);
  gtk_scrolled_window_set_min_content_height(
    GTK_SCROLLED_WINDOW(A.bodyScroll), narrow ? 560 : -1);
  gtk_widget_set_size_request(A.gridScroll, narrow ? -1 : 260,
    narrow ? 420 : 260);
  gtk_widget_set_size_request(A.aspCard, narrow ? -1 : 260,
    narrow ? 480 : -1);
  // Re-place the pane handles for the new orientation.
  A.wheelPaneSet = A.aspPaneSet = false;
}

void AddShortcut(GtkShortcutController *sc, const char *accel,
  GtkShortcutFunc fn, gpointer data = nullptr) {
  gtk_shortcut_controller_add_shortcut(sc, gtk_shortcut_new(
    gtk_shortcut_trigger_parse_string(accel),
    gtk_callback_action_new(fn, data, nullptr)));
}

const char *const kPages[] = {"wheel", "positions", "aspects", "balance",
  "reports"};

void ShowPage(int i) {
  gtk_stack_set_visible_child_name(A.stack, kPages[std::clamp(i, 0, 4)]);
}

GtkWidget *MenuItem(const char *label, const char *accel,
  std::function<void()> fn) {
  GtkWidget *btn = gtk_button_new();
  gtk_widget_add_css_class(btn, "flat");
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
  GtkWidget *l = Label(label);
  gtk_widget_set_hexpand(l, TRUE);
  gtk_box_append(GTK_BOX(row), l);
  if (accel) gtk_box_append(GTK_BOX(row), Label(accel, "hint"));
  gtk_button_set_child(GTK_BUTTON(btn), row);
  auto *f = new std::function<void()>(std::move(fn));
  g_signal_connect_data(btn, "clicked", CB(+[](GtkButton *b,
    gpointer d) {
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(b), GTK_TYPE_POPOVER);
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    (*(std::function<void()> *)d)();
  }), f, [](gpointer d, GClosure *) { delete (std::function<void()> *)d; },
    GConnectFlags(0));
  return btn;
}

void OnThemeChanged() {
  const theme::Palette &p = theme::Current();
  if (A.chart.ok) UpdateViews();
  gtk_widget_queue_draw(GTK_WIDGET(A.wheel));
  if (p.name != A.lastTheme) {
    Toast("Omarchy theme: " + p.name);
    A.lastTheme = p.name;
  }
}

void BuildWindow(GtkApplication *app) {
  A.win = GTK_WINDOW(gtk_application_window_new(app));
  gtk_window_set_title(A.win, "Astrolog Studio");
  gtk_window_set_default_size(A.win, 1440, 920);
  gtk_window_set_icon_name(A.win, "astrolog-studio");

  // Header bar.
  GtkWidget *hb = gtk_header_bar_new();
  gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), FALSE);
  GtkWidget *toggle = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(toggle), "astrolog-sidebar-symbolic");
  gtk_widget_add_css_class(toggle, "flat");
  gtk_widget_set_tooltip_text(toggle, "Toggle sidebar (F9)");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), toggle);
  GtkWidget *brand = Markup("<span>✦</span> ASTROLOG <span alpha=\"55%\" "
    "weight=\"normal\">STUDIO</span>", "app-title");
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), brand);

  A.stack = GTK_STACK(gtk_stack_new());
  gtk_stack_set_transition_type(A.stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration(A.stack, 140);
  GtkWidget *switcher = gtk_stack_switcher_new();
  gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), A.stack);
  gtk_widget_set_valign(switcher, GTK_ALIGN_CENTER);
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb), switcher);

  GtkWidget *menuBtn = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menuBtn), "astrolog-menu-symbolic");
  gtk_widget_add_css_class(menuBtn, "flat");
  GtkWidget *pop = gtk_popover_new();
  GtkWidget *menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(menu), MenuItem("Save chart", "Ctrl+S", [] {
    SaveCurrent();
  }));
  gtk_box_append(GTK_BOX(menu), MenuItem("Cast for now", "Ctrl+N", [] {
    CastNow();
  }));
  gtk_box_append(GTK_BOX(menu), MenuItem("Find a city", "Ctrl+L", [] {
    gtk_revealer_set_reveal_child(A.sidebar, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(A.city));
  }));
  gtk_box_append(GTK_BOX(menu), MenuItem("Copy as Astrolog command", nullptr,
    [] {
      gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(A.win)),
        CommandLine().c_str());
      Toast("Command line copied");
    }));
  gtk_box_append(GTK_BOX(menu), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  gtk_box_append(GTK_BOX(menu), MenuItem("About Astrolog Studio", nullptr, [] {
    const theme::Palette &p = theme::Current();
    std::string msg = "Astrolog Studio — GTK front end for Astrolog 8.00\n"
      "Astrolog © 1991–2026 Walter D. Pullen · uses the Swiss Ephemeris\n"
      "Free software under the GNU GPL v2 or later, without warranty.\n"
      "Theme: " + p.name + (p.fromOmarchy ? " (Omarchy, live)" : "") +
      " · Fonts: " + p.font + ", " + p.mono;
    GtkAlertDialog *d = gtk_alert_dialog_new("%s", "Astrolog Studio");
    gtk_alert_dialog_set_detail(d, msg.c_str());
    gtk_alert_dialog_show(d, A.win);
    g_object_unref(d);
  }));
  gtk_box_append(GTK_BOX(menu), MenuItem("Quit", "Ctrl+Q", [] {
    gtk_window_close(A.win);
  }));
  gtk_popover_set_child(GTK_POPOVER(pop), menu);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(menuBtn), pop);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), menuBtn);
  GtkWidget *nowBtn = gtk_button_new_from_icon_name(
    "astrolog-now-symbolic");
  gtk_widget_add_css_class(nowBtn, "flat");
  gtk_widget_set_tooltip_text(nowBtn, "Cast for now (Ctrl+N)");
  g_signal_connect(nowBtn, "clicked", CB(+[](GtkButton *, gpointer) {
    CastNow();
  }), nullptr);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), nowBtn);
  gtk_window_set_titlebar(A.win, hb);

  // Body.
  GtkWidget *overlay = gtk_overlay_new();
  GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  A.sidebar = GTK_REVEALER(gtk_revealer_new());
  gtk_revealer_set_transition_type(A.sidebar,
    GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
  gtk_revealer_set_transition_duration(A.sidebar, 180);
  gtk_revealer_set_child(A.sidebar, BuildSidebar());
  gtk_widget_set_hexpand(GTK_WIDGET(A.sidebar), FALSE);
  gtk_revealer_set_reveal_child(A.sidebar, TRUE);
  g_object_bind_property(toggle, "active", A.sidebar, "reveal-child",
    GBindingFlags(G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE));
  gtk_box_append(GTK_BOX(body), GTK_WIDGET(A.sidebar));

  GtkWidget *main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(main, TRUE);
  GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(hero, "hero");
  GtkWidget *heroText = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand(heroText, TRUE);
  A.heroName = GTK_LABEL(Label("", "hero-name"));
  gtk_label_set_ellipsize(A.heroName, PANGO_ELLIPSIZE_END);
  A.heroMeta = GTK_LABEL(Label("", "hero-meta"));
  gtk_label_set_selectable(A.heroMeta, TRUE);
  gtk_box_append(GTK_BOX(heroText), GTK_WIDGET(A.heroName));
  gtk_box_append(GTK_BOX(heroText), GTK_WIDGET(A.heroMeta));
  gtk_box_append(GTK_BOX(hero), heroText);
  A.chips = GTK_FLOW_BOX(gtk_flow_box_new());
  gtk_flow_box_set_selection_mode(A.chips, GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line(A.chips, 8);
  gtk_flow_box_set_column_spacing(A.chips, 8);
  gtk_flow_box_set_row_spacing(A.chips, 8);
  gtk_widget_set_halign(GTK_WIDGET(A.chips), GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(hero), GTK_WIDGET(A.chips));
  gtk_box_append(GTK_BOX(main), hero);

  gtk_stack_add_titled(A.stack, BuildWheelPage(), "wheel", "Wheel");
  gtk_stack_add_titled(A.stack, BuildPositionsPage(), "positions", "Positions");
  gtk_stack_add_titled(A.stack, BuildAspectsPage(), "aspects", "Aspects");
  gtk_stack_add_titled(A.stack, BuildBalancePage(), "balance", "Balance");
  gtk_stack_add_titled(A.stack, BuildReportsPage(), "reports", "Reports");
  gtk_widget_set_vexpand(GTK_WIDGET(A.stack), TRUE);
  g_signal_connect(A.stack, "notify::visible-child-name", CB(+[](
    GObject *, GParamSpec *, gpointer) {
    const char *n = gtk_stack_get_visible_child_name(A.stack);
    for (int i = 0; i < 5; i++)
      if (n && strcmp(n, kPages[i]) == 0) A.prefs.page = i;
    SchedulePrefsSave();
    if (n && strcmp(n, "reports") == 0) {
      if (!gtk_list_box_get_selected_row(A.reportList))
        gtk_list_box_select_row(A.reportList,
          gtk_list_box_get_row_at_index(A.reportList, A.reportIndex));
      else
        RunReport();
    }
  }), nullptr);
  gtk_box_append(GTK_BOX(main), GTK_WIDGET(A.stack));
  gtk_box_append(GTK_BOX(body), main);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), body);

  A.toast = GTK_REVEALER(gtk_revealer_new());
  gtk_revealer_set_transition_type(A.toast,
    GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
  gtk_widget_set_halign(GTK_WIDGET(A.toast), GTK_ALIGN_CENTER);
  gtk_widget_set_valign(GTK_WIDGET(A.toast), GTK_ALIGN_END);
  gtk_widget_set_can_target(GTK_WIDGET(A.toast), FALSE);
  A.toastLabel = GTK_LABEL(Label("", "toast"));
  gtk_revealer_set_child(A.toast, GTK_WIDGET(A.toastLabel));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), GTK_WIDGET(A.toast));
  gtk_window_set_child(A.win, overlay);

  // Keyboard shortcuts.
  GtkEventController *sc = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(sc),
    GTK_SHORTCUT_SCOPE_GLOBAL);
  auto S = GTK_SHORTCUT_CONTROLLER(sc);
  AddShortcut(S, "<Control>s", +[](GtkWidget *, GVariant *, gpointer) -> gboolean {
    SaveCurrent(); return TRUE; });
  AddShortcut(S, "<Control>n", +[](GtkWidget *, GVariant *, gpointer) -> gboolean {
    CastNow(); return TRUE; });
  AddShortcut(S, "<Control>l", +[](GtkWidget *, GVariant *, gpointer) -> gboolean {
    gtk_revealer_set_reveal_child(A.sidebar, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(A.city)); return TRUE; });
  AddShortcut(S, "<Control>q", +[](GtkWidget *, GVariant *, gpointer) -> gboolean {
    gtk_window_close(A.win); return TRUE; });
  AddShortcut(S, "F9", +[](GtkWidget *, GVariant *, gpointer) -> gboolean {
    gtk_revealer_set_reveal_child(A.sidebar,
      !gtk_revealer_get_reveal_child(A.sidebar)); return TRUE; });
  for (int i = 0; i < 5; i++) {
    char accel[32];
    snprintf(accel, sizeof(accel), "<Control>%d", i + 1);
    AddShortcut(S, accel, +[](GtkWidget *, GVariant *, gpointer d) -> gboolean {
      ShowPage(GPOINTER_TO_INT(d)); return TRUE; }, GINT_TO_POINTER(i));
  }
  gtk_widget_add_controller(GTK_WIDGET(A.win), sc);

  // Watch the content width every frame (cheap) and restack pages when the
  // window is tiled narrow. Hysteresis avoids flapping at the threshold.
  ApplyLayout(false);
  gtk_widget_add_tick_callback(GTK_WIDGET(A.win), [](GtkWidget *,
    GdkFrameClock *, gpointer) -> gboolean {
    int w = gtk_widget_get_width(GTK_WIDGET(A.stack));
    if (w <= 0) return G_SOURCE_CONTINUE;
    if (A.narrow != 1 && w < 700) ApplyLayout(true);
    else if (A.narrow != 0 && w > 740) ApplyLayout(false);
    // Place each pane handle once the page has a size: the right panel
    // gets its remembered width; stacked, the wheel gets a square.
    auto place = [](GtkWidget *paned, int panel, bool *done) {
      if (*done) return;
      int pw = gtk_widget_get_width(paned);
      if (pw <= 0) return;
      int pos = A.narrow == 1 ? std::max(360, std::min(pw, 560)) :
        std::max(300, pw - panel);
      gtk_paned_set_position(GTK_PANED(paned), pos);
      *done = true;
    };
    place(A.wheelPage, A.prefs.wheelPanel, &A.wheelPaneSet);
    place(A.aspPage, A.prefs.aspectPanel, &A.aspPaneSet);
    return G_SOURCE_CONTINUE;
  }, nullptr, nullptr);

  g_signal_connect(A.win, "close-request", CB(+[](GtkWindow *,
    gpointer) -> gboolean {
    SavePrefsNow();
    return FALSE;
  }), nullptr);
  // Also persist whenever a pane handle is dragged.
  for (GtkWidget *pane : {A.wheelPage, A.aspPage})
    g_signal_connect(pane, "notify::position", CB(+[](GObject *,
      GParamSpec *, gpointer) { SchedulePrefsSave(); }), nullptr);
}

void Activate(GtkApplication *app, gpointer) {
  if (A.win) {
    gtk_window_present(A.win);
    return;
  }
  // Bundled monochrome icons, so the UI looks the same under any icon theme
  // (e.g. Yaru lacks several symbolic icons and falls back to color ones).
  std::string icons = astro::DataDir() + "/studio/icons";
  gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(
    gdk_display_get_default()), icons.c_str());
  theme::Install(OnThemeChanged);
  A.lastTheme = theme::Current().name;
  BuildWindow(app);

  // Restore preferences.
  A.prefs = store::LoadPrefs();
  A.loading = true;
  const auto &s = A.prefs.settings;
  gtk_drop_down_set_selected(A.house, s.houseSystem);
  gtk_drop_down_set_selected(A.zodiac, s.sidereal ? 1 : 0);
  gtk_switch_set_active(A.asteroids, s.asteroids);
  gtk_switch_set_active(A.points, s.points);
  gtk_switch_set_active(A.minor, s.minorAspects);
  gtk_switch_set_active(A.trueNode, s.trueNode);
  gtk_drop_down_set_selected(A.textSize, std::clamp(A.prefs.textSize, 0, 3));
  ApplyTextSize();
  gtk_switch_set_active(A.transits, A.prefs.transits);
  A.loading = false;

  A.saved = store::LoadCharts();
  RefreshSaved();

  if (A.prefs.haveLast && !A.startNow) {
    LoadIntoForm(A.prefs.last);
  } else {
    // First run: a chart for this moment, here.
    CastNow();
  }
  ShowPage(A.prefs.page);
  if (const char *pg = g_getenv("ASTROLOG_STUDIO_SNAPSHOT_PAGE"))
    ShowPage(atoi(pg));
  gtk_window_present(A.win);

  // Developer aid: ASTROLOG_STUDIO_SNAPSHOT=out.png saves an image of the
  // window once it has settled, then quits (used for README screenshots and
  // visual checks, e.g. under GDK_BACKEND=broadway).
  if (g_getenv("ASTROLOG_STUDIO_SNAPSHOT"))
    g_timeout_add(2500, [](gpointer) -> gboolean {
      GtkWidget *w = GTK_WIDGET(A.win);
      GdkPaintable *pt = gtk_widget_paintable_new(w);
      GtkSnapshot *snap = gtk_snapshot_new();
      gdk_paintable_snapshot(pt, snap, gtk_widget_get_width(w),
        gtk_widget_get_height(w));
      GskRenderNode *node = gtk_snapshot_free_to_node(snap);
      if (node) {
        GskRenderer *r = gtk_native_get_renderer(GTK_NATIVE(w));
        GdkTexture *tex = gsk_renderer_render_texture(r, node, nullptr);
        gdk_texture_save_to_png(tex, g_getenv("ASTROLOG_STUDIO_SNAPSHOT"));
        g_object_unref(tex);
        gsk_render_node_unref(node);
      }
      g_object_unref(pt);
      g_application_quit(G_APPLICATION(A.app));
      return G_SOURCE_REMOVE;
    }, nullptr);
}

std::string FindDataDir(const char *argv0) {
  const char *env = g_getenv("ASTROLOG_STUDIO_DATA");
  std::vector<std::string> cands;
  if (env) cands.push_back(env);
  gchar *self = g_file_read_link("/proc/self/exe", nullptr);
  std::string exe = self ? self : argv0;
  g_free(self);
  gchar *dir = g_path_get_dirname(exe.c_str());
  cands.push_back(dir);
  cands.push_back(std::string(dir) + "/..");
  cands.push_back(std::string(dir) + "/../share/astrolog-studio");
  g_free(dir);
  cands.push_back(std::string(g_get_user_data_dir()) + "/astrolog-studio/data");
  cands.push_back("/usr/local/share/astrolog-studio");
  cands.push_back("/usr/share/astrolog-studio");
  for (const auto &c : cands) {
    std::string f = c + "/astrolog.as";
    if (g_file_test(f.c_str(), G_FILE_TEST_EXISTS)) {
      gchar *canon = g_canonicalize_filename(c.c_str(), nullptr);
      std::string r = canon;
      g_free(canon);
      return r;
    }
  }
  return "";
}

}  // namespace

int main(int argc, char **argv) {
  // `astrolog-studio --cli ...` behaves exactly like the classic astrolog
  // command line program. Reports are generated this way in a subprocess.
  if (argc > 1 && strcmp(argv[1], "--cli") == 0)
    return astro::RunCli(argc - 1, argv + 1);

  // --now opens with a chart for the current moment at the current location
  // (handy for a desktop keybinding). Strip it before GTK sees the args.
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], "--now") == 0) {
      A.startNow = true;
      for (int j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
      argc--;
      break;
    }

  std::string data = FindDataDir(argv[0]);
  std::string err;
  if (data.empty() || !astro::Init(data, &err)) {
    g_printerr("astrolog-studio: cannot find Astrolog data files "
      "(astrolog.as, atlas.as, ephem/). Set ASTROLOG_STUDIO_DATA.\n%s\n",
      err.c_str());
    return 1;
  }
  if (!err.empty()) g_printerr("astrolog-studio: %s\n", err.c_str());

  // A snapshot run is a separate, throwaway instance: it must not hand off
  // to an already running window or touch saved preferences.
  bool snapshot = g_getenv("ASTROLOG_STUDIO_SNAPSHOT") != nullptr;
  A.app = gtk_application_new("org.astrolog.Studio",
    snapshot ? G_APPLICATION_NON_UNIQUE : G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(A.app, "activate", G_CALLBACK(Activate), nullptr);
  int status = g_application_run(G_APPLICATION(A.app), argc, argv);
  g_object_unref(A.app);
  return status;
}
