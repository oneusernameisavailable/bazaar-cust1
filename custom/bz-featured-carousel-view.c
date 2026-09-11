/* bz-featured-carousel-view.c
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

#include "bz-featured-carousel-view.h"

#include "bz-application.h"
#include "bz-curated-appids-info.h"
#include "bz-featured-carousel.h"

struct _BzFeaturedCarouselView
{
  AdwBin parent_instance;

  BzCuratedFeaturedCarousel *carousel;
};

G_DEFINE_FINAL_TYPE (BzFeaturedCarouselView, bz_featured_carousel_view, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_CAROUSEL,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_featured_carousel_view_dispose (GObject *object)
{
  BzFeaturedCarouselView *self = BZ_FEATURED_CAROUSEL_VIEW (object);

  g_clear_object (&self->carousel);

  G_OBJECT_CLASS (bz_featured_carousel_view_parent_class)->dispose (object);
}

static void
bz_featured_carousel_view_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  BzFeaturedCarouselView *self = BZ_FEATURED_CAROUSEL_VIEW (object);

  switch (prop_id)
    {
    case PROP_CAROUSEL:
      g_value_set_object (value, bz_featured_carousel_view_get_carousel (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_featured_carousel_view_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  BzFeaturedCarouselView *self = BZ_FEATURED_CAROUSEL_VIEW (object);

  switch (prop_id)
    {
    case PROP_CAROUSEL:
      bz_featured_carousel_view_set_carousel (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_featured_carousel_view_class_init (BzFeaturedCarouselViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose      = bz_featured_carousel_view_dispose;
  object_class->get_property = bz_featured_carousel_view_get_property;
  object_class->set_property = bz_featured_carousel_view_set_property;

  props[PROP_CAROUSEL] =
      g_param_spec_object (
          "carousel",
          NULL, NULL,
          BZ_TYPE_CURATED_FEATURED_CAROUSEL,
          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_FEATURED_CAROUSEL);
}

static void
bz_featured_carousel_view_init (BzFeaturedCarouselView *self)
{
}

GtkWidget *
bz_featured_carousel_view_new (BzCuratedFeaturedCarousel *carousel)
{
  return g_object_new (
      BZ_TYPE_FEATURED_CAROUSEL_VIEW,
      "carousel", carousel,
      NULL);
}

void
bz_featured_carousel_view_set_carousel (BzFeaturedCarouselView    *self,
                                        BzCuratedFeaturedCarousel *carousel)
{
  BzCuratedAppidsInfo *appids = NULL;
  GListModel          *list   = NULL;

  g_return_if_fail (BZ_IS_FEATURED_CAROUSEL_VIEW (self));
  g_return_if_fail (carousel == NULL || BZ_IS_CURATED_FEATURED_CAROUSEL (carousel));

  g_clear_object (&self->carousel);
  adw_bin_set_child (ADW_BIN (self), NULL);

  if (carousel != NULL)
    {
      self->carousel = g_object_ref (carousel);

      appids = bz_curated_featured_carousel_get_appids (carousel);
      if (appids != NULL)
        list = bz_curated_appids_info_get_list (appids);

      if (list != NULL)
        {
          BzStateInfo             *info           = NULL;
          BzApplicationMapFactory *factory        = NULL;
          g_autoptr (GListModel) groups           = NULL;
          BzFeaturedCarousel *carousel_w          = NULL;
          gboolean            disable_auto_rotate = FALSE;

          info    = bz_state_info_get_default ();
          factory = bz_state_info_get_application_factory (info);
          groups  = bz_application_map_factory_generate (factory, list);

          g_object_get (carousel, "disable-auto-rotate", &disable_auto_rotate, NULL);

          carousel_w = bz_featured_carousel_new ();
          bz_featured_carousel_set_model (carousel_w, groups);
          g_object_set (carousel_w, "auto-rotate", !disable_auto_rotate, NULL);
          gtk_widget_set_margin_top (GTK_WIDGET (carousel_w), 20);
          gtk_widget_set_margin_bottom (GTK_WIDGET (carousel_w), 12);

          adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (carousel_w));
        }
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CAROUSEL]);
}

BzCuratedFeaturedCarousel *
bz_featured_carousel_view_get_carousel (BzFeaturedCarouselView *self)
{
  g_return_val_if_fail (BZ_IS_FEATURED_CAROUSEL_VIEW (self), NULL);
  return self->carousel;
}
