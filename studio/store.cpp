// Copyright (C) 2026 Ryan Arthur. Part of Astrolog Studio, a modified
// version of Astrolog 8.00, which is Copyright (C) 1991-2026 by Walter D.
// Pullen (see README.md and the notices in the Astrolog source files).
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
//
// Astrolog Studio - persistence of saved charts and preferences.

#include "store.h"

#include <glib.h>
#include <glib/gstdio.h>

namespace store {

namespace {

std::string Dir() {
  std::string d = std::string(g_get_user_data_dir()) + "/astrolog-studio";
  g_mkdir_with_parents(d.c_str(), 0755);
  return d;
}

void WriteBirth(GKeyFile *kf, const char *group, const astro::BirthData &b) {
  g_key_file_set_string(kf, group, "name", b.name.c_str());
  g_key_file_set_string(kf, group, "location", b.location.c_str());
  g_key_file_set_string(kf, group, "zone", b.zoneName.c_str());
  g_key_file_set_integer(kf, group, "zone_index", b.zoneIndex);
  char date[32], time[32];
  snprintf(date, sizeof(date), "%04d-%02d-%02d", b.year, b.month, b.day);
  snprintf(time, sizeof(time), "%02d:%02d:%02d", b.hour, b.minute, b.second);
  g_key_file_set_string(kf, group, "date", date);
  g_key_file_set_string(kf, group, "time", time);
  g_key_file_set_double(kf, group, "utc_offset", b.utcOffset);
  g_key_file_set_double(kf, group, "dst", b.dst);
  g_key_file_set_boolean(kf, group, "lmt", b.lmt);
  g_key_file_set_double(kf, group, "longitude", b.lon);
  g_key_file_set_double(kf, group, "latitude", b.lat);
}

bool ReadBirth(GKeyFile *kf, const char *group, astro::BirthData *b) {
  auto str = [&](const char *k) {
    gchar *v = g_key_file_get_string(kf, group, k, nullptr);
    std::string s = v ? v : "";
    g_free(v);
    return s;
  };
  std::string date = str("date"), time = str("time");
  if (sscanf(date.c_str(), "%d-%d-%d", &b->year, &b->month, &b->day) != 3)
    return false;
  if (sscanf(time.c_str(), "%d:%d:%d", &b->hour, &b->minute, &b->second) < 2)
    return false;
  b->name = str("name");
  b->location = str("location");
  b->zoneName = str("zone");
  b->zoneIndex = g_key_file_get_integer(kf, group, "zone_index", nullptr);
  if (!g_key_file_has_key(kf, group, "zone_index", nullptr)) b->zoneIndex = -1;
  b->utcOffset = g_key_file_get_double(kf, group, "utc_offset", nullptr);
  b->dst = g_key_file_get_double(kf, group, "dst", nullptr);
  b->lmt = g_key_file_get_boolean(kf, group, "lmt", nullptr);
  b->lon = g_key_file_get_double(kf, group, "longitude", nullptr);
  b->lat = g_key_file_get_double(kf, group, "latitude", nullptr);
  return true;
}

}  // namespace

std::vector<astro::BirthData> LoadCharts() {
  std::vector<astro::BirthData> v;
  GKeyFile *kf = g_key_file_new();
  std::string path = Dir() + "/charts.ini";
  if (g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE, nullptr)) {
    gsize n = 0;
    gchar **groups = g_key_file_get_groups(kf, &n);
    for (gsize i = 0; i < n; i++) {
      astro::BirthData b;
      if (ReadBirth(kf, groups[i], &b)) v.push_back(b);
    }
    g_strfreev(groups);
  }
  g_key_file_free(kf);
  return v;
}

void SaveCharts(const std::vector<astro::BirthData> &charts) {
  GKeyFile *kf = g_key_file_new();
  for (size_t i = 0; i < charts.size(); i++) {
    char group[32];
    snprintf(group, sizeof(group), "chart-%03zu", i);
    WriteBirth(kf, group, charts[i]);
  }
  std::string path = Dir() + "/charts.ini";
  g_key_file_save_to_file(kf, path.c_str(), nullptr);
  g_key_file_free(kf);
}

Prefs LoadPrefs() {
  Prefs p;
  GKeyFile *kf = g_key_file_new();
  std::string path = Dir() + "/prefs.ini";
  if (g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE, nullptr)) {
    const char *g = "settings";
    auto &s = p.settings;
    s.houseSystem = g_key_file_get_integer(kf, g, "house_system", nullptr);
    s.sidereal = g_key_file_get_boolean(kf, g, "sidereal", nullptr);
    s.minorAspects = g_key_file_get_boolean(kf, g, "minor_aspects", nullptr);
    if (g_key_file_has_key(kf, g, "asteroids", nullptr))
      s.asteroids = g_key_file_get_boolean(kf, g, "asteroids", nullptr);
    if (g_key_file_has_key(kf, g, "points", nullptr))
      s.points = g_key_file_get_boolean(kf, g, "points", nullptr);
    s.trueNode = g_key_file_get_boolean(kf, g, "true_node", nullptr);
    p.transits = g_key_file_get_boolean(kf, g, "transits", nullptr);
    p.page = g_key_file_get_integer(kf, g, "page", nullptr);
    if (g_key_file_has_key(kf, g, "sidebar_width", nullptr))
      p.sidebarWidth = g_key_file_get_integer(kf, g, "sidebar_width", nullptr);
    if (g_key_file_has_key(kf, g, "wheel_panel", nullptr))
      p.wheelPanel = g_key_file_get_integer(kf, g, "wheel_panel", nullptr);
    if (g_key_file_has_key(kf, g, "aspect_panel", nullptr))
      p.aspectPanel = g_key_file_get_integer(kf, g, "aspect_panel", nullptr);
    p.haveLast = ReadBirth(kf, "last", &p.last);
  }
  g_key_file_free(kf);
  return p;
}

void SavePrefs(const Prefs &p) {
  GKeyFile *kf = g_key_file_new();
  const char *g = "settings";
  const auto &s = p.settings;
  g_key_file_set_integer(kf, g, "house_system", s.houseSystem);
  g_key_file_set_boolean(kf, g, "sidereal", s.sidereal);
  g_key_file_set_boolean(kf, g, "minor_aspects", s.minorAspects);
  g_key_file_set_boolean(kf, g, "asteroids", s.asteroids);
  g_key_file_set_boolean(kf, g, "points", s.points);
  g_key_file_set_boolean(kf, g, "true_node", s.trueNode);
  g_key_file_set_boolean(kf, g, "transits", p.transits);
  g_key_file_set_integer(kf, g, "page", p.page);
  g_key_file_set_integer(kf, g, "sidebar_width", p.sidebarWidth);
  g_key_file_set_integer(kf, g, "wheel_panel", p.wheelPanel);
  g_key_file_set_integer(kf, g, "aspect_panel", p.aspectPanel);
  if (p.haveLast) WriteBirth(kf, "last", p.last);
  std::string path = Dir() + "/prefs.ini";
  g_key_file_save_to_file(kf, path.c_str(), nullptr);
  g_key_file_free(kf);
}

}  // namespace store
