/* cz-app-report.c
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

#include "config.h"

#include "cz-app-report.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

#include "bz-entry-group.h"
#include "bz-flathub-category.h"
#include "bz-flathub-state.h"
#include "cz-custom-filter.h"

typedef struct
{
  char       *display_name;
  GHashTable *id_set;
} CatInfo;

static void
cat_info_free (CatInfo *info)
{
  if (info == NULL)
    return;
  g_free (info->display_name);
  if (info->id_set != NULL)
    g_hash_table_unref (info->id_set);
  g_slice_free (CatInfo, info);
}

/* Write one RFC-4180 CSV field (quoted when it contains a delimiter,
 * double quote or newline), followed by a field separator. */
static void
write_csv_field (GString    *out,
                 const char *field)
{
  const char *p;

  if (field == NULL)
    field = "";

  for (p = field; *p != '\0'; p++)
    {
      if (*p == ',' || *p == '"' || *p == '\n')
        {
          g_string_append_c (out, '"');
          for (p = field; *p != '\0'; p++)
            {
              if (*p == '"')
                g_string_append_c (out, '"');
              g_string_append_c (out, *p);
            }
          g_string_append_c (out, '"');
          return;
        }
    }

  g_string_append (out, field);
}

/* Append a cell to the row: the field value plus (unless it is the final
 * column) a comma separator, so empty cells keep their position. */
static void
append_csv_field (GString    *line,
                  const char *field,
                  gboolean    last)
{
  write_csv_field (line, field);
  if (!last)
    g_string_append_c (line, ',');
}

char *
cz_app_report_suggest_path (const char *db_path)
{
  g_autofree gchar    *dir          = NULL;
  g_autofree gchar    *reports_dir  = NULL;
  g_autofree gchar    *ts           = NULL;
  g_autofree gchar    *filename     = NULL;
  g_autofree gchar    *path         = NULL;
  g_autoptr (GDateTime) now         = NULL;

  dir         = g_path_get_dirname (db_path);
  reports_dir = g_build_filename (dir, "reports", NULL);
  g_mkdir_with_parents (reports_dir, 0700);

  now      = g_date_time_new_now_local ();
  ts       = g_date_time_format (now, "%Y%m%d-%H%M%S");
  filename = g_strdup_printf ("bazaar-app-report-%s.csv", ts);
  path     = g_build_filename (reports_dir, filename, NULL);

  return g_steal_pointer (&path);
}

static gint
compare_mtime_desc (gconstpointer a,
                    gconstpointer b)
{
  const char *path_a = *(const char *const *) a;
  const char *path_b = *(const char *const *) b;
  GStatBuf    sa;
  GStatBuf    sb;

  g_stat (path_a, &sa);
  g_stat (path_b, &sb);

  if (sb.st_mtime > sa.st_mtime)
    return 1;
  if (sb.st_mtime < sa.st_mtime)
    return -1;
  return 0;
}

void
cz_app_report_prune (const char *reports_dir,
                     guint       max)
{
  GDir        *dir = NULL;
  GPtrArray   *files = NULL;
  const char  *name;
  guint        i;

  if (max == 0)
    max = CZ_APP_REPORT_RETENTION;

  dir = g_dir_open (reports_dir, 0, NULL);
  if (dir == NULL)
    return;

  files = g_ptr_array_new_with_free_func (g_free);

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      if (!g_str_has_suffix (name, ".csv"))
        continue;
      g_ptr_array_add (files, g_build_filename (reports_dir, name, NULL));
    }
  g_dir_close (dir);

  if (files->len <= max)
    {
      g_ptr_array_unref (files);
      return;
    }

  g_ptr_array_sort (files, compare_mtime_desc);

  for (i = max; i < files->len; i++)
    {
      const char *path = g_ptr_array_index (files, i);
      g_remove (path);
    }

  g_ptr_array_unref (files);
}

static gboolean
write_row (GOutputStream  *out,
           GString        *line,
           const char     *app_name,
           const char     *app_id,
           const char     *category_cell,
           const char     *core_label,
           const char     *noncore_cell,
           const char     *aesthetics,
           const char     *usability,
           const char     *features,
           const char     *issues,
           GError        **error)
{
  g_string_truncate (line, 0);
  append_csv_field (line, app_name, FALSE);
  append_csv_field (line, app_id, FALSE);
  append_csv_field (line, category_cell, FALSE);
  append_csv_field (line, core_label, FALSE);
  append_csv_field (line, noncore_cell, FALSE);
  append_csv_field (line, aesthetics, FALSE);
  append_csv_field (line, usability, FALSE);
  append_csv_field (line, features, FALSE);
  append_csv_field (line, issues, TRUE);
  g_string_append_c (line, '\n');

  return g_output_stream_write_all (out, line->str, line->len,
                                    NULL, NULL, error);
}

