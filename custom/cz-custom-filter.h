#pragma once

#include <glib.h>
#include "bz-flathub-category.h"

G_BEGIN_DECLS

gboolean   cz_category_get_show_in_list (BzFlathubCategory *category);
const char *cz_category_get_short_name   (BzFlathubCategory *category);
GHashTable *cz_category_build_id_set     (BzFlathubCategory *category);
gboolean   cz_category_is_appstream      (BzFlathubCategory *category);
gboolean   cz_category_app_filter        (gpointer item, gpointer user_data);

G_END_DECLS
