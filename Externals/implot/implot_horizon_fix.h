// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// newlib has no timegm, and Horizon has no timezone database to use.
// This is force-included into implot, which is the only usage of this.

#pragma once

#include <ctime>

// Howard Hinnant's days_from_civil. y/m/d are Gregorian, m in [1, 12], d in [1, 31].
inline long long ImPlotHorizonDaysFromCivil(long long y, unsigned m, unsigned d)
{
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

inline std::time_t timegm(std::tm* tm)
{
  const long long days =
      ImPlotHorizonDaysFromCivil(tm->tm_year + 1900LL, static_cast<unsigned>(tm->tm_mon + 1),
                                 static_cast<unsigned>(tm->tm_mday));
  return static_cast<std::time_t>(((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec);
}
