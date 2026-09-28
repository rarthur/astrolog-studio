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
// engine.cpp: the bridge between the Astrolog core (a C style program built
// around global state) and the GUI. This is the only file of the front end
// that includes the Astrolog headers.

// GLib and the standard headers must come before astrolog.h, whose short
// macros (loop, planet, space...) would otherwise mangle their declarations.
#include <glib.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine.h"

#include "../astrolog.h"

// Tables defined in atlas.cpp which are not declared in extern.h.
extern CONST char *rgszzn[];
void SwissEnsurePath();

// The classic command line entry point from astrolog.cpp (renamed at build
// time with -Dmain=astrolog_cli_main).
int astrolog_cli_main(int argc, char **argv);

namespace astro {

namespace {

std::string g_dataDir;
std::string g_progName;
std::string g_selfExe;
bool g_init = false;

std::string ToUtf8(const char *sz) {
  if (sz == nullptr)
    return "";
  if (g_utf8_validate(sz, -1, nullptr))
    return sz;
  gchar *conv = g_convert(sz, -1, "UTF-8", "ISO-8859-1", nullptr, nullptr,
    nullptr);
  std::string s = conv ? conv : "";
  g_free(conv);
  return s;
}

const char *const kBodyGlyph[] = {
  "⊕",  // Earth
  "☉",  // Sun
  "☽",  // Moon
  "☿",  // Mercury
  "♀",  // Venus
  "♂",  // Mars
  "♃",  // Jupiter
  "♄",  // Saturn
  "♅",  // Uranus
  "♆",  // Neptune
  "♇",  // Pluto
  "⚷",  // Chiron
  "⚳",  // Ceres
  "⚴",  // Pallas
  "⚵",  // Juno
  "⚶",  // Vesta
  "☊",  // North Node
  "☋",  // South Node
  "⚸",  // Lilith
  "⊗",  // Part of Fortune
  "Vx",      // Vertex
  "EP",      // East Point
};

const char *const kSignGlyph[12] = {
  "♈", "♉", "♊", "♋", "♌", "♍",
  "♎", "♏", "♐", "♑", "♒", "♓",
};

BodyKind KindOf(int id) {
  if (id == oSun || id == oMoo) return kLuminary;
  if (id >= oMer && id <= oPlu) return kPlanet;
  if (id >= oChi && id <= oVes) return kAsteroid;
  if (id == oNod || id == oSou) return kNode;
  if (id == oAsc || id == oMC) return kAngle;
  return kPoint;
}

std::string BodyName(int id) {
  if (id == oAsc) return "Ascendant";
  if (id == oMC) return "Midheaven";
  if (id == oFor) return "Fortune";
  return ToUtf8(szObjName[id]);
}

std::string BodyGlyph(int id) {
  if (id == oAsc) return "AC";
  if (id == oMC) return "MC";
  if (id >= 0 && id <= oEP) return kBodyGlyph[id];
  return ToUtf8(szObjName[id]).substr(0, 2);
}

// Apply GUI settings to the Astrolog global user settings.
void ApplySettings(const Settings &s) {
  us.nHouseSystem = std::clamp(s.houseSystem, 0, cSystem - 1);
  us.fSidereal = s.sidereal;
  us.fTrueNode = s.trueNode;
  us.nAsp = s.minorAspects ? aBQn : aSex;
  us.nAppSep = 0;
  us.fParallel = us.fDistance = fFalse;
  us.fAspect3D = us.fAspectLat = fFalse;

  for (int i = 0; i <= oEP; i++)
    ignore[i] = fTrue;
  for (int i = oSun; i <= oPlu; i++)
    ignore[i] = fFalse;
  ignore[oNod] = ignore[oSou] = fFalse;
  if (s.asteroids)
    for (int i = oChi; i <= oVes; i++)
      ignore[i] = fFalse;
  if (s.points)
    ignore[oLil] = ignore[oFor] = ignore[oVtx] = fFalse;
  ignore[oAsc] = ignore[oMC] = fFalse;
  AdjustRestrictions();
}

// Fill the Astrolog core chart record from GUI birth data.
void SetCore(const BirthData &bd) {
  real tim = bd.hour + bd.minute / 60.0 + bd.second / 3600.0;
  real zon = bd.lmt ? zonLMT : -bd.utcOffset;
  SetCI(ciCore, bd.month, bd.day, bd.year, tim, bd.lmt ? 0.0 : bd.dst, zon,
    -bd.lon, bd.lat);
  ciMain = ciCore;
}

std::vector<int> WantedBodies(const Settings &s) {
  std::vector<int> v;
  for (int i = oSun; i <= oPlu; i++) v.push_back(i);
  if (s.asteroids)
    for (int i = oChi; i <= oVes; i++) v.push_back(i);
  v.push_back(oNod);
  v.push_back(oSou);
  if (s.points) {
    v.push_back(oLil);
    v.push_back(oFor);
    v.push_back(oVtx);
  }
  v.push_back(oAsc);
  v.push_back(oMC);
  return v;
}

double AngleDiff(double a, double b) {  // Signed shortest a - b.
  double d = std::fmod(a - b + 540.0, 360.0) - 180.0;
  return d;
}

// Find the aspect (if any) between two positions, using Astrolog's aspect
// angles and orb tables.
bool FindAspect(double lonA, double spdA, int idA, double lonB, double spdB,
  int idB, int nAsp, double orbScale, Aspect *out) {
  double sep = std::fabs(AngleDiff(lonA, lonB));
  int best = 0;
  double bestOrb = 1e9, bestMax = 0.0;
  for (int asp = 1; asp <= nAsp; asp++) {
    if (FIgnoreA(asp))
      continue;
    // Angles only make conjunctions and oppositions with each other.
    if (KindOf(idA) == kAngle && KindOf(idB) == kAngle)
      continue;
    // The nodes are always opposite; that tells us nothing.
    if ((idA == oNod && idB == oSou) || (idA == oSou && idB == oNod))
      continue;
    double maxOrb = GetOrb(idA, idB, asp) * orbScale;
    // Minor points get tighter orbs.
    if (KindOf(idA) == kPoint || KindOf(idB) == kPoint ||
      KindOf(idA) == kAsteroid || KindOf(idB) == kAsteroid)
      maxOrb = std::min(maxOrb, asp <= aSex ? 3.0 : 1.5);
    double orb = std::fabs(sep - rAspAngle[asp]);
    if (orb <= maxOrb && orb < bestOrb) {
      best = asp;
      bestOrb = orb;
      bestMax = maxOrb;
    }
  }
  if (!best)
    return false;
  out->type = best;
  out->orb = bestOrb;
  out->maxOrb = bestMax;
  // Applying if the separation is moving toward the exact aspect angle.
  double dt = 0.01;
  double sep2 = std::fabs(AngleDiff(lonA + spdA * dt, lonB + spdB * dt));
  out->applying = std::fabs(sep2 - rAspAngle[best]) < bestOrb;
  return true;
}

}  // namespace

bool Init(const std::string &dataDir, std::string *error) {
  if (g_init)
    return true;
  g_dataDir = dataDir;
  gchar *self = g_file_read_link("/proc/self/exe", nullptr);
  g_selfExe = self ? self : "astrolog-studio";
  g_free(self);

  // Astrolog finds its data files relative to the program's directory.
  g_progName = dataDir + "/astrolog";
  is.szProgName = (char *)g_progName.c_str();
  InitProgram();
  us.fNoQuit = fTrue;  // Errors must never exit the GUI.
  if (!FProcessSwitchFile(DEFAULT_INFOFILE, NULL)) {
    if (error) *error = "Could not read astrolog.as in " + dataDir;
    return false;
  }
  us.fNoQuit = fTrue;
  us.fAnsiColor = us.fAnsiChar = fFalse;
  ciTran = ciHexa = ciFive = ciFour = ciThre = ciTwin = ciMain = ciCore;
  // Astrolog composes a Swiss Ephemeris search path from several data
  // directories, and when the install location is deep that path overflows
  // the library's 242 character limit and is silently replaced with the
  // default. SE_EPHE_PATH takes priority, so point it straight at our files
  // (child report processes inherit it too).
  std::string ephe = dataDir + "/ephem";
  if (!g_getenv("SE_EPHE_PATH"))
    g_setenv("SE_EPHE_PATH", ephe.c_str(), TRUE);
  is.fSwissPathSet = fFalse;
  SwissEnsurePath();
  if (!FEnsureAtlas() && error)
    *error = "Atlas (atlas.as) could not be loaded; city search disabled.";
  FEnsureTimezoneChanges();
  g_init = true;
  return true;
}

const std::string &DataDir() { return g_dataDir; }

Chart Cast(const BirthData &bd, const Settings &s) {
  Chart c;
  c.data = bd;
  c.settings = s;
  if (!g_init) {
    c.error = "Engine not initialized";
    return c;
  }
  if (bd.month < 1 || bd.month > 12 || bd.day < 1 ||
    bd.day > DayInMonth(bd.month, bd.year)) {
    c.error = "Invalid date";
    return c;
  }
  if (std::fabs(bd.lat) > 90.0 || std::fabs(bd.lon) > 180.0) {
    c.error = "Invalid coordinates";
    return c;
  }
  ApplySettings(s);
  SetCore(bd);
  is.fHaveInfo = fTrue;
  CastChart(1);
  c.julianDay = is.JD;

  for (int i = 1; i <= 12; i++)
    c.cusps[i] = chouse[i];
  c.asc = chouse[1];
  c.mc = chouse[10];
  c.houseSystem = ToUtf8(szSystem[us.nHouseSystem]);
  c.zodiac = s.sidereal ? "Sidereal" : "Tropical";

  for (int id : WantedBodies(s)) {
    Body b;
    b.id = id;
    b.name = BodyName(id);
    b.glyph = BodyGlyph(id);
    b.kind = KindOf(id);
    b.lon = id == oAsc ? chouse[1] : (id == oMC ? chouse[10] : planet[id]);
    b.lat = planetalt[id];
    b.speed = ret[id];
    b.house = inhouse[id];
    if (b.house < 1 || b.house > 12) b.house = id == oAsc ? 1 : 10;
    b.retro = b.kind != kAngle && ret[id] < 0.0;
    c.bodies.push_back(b);
  }

  int nAsp = us.nAsp;
  for (size_t i = 0; i < c.bodies.size(); i++)
    for (size_t j = i + 1; j < c.bodies.size(); j++) {
      const Body &a = c.bodies[i], &b = c.bodies[j];
      Aspect asp;
      if (FindAspect(a.lon, a.speed, a.id, b.lon, b.speed, b.id, nAsp,
        s.orbScale, &asp)) {
        asp.a = (int)i;
        asp.b = (int)j;
        c.aspects.push_back(asp);
      }
    }
  std::sort(c.aspects.begin(), c.aspects.end(),
    [](const Aspect &x, const Aspect &y) { return x.orb < y.orb; });
  c.ok = true;
  return c;
}

std::vector<Aspect> CrossAspects(const Chart &outer, const Chart &inner,
  const Settings &s) {
  std::vector<Aspect> v;
  int nAsp = s.minorAspects ? aBQn : aSex;
  for (size_t i = 0; i < outer.bodies.size(); i++) {
    const Body &a = outer.bodies[i];
    if (a.kind == kAngle || a.kind == kPoint) continue;
    for (size_t j = 0; j < inner.bodies.size(); j++) {
      const Body &b = inner.bodies[j];
      Aspect asp;
      // Transits use tight orbs.
      if (FindAspect(a.lon, a.speed, a.id, b.lon, 0.0, b.id, nAsp,
        s.orbScale * 0.35, &asp)) {
        asp.a = (int)i;
        asp.b = (int)j;
        v.push_back(asp);
      }
    }
  }
  std::sort(v.begin(), v.end(),
    [](const Aspect &x, const Aspect &y) { return x.orb < y.orb; });
  return v;
}

std::vector<City> SearchCities(const std::string &query, int maxResults) {
  std::vector<City> out;
  if (!g_init || is.rgae == NULL || query.size() < 2)
    return out;
  // Split "City, Region" into the city part and an optional qualifier.
  std::string q = query, qual;
  size_t comma = q.find(',');
  if (comma != std::string::npos) {
    qual = q.substr(comma + 1);
    q = q.substr(0, comma);
  }
  auto trim = [](std::string &t) {
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t") + 1);
  };
  trim(q);
  trim(qual);
  if (q.empty())
    return out;
  gchar *qlow = g_utf8_casefold(q.c_str(), -1);
  gchar *quallow = g_utf8_casefold(qual.c_str(), -1);
  size_t qlen = strlen(qlow);

  for (int iae = 0; iae < is.cae; iae++) {
    const AtlasEntry &ae = is.rgae[iae];
    std::string nm = ToUtf8(ae.szNam);
    gchar *low = g_utf8_casefold(nm.c_str(), -1);
    const char *hit = strstr(low, qlow);
    int score = 0;
    if (hit) {
      if (strcmp(low, qlow) == 0) score = 100;
      else if (hit == low) score = 60;
      else if (hit[-1] == ' ' || hit[-1] == '-') score = 40;
      else score = 10;
      if (hit[qlen] == '\0') score += 5;
    }
    g_free(low);
    if (!score)
      continue;
    City c;
    c.display = ToUtf8(SzCity(iae));
    if (*quallow) {
      gchar *dl = g_utf8_casefold(c.display.c_str(), -1);
      bool ok = strstr(dl + nm.size(), quallow) != nullptr;
      g_free(dl);
      if (!ok)
        continue;
    }
    c.lon = -ae.lon;
    c.lat = ae.lat;
    c.zoneIndex = ae.izn;
    c.zoneName = ae.izn >= 0 ? ToUtf8(rgszzn[ae.izn]) : "";
    // The atlas is ordered by population within each country, so earlier
    // entries are more prominent cities; nudge them up slightly.
    c.score = score * 100000 - iae;
    out.push_back(c);
  }
  g_free(qlow);
  g_free(quallow);
  std::sort(out.begin(), out.end(),
    [](const City &a, const City &b) { return a.score > b.score; });
  if ((int)out.size() > maxResults)
    out.resize(maxResults);
  return out;
}

bool CityForZone(const std::string &zoneName, City *out) {
  if (!g_init || is.rgae == NULL || zoneName.empty())
    return false;
  // "America/New_York" -> "New York"
  std::string want = zoneName.substr(zoneName.rfind('/') + 1);
  std::replace(want.begin(), want.end(), '_', ' ');
  int first = -1, named = -1;
  for (int iae = 0; iae < is.cae; iae++) {
    int izn = is.rgae[iae].izn;
    if (izn < 0 || zoneName != rgszzn[izn])
      continue;
    if (first < 0) first = iae;
    if (named < 0 && g_ascii_strncasecmp(is.rgae[iae].szNam, want.c_str(),
      want.size()) == 0)
      named = iae;
  }
  int iae = named >= 0 ? named : first;
  if (iae < 0)
    return false;
  const AtlasEntry &ae = is.rgae[iae];
  out->display = ToUtf8(SzCity(iae));
  out->lon = -ae.lon;
  out->lat = ae.lat;
  out->zoneIndex = ae.izn;
  out->zoneName = zoneName;
  return true;
}

bool NearestCity(double lat, double lon, double maxKm, City *out) {
  if (!g_init || is.rgae == NULL)
    return false;
  const double rad = M_PI / 180.0;
  int best = -1;
  double bestKm = maxKm;
  for (int iae = 0; iae < is.cae; iae++) {
    const AtlasEntry &ae = is.rgae[iae];
    // Great-circle distance (haversine). Atlas longitudes are west positive.
    double dLat = (ae.lat - lat) * rad, dLon = (-ae.lon - lon) * rad;
    double h = std::sin(dLat / 2) * std::sin(dLat / 2) + std::cos(lat * rad) *
      std::cos(ae.lat * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
    double km = 2 * 6371.0 * std::asin(std::min(1.0, std::sqrt(h)));
    if (km < bestKm) {
      bestKm = km;
      best = iae;
    }
  }
  if (best < 0)
    return false;
  const AtlasEntry &ae = is.rgae[best];
  out->display = ToUtf8(SzCity(best));
  out->lon = -ae.lon;
  out->lat = ae.lat;
  out->zoneIndex = ae.izn;
  out->zoneName = ae.izn >= 0 ? ToUtf8(rgszzn[ae.izn]) : "";
  return true;
}

int ZoneIndex(const std::string &zoneName) {
  for (int izn = 0; izn < iznMax; izn++)
    if (zoneName == rgszzn[izn])
      return izn;
  return -1;
}

bool ResolveZone(BirthData &bd) {
  if (!g_init || bd.zoneIndex < 0 || !FEnsureTimezoneChanges())
    return false;
  CI ci;
  ClearB((pbyte)&ci, sizeof(CI));
  ci.mon = bd.month;
  ci.day = bd.day;
  ci.yea = bd.year;
  ci.tim = bd.hour + bd.minute / 60.0 + bd.second / 3600.0;
  ci.zon = ZondefFromIzn(bd.zoneIndex);
  ci.dst = 0.0;
  if (!DisplayTimezoneChanges(bd.zoneIndex, 0, &ci))
    return false;
  if (ci.zon == zonLMT) {
    bd.lmt = true;
    bd.dst = 0.0;
    return true;
  }
  bd.lmt = false;
  bd.utcOffset = -ci.zon;
  bd.dst = ci.dst;
  return true;
}

std::vector<std::string> ChartArgs(const BirthData &bd, const Settings &s) {
  char tim[32], zon[32], dst[16], lon[32], lat[32], day[8], mon[8], yea[16];
  snprintf(mon, sizeof(mon), "%d", bd.month);
  snprintf(day, sizeof(day), "%d", bd.day);
  snprintf(yea, sizeof(yea), "%d", bd.year);
  snprintf(tim, sizeof(tim), "%d:%02d:%02d", bd.hour, bd.minute, bd.second);
  snprintf(dst, sizeof(dst), "%s", bd.lmt ? "0" : (bd.dst != 0.0 ? "1" : "0"));
  if (bd.lmt)
    snprintf(zon, sizeof(zon), "LMT");
  else
    snprintf(zon, sizeof(zon), "%.4f", -bd.utcOffset);
  snprintf(lon, sizeof(lon), "%.6f", -bd.lon);
  snprintf(lat, sizeof(lat), "%.6f", bd.lat);
  std::vector<std::string> v = {"-qb", mon, day, yea, tim, dst, zon, lon, lat};
  v.push_back("-zi");
  v.push_back(bd.name.empty() ? "Untitled" : bd.name);
  v.push_back(bd.location.empty() ? "Unknown" : bd.location);
  v.push_back("-c");
  v.push_back(std::to_string(s.houseSystem));
  v.push_back(s.sidereal ? "=s" : "_s");
  v.push_back(s.trueNode ? "=Yn" : "_Yn");
  v.push_back("-A");
  v.push_back(s.minorAspects ? "11" : "5");
  v.push_back(":I");
  v.push_back("100");
  return v;
}

std::string Report(const BirthData &bd, const Settings &s,
  const std::vector<std::string> &switches, std::string *error) {
  std::vector<std::string> args = {g_selfExe, "--cli"};
  for (auto &a : ChartArgs(bd, s)) args.push_back(a);
  for (auto &a : switches) args.push_back(a);
  std::vector<char *> argv;
  for (auto &a : args) argv.push_back((char *)a.c_str());
  argv.push_back(nullptr);

  gchar *out = nullptr, *err = nullptr;
  gint status = 0;
  GError *gerr = nullptr;
  if (!g_spawn_sync(g_dataDir.c_str(), argv.data(), nullptr,
    G_SPAWN_DEFAULT, nullptr, nullptr, &out, &err, &status, &gerr)) {
    if (error) *error = gerr ? gerr->message : "spawn failed";
    g_clear_error(&gerr);
    return "";
  }
  std::string text = ToUtf8(out);
  if (error && err && *err) *error = ToUtf8(err);
  g_free(out);
  g_free(err);
  // Strip a UTF-8 byte order mark if present.
  if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
  return text;
}

int RunCli(int argc, char **argv) {
  return astrolog_cli_main(argc, argv);
}

const std::vector<std::string> &HouseSystems() {
  static std::vector<std::string> v;
  if (v.empty())
    for (int i = 0; i < cSystem; i++)
      v.push_back(ToUtf8(szSystem[i]));
  return v;
}

std::string SignName(int sign) {
  return ToUtf8(szSignName[(sign % 12 + 12) % 12 + 1]);
}
std::string SignGlyph(int sign) { return kSignGlyph[(sign % 12 + 12) % 12]; }
int SignElement(int sign) { return (sign % 12 + 12) % 4; }
int SignMode(int sign) { return (sign % 12 + 12) % 3; }

std::string AspectName(int type) {
  return type >= 1 && type <= cAspect ? ToUtf8(szAspectName[type]) : "";
}

std::string AspectGlyph(int type) {
  switch (type) {
  case aCon: return "☌";
  case aOpp: return "☍";
  case aSqu: return "□";
  case aTri: return "△";
  case aSex: return "⚹";
  case aInc: return "⚻";
  case aSSx: return "⚺";
  case aSSq: return "∠";
  case aSes: return "⚼";
  case aQui: return "Q";
  case aBQn: return "bQ";
  default: return "?";
  }
}

double AspectAngle(int type) {
  return type >= 1 && type <= cAspect ? rAspAngle[type] : 0.0;
}

int AspectNature(int type) {
  switch (type) {
  case aCon: return 0;
  case aOpp: case aSqu: return 1;
  case aTri: case aSex: return 2;
  default: return 3;
  }
}

std::string FormatZodiac(double lon, bool seconds) {
  lon = std::fmod(lon + 360.0, 360.0);
  int sign = (int)(lon / 30.0);
  double d = lon - sign * 30.0;
  int deg = (int)d;
  double m = (d - deg) * 60.0;
  int min = (int)m;
  // Truncate rather than round, matching Astrolog's own listings.
  int sec = (int)((m - min) * 60.0);
  char buf[64];
  if (seconds)
    snprintf(buf, sizeof(buf), "%2d°%.3s%02d'%02d\"", deg,
      SignName(sign).c_str(), min, sec);
  else
    snprintf(buf, sizeof(buf), "%2d°%.3s%02d'", deg, SignName(sign).c_str(),
      min);
  return buf;
}

std::string FormatDegree(double v, char pos, char neg) {
  char h = v < 0 ? neg : pos;
  v = std::fabs(v);
  int d = (int)v;
  int m = (int)std::lround((v - d) * 60.0);
  if (m == 60) { m = 0; d++; }
  char buf[32];
  snprintf(buf, sizeof(buf), "%d°%02d'%c", d, m, h);
  return buf;
}

std::string FormatOffset(double hours) {
  char buf[32];
  int total = (int)std::lround(std::fabs(hours) * 60.0);
  snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", hours < 0 ? '-' : '+',
    total / 60, total % 60);
  return buf;
}

}  // namespace astro
