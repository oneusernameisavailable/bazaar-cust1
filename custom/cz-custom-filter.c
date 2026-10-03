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
  {           "addons",      N_ ("Addons"),           TRUE },
  {   "uncategorized",  N_ ("Uncategorized"),           TRUE },
  {          "popular",    N_ ("Popular"),          FALSE },
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

/* ---- Primary-category assignment ---- */

struct _CzCategoryPrimary
{
  guint              ref_count;

  BzFlathubCategory **items;         /* owned refs, table order */
  guint              n_items;

  GHashTable        **full_sets;     /* owned; app-id -> app-id */
  gboolean           *is_metadata;   /* per-tab: appstream-metadata driven */
  gboolean            memo_valid;
};

CzCategoryPrimary *
cz_category_primary_new (GListModel *categories)
{
  CzCategoryPrimary *self = NULL;
  GPtrArray         *items = NULL;
  guint              n_items;

  if (categories == NULL)
    return NULL;

  items = g_ptr_array_new_with_free_func (g_object_unref);

  /* Pick the visible tabs in cz_category_info[] order so the tie-break
   * below is deterministic regardless of item order in the model. */
  for (guint t = 0; cz_category_info[t].name != NULL; t++)
    {
      const CzCategoryInfo *info = &cz_category_info[t];
      guint n;

      if (!info->show_in_list)
        continue;

      n = g_list_model_get_n_items (categories);
      for (guint i = 0; i < n; i++)
        {
          g_autoptr (BzFlathubCategory) cat = g_list_model_get_item (categories, i);

          if (cat != NULL &&
              g_strcmp0 (bz_flathub_category_get_name (cat), info->name) == 0)
            {
              g_ptr_array_add (items, g_object_ref (cat));
              break;
            }
        }
    }

  n_items = items->len;
  if (n_items == 0)
    {
      g_ptr_array_unref (items);
      return NULL;
    }

  self = g_new0 (CzCategoryPrimary, 1);
  self->ref_count = 1;
  self->n_items = n_items;
  self->items = g_new (BzFlathubCategory *, n_items);
  self->full_sets = g_new0 (GHashTable *, n_items);
  self->is_metadata = g_new0 (gboolean, n_items);
  self->memo_valid = FALSE;

  for (guint i = 0; i < n_items; i++)
    self->items[i] = g_object_ref (g_ptr_array_index (items, i));

  g_ptr_array_unref (items);

  trace ("cz_category_primary_new: %u visible tabs", n_items);
  return self;
}

CzCategoryPrimary *
cz_category_primary_ref (CzCategoryPrimary *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  self->ref_count++;
  return self;
}

void
cz_category_primary_unref (CzCategoryPrimary *self)
{
  if (self == NULL || --self->ref_count > 0)
    return;

  for (guint i = 0; i < self->n_items; i++)
    {
      g_object_unref (self->items[i]);
      g_clear_pointer (&self->full_sets[i], g_hash_table_unref);
    }

  g_free (self->items);
  g_free (self->full_sets);
  g_free (self->is_metadata);
  g_free (self);
}

void
cz_category_primary_invalidate (CzCategoryPrimary *self)
{
  g_return_if_fail (self != NULL);

  if (!self->memo_valid)
    return;

  for (guint i = 0; i < self->n_items; i++)
    g_clear_pointer (&self->full_sets[i], g_hash_table_unref);

  self->memo_valid = FALSE;
  trace ("cz_category_primary_invalidate: memo dropped");
}

static void
ensure_memo (CzCategoryPrimary *self)
{
  if (self->memo_valid)
    return;

  for (guint i = 0; i < self->n_items; i++)
    {
      self->full_sets[i] = cz_category_build_id_set (self->items[i]);
      self->is_metadata[i] = cz_category_is_appstream (self->items[i]);
      trace ("cz_category_primary: memo[%u] %s size=%u is_metadata=%d",
             i, bz_flathub_category_get_name (self->items[i]),
             g_hash_table_size (self->full_sets[i]),
             self->is_metadata[i]);
    }

  self->memo_valid = TRUE;
}

