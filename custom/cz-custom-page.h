/* cz-custom-page.h
 *
 * Copyright 2025
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

#include "cz-custom-label-store.h"

G_BEGIN_DECLS

#define CZ_TYPE_CUSTOM_PAGE (cz_custom_page_get_type ())
G_DECLARE_FINAL_TYPE (CzCustomPage, cz_custom_page, CZ, CUSTOM_PAGE, AdwBin)

CzCustomPage *
cz_custom_page_new (void);

CzCustomLabelStore *
cz_custom_page_get_label_store (CzCustomPage *self);

void
cz_custom_page_rebuild_noncore_pills (CzCustomPage *self);

G_END_DECLS