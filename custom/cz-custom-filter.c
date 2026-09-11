#include "config.h"

#include "cz-custom-filter.h"

#include <glib/gi18n.h>

#include "bz-entry-group.h"
#include "bz-entry.h"
#include "bz-application.h"

static void
trace (const char *fmt, ...) __attribute__((format (printf, 1, 2)));

static void
trace (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, fmt, ap);
  va_end (ap);
}

typedef struct
{
  const char *name;
  const char *short_name;
  gboolean    show_in_list;
} CzCategoryInfo;

static const CzCategoryInfo cz_category_info[] = {
  {       "audiovideo",       N_ ("Media"),           TRUE },
  {      "development",     N_ ("Develop"),           TRUE },
  {        "education",       N_ ("Learn"),           TRUE },
  {             "game",        N_ ("Play"),           TRUE },
  {         "graphics",      N_ ("Create"),           TRUE },
  {          "network",    N_ ("Internet"),           TRUE },
  {           "office",        N_ ("Work"),           TRUE },
  {          "science",     N_ ("Science"),           TRUE },
  {           "system",      N_ ("System"),           TRUE },
  {          "utility",       N_ ("Tools"),           TRUE },
  {         "trending",    N_ ("Trending"),           TRUE },
  {           "mobile",      N_ ("Mobile"),           TRUE },
  {          "adwaita",     N_ ("Adwaita"),           TRUE },
  {              "kde",    N_ ("KDE Apps"),           TRUE },
  {          "popular",     N_ ("Popular"),          FALSE },
  {   "recently-added",         N_ ("New"),          FALSE },
  { "recently-updated",     N_ ("Updated"),          FALSE },
  {        "game-only",       N_ ("Games"),          FALSE },
  {        "emulators",   N_ ("Emulators"),          FALSE },
  {        "launchers",   N_ ("Launchers"),          FALSE },
  {       "game-tools",  N_ ("Game Tools"),          FALSE },
  {               NULL,                  NULL,         FALSE }
};

static const CzCategoryInfo *
lookup (const char *name)
{
  if (name == NULL)
    return NULL;

  for (guint i = 0; cz_category_info[i].name != NULL; i++)
    {
      if (g_strcmp0 (cz_category_info[i].name, name) == 0)
        return &cz_category_info[i];
    }

  return NULL;
}

gboolean
cz_category_get_show_in_list (BzFlathubCategory *category)
{
  const CzCategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (category), FALSE);

  info = lookup (bz_flathub_category_get_name (category));
  return info ? info->show_in_list : FALSE;
}

const char *
cz_category_get_short_name (BzFlathubCategory *category)
{
  const CzCategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (category), NULL);

  info = lookup (bz_flathub_category_get_name (category));
  return info ? _ (info->short_name) : bz_flathub_category_get_name (category);
}

GHashTable *
cz_category_build_id_set (BzFlathubCategory *category)
{
  BzStateInfo   *state    = NULL;
  GListModel    *all      = NULL;
  GHashTable    *set      = NULL;
  const char    *cat_name = NULL;
  guint          n;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (category), NULL);

  cat_name = bz_flathub_category_get_name (category);

  state = bz_state_info_get_default ();
  if (state == NULL)
    return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  all = bz_state_info_get_all_entry_groups (state);
  if (all == NULL)
    return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  n = g_list_model_get_n_items (all);
  for (guint i = 0; i < n; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (all, i);
      const char *id;

      if (item == NULL || !BZ_IS_ENTRY_GROUP (item))
        continue;

      if (!bz_entry_group_has_category (BZ_ENTRY_GROUP (item), cat_name))
        continue;

      id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
      if (id != NULL)
        g_hash_table_add (set, g_strdup (id));
    }

  /* Fallback for special Flathub collections (trending, mobile, adwaita, kde)
   * that don't appear in appstream metadata — read from category's API fetch */
  if (g_hash_table_size (set) == 0)
    {
      GListModel *raw_apps = NULL;

      g_object_get (category, "applications", &raw_apps, NULL);
      if (raw_apps != NULL)
        {
          guint n_raw = g_list_model_get_n_items (raw_apps);

          for (guint i = 0; i < n_raw; i++)
            {
              g_autoptr (GObject) item = g_list_model_get_item (raw_apps, i);
              const char *id;

              if (item == NULL)
                continue;

              if (BZ_IS_ENTRY_GROUP (item))
                id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
              else if (GTK_IS_STRING_OBJECT (item))
                id = gtk_string_object_get_string (GTK_STRING_OBJECT (item));
              else
                continue;

              if (id != NULL)
                g_hash_table_add (set, g_strdup (id));
            }
          g_object_unref (raw_apps);
        }
    }

  trace ("cz_category_build_id_set: category=%s, matched=%u",
         cat_name, g_hash_table_size (set));
  return set;
}

gboolean
cz_category_is_appstream (BzFlathubCategory *category)
{
  BzStateInfo *state = NULL;
  GListModel  *all   = NULL;
  const char  *cat_name;
  guint        n;
  gboolean     found = FALSE;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (category), FALSE);

  cat_name = bz_flathub_category_get_name (category);

  state = bz_state_info_get_default ();
  if (state == NULL)
    return FALSE;

  all = bz_state_info_get_all_entry_groups (state);
  if (all == NULL)
    return FALSE;

  n = g_list_model_get_n_items (all);
  for (guint i = 0; i < n && !found; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (all, i);

      if (item != NULL && BZ_IS_ENTRY_GROUP (item))
        found = bz_entry_group_has_category (BZ_ENTRY_GROUP (item), cat_name);
    }

  trace ("cz_category_is_appstream: category=%s, found=%d", cat_name, found);
  return found;
}

gboolean
cz_category_app_filter (gpointer item, gpointer user_data)
{
  GHashTable *allowed_ids = user_data;
  const char *id = NULL;

  if (!BZ_IS_ENTRY_GROUP (item))
    return FALSE;

  id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
  if (id == NULL)
    return FALSE;

  return g_hash_table_contains (allowed_ids, id);
}