gboolean
cz_app_report_generate (BzLabelStore *store,
                        BzStateInfo  *state,
                        const char   *out_path,
                        GError      **error)
{
  g_autoptr (GFileOutputStream) out    = NULL;
  g_autoptr (GPtrArray)         cats   = NULL;
  g_autoptr (GString)           line   = NULL;
  BzFlathubState               *flathub = NULL;
  GListModel                   *all     = NULL;
  GListModel                   *categories = NULL;
  guint                         n_apps;
  guint                         i;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (state != NULL, FALSE);
  g_return_val_if_fail (out_path != NULL, FALSE);

  out = g_file_replace (g_file_new_for_path (out_path), NULL, FALSE,
                        G_FILE_CREATE_REPLACE_DESTINATION, NULL, error);
  if (out == NULL)
    return FALSE;

  /* Collect the custom-tab (show-in-list) categories once. */
  cats = g_ptr_array_new_with_free_func ((GDestroyNotify) cat_info_free);
  flathub = bz_state_info_get_flathub (state);
  categories = flathub != NULL ? bz_flathub_state_get_categories (flathub) : NULL;
  if (categories != NULL)
    {
      guint n_cats = g_list_model_get_n_items (categories);
      guint c;

      for (c = 0; c < n_cats; c++)
        {
          g_autoptr (BzFlathubCategory) cat = g_list_model_get_item (categories, c);
          CatInfo *info;

          if (cat == NULL || !cz_category_get_show_in_list (cat))
            continue;

          info = g_slice_new0 (CatInfo);
          info->display_name = g_strdup (bz_flathub_category_get_display_name (cat));
          info->id_set       = cz_category_build_id_set (cat);
          g_ptr_array_add (cats, info);
        }
    }

  line = g_string_new (NULL);

  if (!write_row (G_OUTPUT_STREAM (out), line,
                  "App Name", "App ID", "Category",
                  "Core Label", "Custom Labels",
                  "Aesthetics", "Usability", "Features", "Issues",
                  error))
    return FALSE;

  all = bz_state_info_get_all_entry_groups (state);
  n_apps = all != NULL ? g_list_model_get_n_items (all) : 0;

  for (i = 0; i < n_apps; i++)
    {
      g_autoptr (BzEntryGroup) group = g_list_model_get_item (all, i);
      const char               *app_id;
      const char               *app_name;
      g_autofree char          *aes = NULL;
      g_autofree char          *usa = NULL;
      g_autofree char          *fea = NULL;
      g_autofree char          *iss = NULL;
      g_autofree char          *category_cell = NULL;
      g_autofree char          *noncore_cell = NULL;
      g_autofree char          *core_label = NULL;
      char                    **noncore = NULL;
      guint                     j;

      if (group == NULL)
        continue;

      app_id   = bz_entry_group_get_id (group);
      app_name = bz_entry_group_get_title (group);

      bz_label_store_get_app_review (store, app_id,
                                     &aes, &usa, &fea, &iss, NULL);

      /* Category cell: every matching custom-tab category, joined "; ". */
      {
        GString *cat = g_string_new (NULL);

        for (j = 0; j < cats->len; j++)
          {
            CatInfo *info = g_ptr_array_index (cats, j);

            if (g_hash_table_contains (info->id_set, app_id))
              {
                if (cat->len > 0)
                  g_string_append (cat, "; ");
                g_string_append (cat, info->display_name);
              }
          }
        category_cell = g_string_free (cat, FALSE);
      }

      noncore = bz_label_store_get_noncore_labels (store, app_id);
      if (noncore != NULL && noncore[0] != NULL)
        {
          GString *c = g_string_new (NULL);
          guint    k;

          for (k = 0; noncore[k] != NULL; k++)
            {
              if (k > 0)
                g_string_append (c, "; ");
              g_string_append (c, noncore[k]);
            }
          noncore_cell = g_string_free (c, FALSE);
        }
      g_strfreev (noncore);

      core_label = bz_label_store_get_core_label (store, app_id);

      if (!write_row (G_OUTPUT_STREAM (out), line, app_name, app_id,
                      category_cell, core_label, noncore_cell,
                      aes, usa, fea, iss, error))
        return FALSE;
    }

  return TRUE;
}