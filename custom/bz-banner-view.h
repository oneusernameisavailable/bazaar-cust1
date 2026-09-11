/* bz-banner-view.h
 *
 * Copyright 2026 Alexander Vanhee
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

#include <adwaita.h>

#include "bz-curated-banner.h"

G_BEGIN_DECLS

#define BZ_TYPE_BANNER_VIEW (bz_banner_view_get_type ())
G_DECLARE_FINAL_TYPE (BzBannerView, bz_banner_view, BZ, BANNER_VIEW, AdwBin)

GtkWidget *
bz_banner_view_new (BzCuratedBanner *banner);

void
bz_banner_view_set_banner (BzBannerView    *self,
                           BzCuratedBanner *banner);

BzCuratedBanner *
bz_banner_view_get_banner (BzBannerView *self);

G_END_DECLS
