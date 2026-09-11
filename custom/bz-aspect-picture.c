/* bz-aspect-picture.c
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

#include "config.h"

#include "bz-aspect-picture.h"

#include <math.h>

struct _BzAspectPicture
{
  GtkWidget parent_instance;

  GdkPaintable *paintable;
  double        ratio;
};

G_DEFINE_FINAL_TYPE (BzAspectPicture, bz_aspect_picture, GTK_TYPE_WIDGET)

enum
{
  PROP_0,
  PROP_PAINTABLE,
  PROP_RATIO,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void on_invalidate_contents (BzAspectPicture *self,
                                    GdkPaintable    *paintable);
static void on_invalidate_size (BzAspectPicture *self,
                                GdkPaintable    *paintable);

static void
bz_aspect_picture_dispose (GObject *object)
{
  BzAspectPicture *self = BZ_ASPECT_PICTURE (object);

  if (self->paintable != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->paintable, on_invalidate_contents, self);
      g_signal_handlers_disconnect_by_func (self->paintable, on_invalidate_size, self);
    }
  g_clear_object (&self->paintable);

  G_OBJECT_CLASS (bz_aspect_picture_parent_class)->dispose (object);
}

static void
bz_aspect_picture_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BzAspectPicture *self = BZ_ASPECT_PICTURE (object);

  switch (prop_id)
    {
    case PROP_PAINTABLE:
      g_value_set_object (value, self->paintable);
      break;
    case PROP_RATIO:
      g_value_set_double (value, self->ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_aspect_picture_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BzAspectPicture *self = BZ_ASPECT_PICTURE (object);

  switch (prop_id)
    {
    case PROP_PAINTABLE:
      bz_aspect_picture_set_paintable (self, g_value_get_object (value));
      break;
    case PROP_RATIO:
      bz_aspect_picture_set_ratio (self, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GtkSizeRequestMode
bz_aspect_picture_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
bz_aspect_picture_measure (GtkWidget     *widget,
                           GtkOrientation orientation,
                           int            for_size,
                           int           *minimum,
                           int           *natural,
                           int           *minimum_baseline,
                           int           *natural_baseline)
{
  BzAspectPicture *self = BZ_ASPECT_PICTURE (widget);

  *minimum          = 0;
  *natural          = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *minimum = 1;
      *natural = 160;
    }
  else
    {
      if (self->ratio == 0.0)
        {
          int base = (for_size > 0) ? for_size : 160;
          int w, h = 0;

          if (self->paintable != NULL)
            {
              w = gdk_paintable_get_intrinsic_width (self->paintable);
              h = gdk_paintable_get_intrinsic_height (self->paintable);

              if (w > 0 && h > 0)
                {
                  *minimum = (int) round (((double) base * h) / w);
                  *natural = *minimum;
                }
              else
                {
                  *minimum = base;
                  *natural = base;
                }
            }
          else
            {
              *minimum = base;
              *natural = base;
            }
        }
      else
        {
          int base = 0;
          int h    = 0;

          base     = (for_size > 0) ? for_size : 160;
          h        = (int) round ((double) base / self->ratio);
          *minimum = h;
          *natural = h;
        }
    }
}

static void
bz_aspect_picture_snapshot (GtkWidget   *widget,
                            GtkSnapshot *snapshot)
{
  BzAspectPicture *self             = BZ_ASPECT_PICTURE (widget);
  int              widget_width     = 0;
  int              widget_height    = 0;
  double           paintable_width  = 0.0;
  double           paintable_height = 0.0;
  double           scale_x          = 0.0;
  double           scale_y          = 0.0;
  double           scale            = 0.0;
  double           draw_width       = 0.0;
  double           draw_height      = 0.0;
  double           x                = 0.0;
  double           y                = 0.0;
  GskRoundedRect   clip;

  if (self->paintable == NULL)
    return;

  widget_width  = gtk_widget_get_width (widget);
  widget_height = gtk_widget_get_height (widget);

  if (widget_width <= 0 || widget_height <= 0)
    return;

  paintable_width  = gdk_paintable_get_intrinsic_width (self->paintable);
  paintable_height = gdk_paintable_get_intrinsic_height (self->paintable);

  if (paintable_width <= 0 || paintable_height <= 0)
    {
      paintable_width  = widget_width;
      paintable_height = widget_height;
    }

  scale_x = (double) widget_width / paintable_width;
  scale_y = (double) widget_height / paintable_height;
  scale   = MAX (scale_x, scale_y);

  draw_width  = paintable_width * scale;
  draw_height = paintable_height * scale;
  x           = ((double) widget_width - draw_width) / 2.0;
  y           = ((double) widget_height - draw_height) / 2.0;

  gsk_rounded_rect_init_from_rect (&clip,
                                   &GRAPHENE_RECT_INIT (0, 0, widget_width, widget_height),
                                   0.0);
  gtk_snapshot_push_rounded_clip (snapshot, &clip);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, y));
  gdk_paintable_snapshot (self->paintable, snapshot, draw_width, draw_height);
  gtk_snapshot_pop (snapshot);
}

static void
bz_aspect_picture_class_init (BzAspectPictureClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_aspect_picture_dispose;
  object_class->get_property = bz_aspect_picture_get_property;
  object_class->set_property = bz_aspect_picture_set_property;

  widget_class->get_request_mode = bz_aspect_picture_get_request_mode;
  widget_class->measure          = bz_aspect_picture_measure;
  widget_class->snapshot         = bz_aspect_picture_snapshot;

  gtk_widget_class_set_css_name (widget_class, "aspectpicture");

  props[PROP_PAINTABLE] =
      g_param_spec_object ("paintable",
                           NULL, NULL,
                           GDK_TYPE_PAINTABLE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_RATIO] =
      g_param_spec_double ("ratio",
                           NULL, NULL,
                           0.000, G_MAXDOUBLE, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_aspect_picture_init (BzAspectPicture *self)
{
  self->ratio = 0;
}

GtkWidget *
bz_aspect_picture_new (void)
{
  return g_object_new (BZ_TYPE_ASPECT_PICTURE, NULL);
}

void
bz_aspect_picture_set_paintable (BzAspectPicture *self,
                                 GdkPaintable    *paintable)
{
  g_return_if_fail (BZ_IS_ASPECT_PICTURE (self));
  g_return_if_fail (paintable == NULL || GDK_IS_PAINTABLE (paintable));

  if (self->paintable == paintable)
    return;

  if (self->paintable != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->paintable, on_invalidate_contents, self);
      g_signal_handlers_disconnect_by_func (self->paintable, on_invalidate_size, self);
    }

  g_clear_object (&self->paintable);
  if (paintable != NULL)
    {
      self->paintable = g_object_ref (paintable);
      g_signal_connect_swapped (self->paintable, "invalidate-contents",
                                G_CALLBACK (on_invalidate_contents), self);
      g_signal_connect_swapped (self->paintable, "invalidate-size",
                                G_CALLBACK (on_invalidate_size), self);
    }

  gtk_widget_queue_resize (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PAINTABLE]);
}

GdkPaintable *
bz_aspect_picture_get_paintable (BzAspectPicture *self)
{
  g_return_val_if_fail (BZ_IS_ASPECT_PICTURE (self), NULL);
  return self->paintable;
}

void
bz_aspect_picture_set_ratio (BzAspectPicture *self,
                             double           ratio)
{
  g_return_if_fail (BZ_IS_ASPECT_PICTURE (self));
  g_return_if_fail (ratio >= 0.0);

  if (self->ratio == ratio)
    return;

  self->ratio = ratio;
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_RATIO]);
}

double
bz_aspect_picture_get_ratio (BzAspectPicture *self)
{
  g_return_val_if_fail (BZ_IS_ASPECT_PICTURE (self), 16.0 / 9.0);
  return self->ratio;
}

static void
on_invalidate_contents (BzAspectPicture *self,
                        GdkPaintable    *paintable)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_invalidate_size (BzAspectPicture *self,
                    GdkPaintable    *paintable)
{
  gtk_widget_queue_resize (GTK_WIDGET (self));
}
