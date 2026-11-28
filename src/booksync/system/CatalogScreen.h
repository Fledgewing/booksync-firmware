// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>

#include "booksync/interface/Library.h"
#ifdef SIMULATOR
#include "booksync/ui/Catalog.h"
#endif
class Activity;
class GfxRenderer;
class MappedInputManager;
namespace booksync {
std::unique_ptr<Activity> createCatalogScreen(GfxRenderer&, MappedInputManager&, Library&);
#ifdef SIMULATOR
CatalogSurface* catalogSceneController();
void showCatalogScene(GfxRenderer&, MappedInputManager&, Library&);
uint32_t catalogScenePaints();
bool writeCatalogGeometry(const char* path);
#endif
}  // namespace booksync
