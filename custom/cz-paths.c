/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CZPaths — see cz-paths.h.
 */

#include "cz-paths.h"

char *
cz_user_data_dir (void)
{
  const char *base = g_get_user_data_dir ();

  if (base == NULL)
    return NULL;

  return g_build_filename (base, CZ_APP_ID, NULL);
}

char *
cz_labels_db_path (void)
{
  g_autofree char *dir = cz_user_data_dir ();

  if (dir == NULL)
    return NULL;

  return g_build_filename (dir, CZ_DB_BASENAME, NULL);
}

char *
cz_backups_dir (void)
{
  g_autofree char *dir = cz_user_data_dir ();

  if (dir == NULL)
    return NULL;

  return g_build_filename (dir, "backups", NULL);
}