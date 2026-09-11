/* bz-group-tile-css-watcher.h
 *
 * Copyright 2025 Adam Masciola
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "bz-entry-group.h"

G_BEGIN_DECLS

#define BZ_TYPE_GROUP_TILE_CSS_WATCHER (bz_group_tile_css_watcher_get_type ())
G_DECLARE_FINAL_TYPE (BzGroupTileCssWatcher, bz_group_tile_css_watcher, BZ, GROUP_TILE_CSS_WATCHER, GObject)

BzGroupTileCssWatcher *
bz_group_tile_css_watcher_new (void);

GtkWidget *
bz_group_tile_css_watcher_dup_widget (BzGroupTileCssWatcher *self);

BzEntryGroup *
bz_group_tile_css_watcher_get_group (BzGroupTileCssWatcher *self);

void
bz_group_tile_css_watcher_set_widget (BzGroupTileCssWatcher *self,
                                      GtkWidget             *widget);

void
bz_group_tile_css_watcher_set_group (BzGroupTileCssWatcher *self,
                                     BzEntryGroup          *group);

G_END_DECLS

/* End of bz-group-tile-css-watcher.h */
