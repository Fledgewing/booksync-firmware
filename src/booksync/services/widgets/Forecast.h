// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StatsProjection.h"
#include "booksync/services/ReadingForecast.h"
namespace booksync {
void projectForecast(const ReadingForecast&, const StatsProjection&, StatsCard&, StatsForecast&);
}