/* Rule A: among the candidate tabs for an app, one holding appstream
 * metadata wins; otherwise the smallest tab (rarity) wins.  Ties break to
 * the earliest cz_category_info[] entry (deterministic). */
static guint
resolve_primary (CzCategoryPrimary *self,
                 const char        *app_id)
{
  guint best_meta_idx = G_MAXUINT;
  guint best_meta_size = G_MAXUINT;
  guint best_any_idx = G_MAXUINT;
  guint best_any_size = G_MAXUINT;

  for (guint i = 0; i < self->n_items; i++)
    {
      guint size;

      if (!g_hash_table_contains (self->full_sets[i], app_id))
        continue;

      size = g_hash_table_size (self->full_sets[i]);

      if (size < best_any_size ||
          (size == best_any_size && i < best_any_idx))
        {
          best_any_idx = i;
          best_any_size = size;
        }

      if (self->is_metadata[i] &&
          (size < best_meta_size ||
           (size == best_meta_size && i < best_meta_idx)))
        {
          best_meta_idx = i;
          best_meta_size = size;
        }
    }

  if (best_meta_idx != G_MAXUINT)
    {
      trace ("cz_category_primary: resolve %s -> metadata tab %s (size=%u)",
             app_id, bz_flathub_category_get_name (self->items[best_meta_idx]),
             best_meta_size);
      return best_meta_idx;
    }

  trace ("cz_category_primary: resolve %s -> tab %s (size=%u)",
         app_id,
         best_any_idx != G_MAXUINT
             ? bz_flathub_category_get_name (self->items[best_any_idx])
             : "(none)",
         best_any_size);
  return best_any_idx;
}

GHashTable *
cz_category_primary_build_id_set (CzCategoryPrimary *self,
                                  BzFlathubCategory  *category)
{
  guint      target = G_MAXUINT;
  GHashTable *result = NULL;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (category), NULL);

  for (guint i = 0; i < self->n_items; i++)
    if (self->items[i] == category ||
        g_strcmp0 (bz_flathub_category_get_name (self->items[i]),
                   bz_flathub_category_get_name (category)) == 0)
      {
        target = i;
        break;
      }

  /* Not a visible tab (e.g. hidden collections): keep original behaviour. */
  if (target == G_MAXUINT)
    return cz_category_build_id_set (category);

  ensure_memo (self);

  result = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  {
    GHashTableIter iter;
    gpointer       key;

    g_hash_table_iter_init (&iter, self->full_sets[target]);
    while (g_hash_table_iter_next (&iter, &key, NULL))
      {
        guint chosen_idx;

        chosen_idx = resolve_primary (self, (const char *) key);
        if (chosen_idx == target)
          g_hash_table_add (result, g_strdup ((const char *) key));
      }
  }

  trace ("cz_category_primary_build_id_set: %s full=%u deduped=%u",
         bz_flathub_category_get_name (category),
         g_hash_table_size (self->full_sets[target]),
         g_hash_table_size (result));
  return result;
}

const char *
cz_category_primary_resolve_tab (CzCategoryPrimary *self,
                                 const char        *app_id)
{
  guint chosen_idx;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (app_id != NULL, NULL);

  ensure_memo (self);

  chosen_idx = resolve_primary (self, app_id);
  if (chosen_idx == G_MAXUINT)
    return NULL;

  trace ("cz_category_primary_resolve_tab: %s -> %s", app_id,
         bz_flathub_category_get_name (self->items[chosen_idx]));
  return bz_flathub_category_get_name (self->items[chosen_idx]);
}

