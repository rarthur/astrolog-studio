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
// engine.h: a small, clean C++ interface over the Astrolog calculation core.
// The Astrolog headers define many short global macros (planet, ret, MM,
// space...), so they are only ever included from engine.cpp. The rest of the
// application talks to the core exclusively through this header.

#pragma once

#include <string>
#include <vector>

namespace astro {

// Birth data as entered by the user. Longitudes are degrees east positive and
// time zone offsets are hours east of UTC (the civil convention, e.g. -5 for
// New York), even though Astrolog internally uses west positive values.
struct BirthData {
  std::string name = "Untitled";
  std::string location;
  std::string zoneName;  // IANA zone name, if the location came from atlas.
  int zoneIndex = -1;    // Astrolog time zone area index, or -1.
  int year = 2000, month = 1, day = 1;
  int hour = 12, minute = 0, second = 0;
  double utcOffset = 0.0;  // Standard time offset, hours east of UTC.
  double dst = 0.0;        // Daylight saving offset in hours (0 or 1).
  bool lmt = false;        // Use Local Mean Time instead of a zone offset.
  double lon = 0.0;        // Degrees, east positive.
  double lat = 0.0;        // Degrees, north positive.
};

struct Settings {
  int houseSystem = 0;         // Index into HouseSystems().
  bool sidereal = false;
  bool minorAspects = false;   // Include semisextile, quincunx, etc.
  bool asteroids = true;       // Chiron, Ceres, Pallas, Juno, Vesta.
  bool points = true;          // Lilith, Part of Fortune, Vertex.
  bool trueNode = false;
  double orbScale = 1.0;
};

enum BodyKind { kLuminary, kPlanet, kAsteroid, kNode, kPoint, kAngle };

struct Body {
  int id = 0;            // Astrolog object index.
  std::string name;
  std::string glyph;
  BodyKind kind = kPlanet;
  double lon = 0.0;      // Ecliptic longitude, 0..360.
  double lat = 0.0;      // Ecliptic latitude (or declination).
  double speed = 0.0;    // Degrees per day.
  int house = 0;         // 1..12
  bool retro = false;
};

struct Aspect {
  int a = 0, b = 0;      // Indices into Chart::bodies (or transit bodies).
  int type = 0;          // Astrolog aspect index (1 = conjunction...).
  double orb = 0.0;      // Absolute orb in degrees.
  double maxOrb = 0.0;
  bool applying = false;
};

struct Chart {
  bool ok = false;
  std::string error;
  BirthData data;
  Settings settings;
  std::vector<Body> bodies;
  double cusps[13] = {0};  // cusps[1..12]
  double asc = 0.0, mc = 0.0;
  std::vector<Aspect> aspects;
  double julianDay = 0.0;
  std::string houseSystem;
  std::string zodiac;      // "Tropical" or "Sidereal (Fagan-Bradley)".
};

struct City {
  std::string display;     // "Paris, France"
  std::string zoneName;
  int zoneIndex = -1;
  double lon = 0.0, lat = 0.0;
  int score = 0;
};

// Initialize the Astrolog core. dataDir must contain astrolog.as, atlas.as,
// timezone.as and the ephem directory. argv0 is used to locate the program.
bool Init(const std::string &dataDir, std::string *error);
const std::string &DataDir();

Chart Cast(const BirthData &bd, const Settings &s);
// Aspects between two charts (e.g. transits to natal).
std::vector<Aspect> CrossAspects(const Chart &outer, const Chart &inner,
  const Settings &s);

std::vector<City> SearchCities(const std::string &query, int maxResults);
// The most prominent atlas city in an IANA time zone (e.g. the system's
// /etc/localtime zone), used to pick a sensible default location.
bool CityForZone(const std::string &zoneName, City *out);
// The atlas city nearest to a point, if one lies within maxKm.
bool NearestCity(double lat, double lon, double maxKm, City *out);
// Astrolog's index for an IANA time zone name, or -1 if unknown.
int ZoneIndex(const std::string &zoneName);
// Given zoneIndex and the local date/time in bd, determine the standard
// offset and daylight saving in effect then (from Astrolog's time zone
// history database). Returns false if unavailable.
bool ResolveZone(BirthData &bd);

// Run an Astrolog text report (given as command switches such as "-I") for
// the chart. This spawns this same executable in CLI mode so the report
// cannot disturb the state of the interactive chart.
std::string Report(const BirthData &bd, const Settings &s,
  const std::vector<std::string> &switches, std::string *error);
// Build the argument list that reproduces this chart on the command line.
std::vector<std::string> ChartArgs(const BirthData &bd, const Settings &s);

// Run the classic Astrolog command line interface.
int RunCli(int argc, char **argv);

// Names and glyphs.
const std::vector<std::string> &HouseSystems();
std::string SignName(int sign);        // 0..11
std::string SignGlyph(int sign);
int SignElement(int sign);             // 0 fire, 1 earth, 2 air, 3 water
int SignMode(int sign);                // 0 cardinal, 1 fixed, 2 mutable
std::string AspectName(int type);
std::string AspectGlyph(int type);
double AspectAngle(int type);
int AspectNature(int type);            // 0 neutral, 1 hard, 2 soft, 3 minor

// Formatting helpers.
std::string FormatZodiac(double lon, bool seconds = false);  // 12°Can09'
std::string FormatDegree(double deg, char pos, char neg);     // 40°43'N
std::string FormatOffset(double hours);                      // UTC-05:00

}  // namespace astro
