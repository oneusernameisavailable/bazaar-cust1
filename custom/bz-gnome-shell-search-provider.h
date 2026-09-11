/* bz-gnome-shell-search-provider.h
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

#include "bz-search-engine.h"

G_BEGIN_DECLS

#define BZ_TYPE_GNOME_SHELL_SEARCH_PROVIDER (bz_gnome_shell_search_provider_get_type ())
G_DECLARE_FINAL_TYPE (BzGnomeShellSearchProvider, bz_gnome_shell_search_provider, BZ, GNOME_SHELL_SEARCH_PROVIDER, GObject)

BzGnomeShellSearchProvider *
bz_gnome_shell_search_provider_new (void);

BzSearchEngine *
bz_gnome_shell_search_provider_get_engine (BzGnomeShellSearchProvider *self);

void
bz_gnome_shell_search_provider_set_engine (BzGnomeShellSearchProvider *self,
                                           BzSearchEngine             *engine);

GDBusConnection *
bz_gnome_shell_search_provider_get_connection (BzGnomeShellSearchProvider *self);

gboolean
bz_gnome_shell_search_provider_set_connection (BzGnomeShellSearchProvider *self,
                                               GDBusConnection            *connection,
                                               GError                    **error);

G_END_DECLS

/* End of bz-gnome-shell-search-provider.h */
