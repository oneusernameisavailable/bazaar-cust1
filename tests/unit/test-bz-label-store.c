/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BzLabelStore unit tests.
 *
 * Covers:
 *   - fresh open creates an empty, valid store
 *   - core label set/get/reopen persistence, "New" default
 *   - noncore label add/has/remove, per-app isolation
 *   - label name add/list/remove-with-row-cleanup/rename
 *   - ensure_app_ids seeds defaults without clobbering
 *   - backup written per changed transaction, pruned to retention
 *   - external backup copy via BZ_PROJECT_BACKUP_DIR
 *   - corrupt primary restored from newest valid backup
 *   - corrupt primary without backup self-heals to empty store
 *   - corrupt primary whose path is the legacy JSON path preserves it
 *     (<path>.legacy), re-creates the DB and migrates from the preserved file
 *   - legacy custom-labels.json next to a fresh .db is migrated (file untouched)
 *   - no-op writes do not create a backup
 *   - export produces JSON containing core/noncore/names
 *   - verify_integrity passes on a healthy store
 *   - concurrent writers over separate connections all persist
 */

#include "bz-label-store.h"

#include <glib.h>
#include <glib/gstdio.h>

#define BACKUP_PREFIX "custom-labels-"
#define BACKUP_SUFFIX ".db"

typedef struct
{
  const char *db;
  const char *backup;
  int         thread_id;
  int         n_ids;
} WriterCtx;

static char *
make_tmpdir (void)
{
  GError *error = NULL;
  char   *dir;

  dir = g_dir_make_tmp ("bz-label-store-XXXXXX", &error);
  g_assert_no_error (error);
  return dir;
}

static gboolean
strv_has (char      **strv,
          const char *needle)
{
  int i;

  if (strv == NULL)
    return FALSE;

  for (i = 0; strv[i] != NULL; i++)
    if (g_strcmp0 (strv[i], needle) == 0)
      return TRUE;
  return FALSE;
}

static void
write_file (const char *path,
            const char *contents)
{
  GError *error = NULL;

  g_assert_true (g_file_set_contents (path, contents, -1, &error));
  g_assert_no_error (error);
}

static guint
count_backup_files (const char *backup_dir)
{
  char       *dir;
  GDir       *d;
  const char *name;
  guint       n = 0;

  dir = g_build_filename (backup_dir, "backups", NULL);
  if (!g_file_test (dir, G_FILE_TEST_IS_DIR))
    {
      g_free (dir);
      return 0;
    }

  d = g_dir_open (dir, 0, NULL);
  g_free (dir);
  if (d == NULL)
    return 0;

  while ((name = g_dir_read_name (d)) != NULL)
    if (g_str_has_prefix (name, BACKUP_PREFIX) &&
        g_str_has_suffix (name, BACKUP_SUFFIX))
      n++;
  g_dir_close (d);
  return n;
}

/* Counts files directly in a directory (external backup copies live flat in
 * BZ_PROJECT_BACKUP_DIR, without the "backups" subdirectory). */
static guint
count_files_directly (const char *dir)
{
  GDir       *d;
  const char *name;
  guint       n = 0;

  d = g_dir_open (dir, 0, NULL);
  if (d == NULL)
    return 0;

  while ((name = g_dir_read_name (d)) != NULL)
    n++;
  g_dir_close (d);
  return n;
}

static const char legacy_json[] =
    "{\n"
    "  \"version\": 1,\n"
    "  \"core\": { \"org.app.One\": \"Work\", \"org.app.Two\": \"New\" },\n"
    "  \"noncore\": { \"org.app.One\": [ \"Red\", \"Blue\" ] },\n"
    "  \"noncore_label_names\": [ \"Red\", \"Blue\", \"Green\" ]\n"
    "}\n";

/* ------------------------------------------------------------------ */
/*  Fresh open                                                          */
/* ------------------------------------------------------------------ */

