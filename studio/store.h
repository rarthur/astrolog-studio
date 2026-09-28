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
//
// Stored as GKeyFile (INI) files under $XDG_DATA_HOME/astrolog-studio/.

#pragma once

#include <string>
#include <vector>

#include "engine.h"

namespace store {

std::vector<astro::BirthData> LoadCharts();
void SaveCharts(const std::vector<astro::BirthData> &charts);

struct Prefs {
  astro::Settings settings;
  astro::BirthData last;
  bool haveLast = false;
  bool transits = false;
  int page = 0;
  int sidebarWidth = 340;
  int wheelPanel = 340;    // Width of the placements/focus panel.
  int aspectPanel = 420;   // Width of the aspect list panel.
  int textSize = 1;        // 0 small, 1 default, 2 large, 3 extra large.
};

Prefs LoadPrefs();
void SavePrefs(const Prefs &p);

}  // namespace store