/* Apps with no appstream category and no collection membership anywhere. */
char **
cz_category_build_uncategorized (GListModel *categories,
                                 GListModel *all_groups)
{
  GHashTable *claimed = NULL;
  GPtrArray  *result  = NULL;
  guint       n_cats;
  guint       n_all;

  g_return_val_if_fail (categories != NULL, NULL);
  g_return_val_if_fail (all_groups != NULL, NULL);

  claimed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* Everything any collection claims (Trending, Mobile, Adwaita, Popular,
   * Recently Added, Updated, game-only, emulators, launchers, game-tools,
   * KDE, ...) is already categorized; keep those out. */
  n_cats = g_list_model_get_n_items (categories);
  for (guint i = 0; i < n_cats; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (categories, i);
      GListModel         *apps = NULL;
      guint               n_apps;

      if (item == NULL || !BZ_IS_FLATHUB_CATEGORY (item))
        continue;

      g_object_get (item, "applications", &apps, NULL);
      if (apps == NULL)
        continue;

      n_apps = g_list_model_get_n_items (apps);
      for (guint j = 0; j < n_apps; j++)
        {
          g_autoptr (GObject) app = g_list_model_get_item (apps, j);
          const char        *id  = NULL;

          if (app == NULL)
            continue;

          if (BZ_IS_ENTRY_GROUP (app))
            id = bz_entry_group_get_id (BZ_ENTRY_GROUP (app));
          else if (GTK_IS_STRING_OBJECT (app))
            id = gtk_string_object_get_string (GTK_STRING_OBJECT (app));
          else
            continue;

          if (id != NULL && !g_hash_table_contains (claimed, id))
            g_hash_table_add (claimed, g_strdup (id));
        }
      g_object_unref (apps);
    }

  result = g_ptr_array_new_with_free_func (g_free);

  n_all = g_list_model_get_n_items (all_groups);
  for (guint i = 0; i < n_all; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (all_groups, i);
      BzCategoryFlags     flags;
      const char         *id;

      if (item == NULL || !BZ_IS_ENTRY_GROUP (item))
        continue;

      /* Appstream metadata present => categorized somewhere already. */
      flags = bz_entry_group_get_categories (BZ_ENTRY_GROUP (item));
      if (flags != 0)
        continue;

      /* Addons/extensions are reachable through their parent app's page and
       * never appear in appstream category collections; keep them out of the
       * catch-all tab (clicking one would open the addons popup instead of an
       * app page). */
      if (bz_entry_group_is_addon (BZ_ENTRY_GROUP (item)))
        continue;

      id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
      if (id == NULL || g_hash_table_contains (claimed, id))
        continue;

      g_ptr_array_add (result, g_strdup (id));
    }

  g_ptr_array_add (result, NULL);

  g_hash_table_unref (claimed);

  trace ("cz_category_build_uncategorized: %u unclaimed zero-category apps",
         result->len - 1);
  return (char **) g_ptr_array_free (result, FALSE);
}

/* Addon/extension groups that are not EOL, for the dedicated "Addons" tab.
 * EOL addons are frozen/junk and hidden everywhere else on this page. */
char **
cz_category_build_addons (GListModel *all_groups)
{
  GPtrArray *result = NULL;
  guint      n_all;

  g_return_val_if_fail (all_groups != NULL, NULL);

  result = g_ptr_array_new_with_free_func (g_free);

  n_all = g_list_model_get_n_items (all_groups);
  for (guint i = 0; i < n_all; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (all_groups, i);
      const char         *eol;
      const char         *id;

      if (item == NULL || !BZ_IS_ENTRY_GROUP (item))
        continue;

      if (!bz_entry_group_is_addon (BZ_ENTRY_GROUP (item)))
        continue;

      eol = bz_entry_group_get_eol (BZ_ENTRY_GROUP (item));
      if (eol != NULL)
        continue;

      id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
      if (id != NULL)
        g_ptr_array_add (result, g_strdup (id));
    }

  g_ptr_array_add (result, NULL);

  trace ("cz_category_build_addons: %u non-EOL addons", result->len - 1);
  return (char **) g_ptr_array_free (result, FALSE);
}
