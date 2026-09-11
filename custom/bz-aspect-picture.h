/* bz-aspect-picture.h
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BZ_TYPE_ASPECT_PICTURE (bz_aspect_picture_get_type ())

G_DECLARE_FINAL_TYPE (BzAspectPicture, bz_aspect_picture, BZ, ASPECT_PICTURE, GtkWidget)

GtkWidget *
bz_aspect_picture_new (void);

void
bz_aspect_picture_set_paintable (BzAspectPicture *self,
                                 GdkPaintable    *paintable);
GdkPaintable *
bz_aspect_picture_get_paintable (BzAspectPicture *self);

void
bz_aspect_picture_set_ratio (BzAspectPicture *self,
                             double           ratio);
double
bz_aspect_picture_get_ratio (BzAspectPicture *self);

G_END_DECLS
