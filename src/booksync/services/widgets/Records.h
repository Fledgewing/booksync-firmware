// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StatsProjection.h"
namespace booksync {
void projectRecords(const StatsSnapshot&, const StatsProjection&, StatsCard&, StatsRecords&);
}