static void
test_open_fresh_empty (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  char         *label;
  char        **names;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  label = bz_label_store_get_core_label (store, "org.app.Nope");
  g_assert_cmpstr (label, ==, "New");
  g_free (label);

  names = bz_label_store_get_all_label_names (store);
  g_assert_null (names);

  g_assert_true (g_file_test (db, G_FILE_TEST_EXISTS));
  g_assert_true (bz_label_store_verify_integrity (store, &error));
  g_assert_no_error (error);

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  Core labels                                                         */
/* ------------------------------------------------------------------ */

static void
test_core_label_roundtrip (void)
{
  struct
  {
    const char *app;
    const char *label;
  } cases[] = {
    {   "org.app.One",   "Work" },
    {   "org.app.Two",    "New" },
    { "org.app.Three", "Design" },
  };
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  GError       *error = NULL;
  guint         i;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  for (i = 0; i < G_N_ELEMENTS (cases); i++)
    g_assert_true (bz_label_store_set_core_label (
        store, cases[i].app, cases[i].label, &error));

  for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      char *got = bz_label_store_get_core_label (store, cases[i].app);

      g_assert_cmpstr (got, ==, cases[i].label);
      g_free (got);
    }

  bz_label_store_close (store);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      char *got = bz_label_store_get_core_label (store, cases[i].app);

      g_assert_cmpstr (got, ==, cases[i].label);
      g_free (got);
    }

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  Noncore labels                                                      */
/* ------------------------------------------------------------------ */

