// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/services/Queries.h"
// Force the inline copy/reset paths into compiler stack-usage evidence.
void copyQuerySnapshot(const booksync::Queries& source, booksync::QueryResult& out) { source.copyResult(out); }