static void
test_noncore_labels (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  char        **labels;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  g_assert_true (bz_label_store_add_noncore_label (
      store, "org.app.One", "Red", &error));
  g_assert_no_error (error);
  g_assert_true (bz_label_store_has_noncore_label (store, "org.app.One", "Red"));
  g_assert_false (bz_label_store_has_noncore_label (store, "org.app.One", "Blue"));
  g_assert_false (bz_label_store_has_noncore_label (store, "org.app.Two", "Red"));

  labels = bz_label_store_get_noncore_labels (store, "org.app.One");
  g_assert_nonnull (labels);
  g_assert_cmpstr (labels[0], ==, "Red");
  g_assert_null (labels[1]);
  g_strfreev (labels);

  g_assert_true (bz_label_store_remove_noncore_label (
      store, "org.app.One", "Red", &error));
  g_assert_no_error (error);
  g_assert_false (bz_label_store_has_noncore_label (store, "org.app.One", "Red"));

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  Label names                                                         */
/* ------------------------------------------------------------------ */

static void
test_label_names (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  char        **names;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  g_assert_true (bz_label_store_add_label_name (store, "Red", &error));
  g_assert_true (bz_label_store_add_label_name (store, "Blue", &error));
  g_assert_true (bz_label_store_add_label_name (store, "Red", &error));
  g_assert_no_error (error);

  names = bz_label_store_get_all_label_names (store);
  g_assert_nonnull (names);
  g_assert_cmpstr (names[0], ==, "Blue");
  g_assert_cmpstr (names[1], ==, "Red");
  g_assert_null (names[2]);
  g_strfreev (names);

  g_assert_true (bz_label_store_add_noncore_label (
      store, "org.app.One", "Blue", &error));
  g_assert_cmpuint (bz_label_store_count_noncore_label (store, "Blue"), ==, 1);

  g_assert_true (bz_label_store_remove_label_name (store, "Blue", &error));
  g_assert_no_error (error);
  g_assert_cmpuint (bz_label_store_count_noncore_label (store, "Blue"), ==, 0);
  names = bz_label_store_get_all_label_names (store);
  g_assert_nonnull (names);
  g_assert_cmpstr (names[0], ==, "Red");
  g_assert_null (names[1]);
  g_strfreev (names);

  g_assert_true (bz_label_store_rename_label_name (
      store, "Red", "Crimson", &error));
  g_assert_no_error (error);
  g_assert_true (bz_label_store_add_noncore_label (
      store, "org.app.Two", "Red", &error));
  g_assert_true (bz_label_store_rename_label_name (
      store, "Red", "Crimson", &error));
  g_assert_no_error (error);
  g_assert_true (bz_label_store_has_noncore_label (
      store, "org.app.Two", "Crimson"));
  g_assert_false (bz_label_store_has_noncore_label (
      store, "org.app.Two", "Red"));

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  ensure_app_ids                                                      */
/* ------------------------------------------------------------------ */

static void
test_ensure_app_ids (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  const char   *ids[] = { "org.app.One", "org.app.Two", "org.app.Three" };
  char        **all;
  char         *label;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  g_assert_true (bz_label_store_set_core_label (
      store, "org.app.Two", "Design", &error));
  g_assert_true (bz_label_store_ensure_app_ids (
      store, ids, G_N_ELEMENTS (ids), &error));
  g_assert_no_error (error);

  label = bz_label_store_get_core_label (store, "org.app.Two");
  g_assert_cmpstr (label, ==, "Design");
  g_free (label);

  label = bz_label_store_get_core_label (store, "org.app.Three");
  g_assert_cmpstr (label, ==, "New");
  g_free (label);

  all = bz_label_store_get_core_app_ids (store);
  g_assert_nonnull (all);
  g_assert_cmpuint (g_strv_length (all), ==, 3);
  g_strfreev (all);

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  Backups                                                             */
/* ------------------------------------------------------------------ */

static void
test_backup_written_and_pruned (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  char         *backups_dir;
  BzLabelStore *store;
  GError       *error = NULL;
  int           i;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  g_assert_cmpuint (count_backup_files (backup), ==, 0);
  g_assert_true (bz_label_store_set_core_label (
      store, "org.app.One", "Work", &error));
  g_assert_no_error (error);
  g_assert_cmpuint (count_backup_files (backup), ==, 1);

  /* Seed 20 fake backups with distinct timestamps so the prune path is
   * exercised deterministically. */
  backups_dir = g_build_filename (backup, "backups", NULL);
  g_assert_true (g_mkdir_with_parents (backups_dir, 0755) == 0);
  for (i = 0; i < 20; i++)
    {
      char *name = g_strdup_printf (BACKUP_PREFIX "20260101-%08d" BACKUP_SUFFIX, i);
      char *path = g_build_filename (backups_dir, name, NULL);

      write_file (path, "junk");
      g_free (path);
      g_free (name);
    }

  g_assert_true (bz_label_store_set_core_label (
      store, "org.app.Two", "New", &error));
  g_assert_no_error (error);

  g_assert_cmpuint (count_backup_files (backup), ==, 15);
  g_free (backups_dir);

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

static void
test_external_backup_copy (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  char         *ext_dir;
  BzLabelStore *store;
  GError       *error = NULL;

  tmp     = make_tmpdir ();
  db      = g_build_filename (tmp, "labels.db", NULL);
  backup  = g_build_filename (tmp, "backup-root", NULL);
  ext_dir = g_build_filename (tmp, "external-backups", NULL);

  g_assert_true (g_mkdir_with_parents (ext_dir, 0755) == 0);
  g_setenv ("BZ_PROJECT_BACKUP_DIR", ext_dir, TRUE);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  g_assert_true (bz_label_store_set_core_label (
      store, "org.app.One", "Work", &error));
  g_assert_no_error (error);

  g_assert_cmpuint (count_backup_files (backup), ==, 1);
  g_assert_cmpuint (count_files_directly (ext_dir), ==, 1);

  g_unsetenv ("BZ_PROJECT_BACKUP_DIR");
  bz_label_store_close (store);
  g_free (ext_dir);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

static void
test_noop_does_not_backup (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  guint         before;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  g_assert_true (bz_label_store_add_noncore_label (
      store, "org.app.One", "Red", &error));
  g_assert_no_error (error);
  g_assert_cmpuint (count_backup_files (backup), ==, 1);

  before = count_backup_files (backup);
  g_assert_true (bz_label_store_add_noncore_label (
      store, "org.app.One", "Red", &error));
  g_assert_no_error (error);
  g_assert_cmpuint (count_backup_files (backup), ==, before);

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  Corruption recovery                                                 */
/* ------------------------------------------------------------------ */

static void
test_corrupt_restores_backup (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  char         *label;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);
  g_assert_true (bz_label_store_set_core_label (
      store, "org.app.One", "Work", &error));
  g_assert_true (bz_label_store_add_noncore_label (
      store, "org.app.One", "Red", &error));
  g_assert_no_error (error);
  bz_label_store_close (store);

  write_file (db, "this is not a sqlite database at all");

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  label = bz_label_store_get_core_label (store, "org.app.One");
  g_assert_cmpstr (label, ==, "Work");
  g_free (label);
  g_assert_true (bz_label_store_has_noncore_label (store, "org.app.One", "Red"));

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

static void
test_corrupt_no_backup_self_heals (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  char         *label;
  char        **names;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);
  bz_label_store_close (store);

  write_file (db, "this is not a sqlite database at all");

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  label = bz_label_store_get_core_label (store, "org.app.One");
  g_assert_cmpstr (label, ==, "New");
  g_free (label);
  names = bz_label_store_get_all_label_names (store);
  g_assert_null (names);
  g_assert_true (bz_label_store_verify_integrity (store, &error));
  g_assert_no_error (error);

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

static void
test_corrupt_legacy_path_preserved_and_migrated (void)
{
  char         *tmp;
  char         *json;
  char         *backup;
  char         *legacy_preserved;
  BzLabelStore *store;
  char         *label;
  char        **names;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  json   = g_build_filename (tmp, "custom-labels.json", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  write_file (json, legacy_json);

  store = bz_label_store_open (json, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  label = bz_label_store_get_core_label (store, "org.app.One");
  g_assert_cmpstr (label, ==, "Work");
  g_free (label);
  label = bz_label_store_get_core_label (store, "org.app.Two");
  g_assert_cmpstr (label, ==, "New");
  g_free (label);
  g_assert_true (bz_label_store_has_noncore_label (store, "org.app.One", "Red"));

  names = bz_label_store_get_all_label_names (store);
  g_assert_nonnull (names);
  g_assert_cmpuint (g_strv_length (names), ==, 3);
  g_assert_true (strv_has (names, "Green"));
  g_strfreev (names);

  bz_label_store_close (store);

  legacy_preserved = g_strdup_printf ("%s.legacy", json);
  g_assert_false (g_file_test (legacy_preserved, G_FILE_TEST_EXISTS));
  g_free (legacy_preserved);

  /* The path is now a real SQLite DB that re-opens cleanly. */
  store = bz_label_store_open (json, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);
  label = bz_label_store_get_core_label (store, "org.app.One");
  g_assert_cmpstr (label, ==, "Work");
  g_free (label);
  bz_label_store_close (store);

  g_free (backup);
  g_free (json);
  g_free (tmp);
}

static void
test_legacy_json_migrated_untouched (void)
{
  char         *tmp;
  char         *json;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  char         *label;
  char         *contents;
  gsize         len;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  json   = g_build_filename (tmp, "custom-labels.json", NULL);
  db     = g_build_filename (tmp, "custom-labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  write_file (json, legacy_json);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  label = bz_label_store_get_core_label (store, "org.app.One");
  g_assert_cmpstr (label, ==, "Work");
  g_free (label);
  g_assert_true (bz_label_store_has_noncore_label (store, "org.app.One", "Blue"));

  bz_label_store_close (store);

  g_assert_true (g_file_get_contents (json, &contents, &len, NULL));
  g_assert_cmpstr (contents, ==, legacy_json);
  g_free (contents);

  g_free (backup);
  g_free (db);
  g_free (json);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  Export                                                              */
/* ------------------------------------------------------------------ */

static void
test_export (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  GString      *out;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  g_assert_true (bz_label_store_set_core_label (
      store, "org.app.One", "Work", &error));
  g_assert_true (bz_label_store_add_noncore_label (
      store, "org.app.One", "Red", &error));
  g_assert_no_error (error);

  out = g_string_new (NULL);
  g_assert_true (bz_label_store_export (store, out, &error));
  g_assert_no_error (error);

  g_assert_nonnull (strstr (out->str, "\"core\""));
  g_assert_nonnull (strstr (out->str, "\"org.app.One\""));
  g_assert_nonnull (strstr (out->str, "Work"));
  g_assert_nonnull (strstr (out->str, "Red"));

  g_string_free (out, TRUE);
  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  Concurrency                                                         */
/* ------------------------------------------------------------------ */

static gpointer
writer_thread (gpointer data)
{
  WriterCtx    *ctx = data;
  BzLabelStore *store;
  GError       *error = NULL;
  int           i;

  store = bz_label_store_open (ctx->db, ctx->backup, &error);
  if (store == NULL)
    g_error ("writer %d: open: %s", ctx->thread_id,
             error != NULL ? error->message : "unknown");
  g_clear_error (&error);

  for (i = 0; i < ctx->n_ids; i++)
    {
      char *app = g_strdup_printf ("org.concurrent.%d.%d", ctx->thread_id, i);

      if (!bz_label_store_set_core_label (store, app, "Work", &error))
        g_error ("writer %d app %s: %s", ctx->thread_id, app, error->message);
      g_free (app);
    }

  bz_label_store_close (store);
  return NULL;
}

static void
test_concurrent_writers (void)
{
  char         *tmp;
  char         *db;
  char         *backup;
  BzLabelStore *store;
  GThread     **threads;
  WriterCtx    *ctxs;
  char        **all;
  int           n_threads  = 8;
  int           per_thread = 25;
  int           t;
  GError       *error = NULL;

  tmp    = make_tmpdir ();
  db     = g_build_filename (tmp, "labels.db", NULL);
  backup = g_build_filename (tmp, "backup-root", NULL);

  threads = g_new0 (GThread *, n_threads);
  ctxs    = g_new0 (WriterCtx, n_threads);

  for (t = 0; t < n_threads; t++)
    {
      ctxs[t].db        = db;
      ctxs[t].backup    = backup;
      ctxs[t].thread_id = t;
      ctxs[t].n_ids     = per_thread;
      threads[t]        = g_thread_new (NULL, writer_thread, &ctxs[t]);
    }

  for (t = 0; t < n_threads; t++)
    g_thread_join (threads[t]);
  g_free (threads);
  g_free (ctxs);

  store = bz_label_store_open (db, backup, &error);
  g_assert_no_error (error);
  g_assert_nonnull (store);

  all = bz_label_store_get_core_app_ids (store);
  g_assert_nonnull (all);
  g_assert_cmpuint (g_strv_length (all), ==,
                    (guint) (n_threads * per_thread));

  {
    char *got = bz_label_store_get_core_label (store, "org.concurrent.7.24");

    g_assert_cmpstr (got, ==, "Work");
    g_free (got);
  }
  g_strfreev (all);

  g_assert_cmpuint (count_backup_files (backup), <=, 15);

  bz_label_store_close (store);
  g_free (backup);
  g_free (db);
  g_free (tmp);
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_unsetenv ("BZ_PROJECT_BACKUP_DIR");

  g_test_add_func ("/bz-label-store/open-fresh-empty",
                   test_open_fresh_empty);
  g_test_add_func ("/bz-label-store/core-label-roundtrip",
                   test_core_label_roundtrip);
  g_test_add_func ("/bz-label-store/noncore-labels",
                   test_noncore_labels);
  g_test_add_func ("/bz-label-store/label-names",
                   test_label_names);
  g_test_add_func ("/bz-label-store/ensure-app-ids",
                   test_ensure_app_ids);
  g_test_add_func ("/bz-label-store/backup-written-and-pruned",
                   test_backup_written_and_pruned);
  g_test_add_func ("/bz-label-store/external-backup-copy",
                   test_external_backup_copy);
  g_test_add_func ("/bz-label-store/noop-no-backup",
                   test_noop_does_not_backup);
  g_test_add_func ("/bz-label-store/corrupt-restores-backup",
                   test_corrupt_restores_backup);
  g_test_add_func ("/bz-label-store/corrupt-no-backup-self-heals",
                   test_corrupt_no_backup_self_heals);
  g_test_add_func ("/bz-label-store/corrupt-legacy-path-preserved",
                   test_corrupt_legacy_path_preserved_and_migrated);
  g_test_add_func ("/bz-label-store/legacy-json-migrated-untouched",
                   test_legacy_json_migrated_untouched);
  g_test_add_func ("/bz-label-store/export-json",
                   test_export);
  g_test_add_func ("/bz-label-store/concurrent-writers",
                   test_concurrent_writers);

  return g_test_run ();
}
