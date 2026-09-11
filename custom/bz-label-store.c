/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BzLabelStore — SQLite-backed label persistence.
 */

#include "config.h"

#include "bz-label-store.h"

#include <json-glib/json-glib.h>
#include <sqlite3.h>

#include <glib/gstdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#define DEFAULT_CORE_LABEL "New"

#define BZ_LABEL_STORE_BACKUP_PREFIX    "custom-labels-"
#define BZ_LABEL_STORE_BACKUP_SUFFIX    ".db"
#define BZ_LABEL_STORE_BACKUP_RETENTION 20
#define BZ_LABEL_STORE_BUSY_TIMEOUT_MS  5000
#define BZ_LABEL_STORE_LOCKFILE         "bz-label-store-backup.lock"

struct _BzLabelStore
{
  sqlite3 *db;
  char    *db_path;
  char    *backup_dir;
};

G_DEFINE_QUARK (bz - label - store, bz_label_store_error)

/* ------------------------------------------------------------------ */
/*  Error helpers                                                       */
/* ------------------------------------------------------------------ */

static gint
compare_filenames (gconstpointer a,
                   gconstpointer b)
{
  /* GPtrArray passes pointers to the elements (char **), not the elements
   * themselves; g_strcmp0 must be wrapped so it compares the strings. */
  return g_strcmp0 (*(const char *const *) a, *(const char *const *) b);
}

static gboolean
set_error (sqlite3    *db,
           int         rc,
           const char *context,
           GError    **error)
{
  g_set_error (error, BZ_LABEL_STORE_ERROR, rc,
               "%s: %s", context, sqlite3_errmsg (db));
  return FALSE;
}

static gboolean
exec_ok (BzLabelStore *store,
         const char   *sql,
         GError      **error)
{
  char *errmsg = NULL;
  int   rc;
  int   attempt;

  for (attempt = 0; attempt < 5; attempt++)
    {
      rc = sqlite3_exec (store->db, sql, NULL, NULL, &errmsg);
      if (rc != SQLITE_SCHEMA)
        break;
      sqlite3_free (errmsg);
      errmsg = NULL;
      /* A concurrent writer changed the schema mid-exec; retry once the
       * schema has settled. */
      g_usleep (10 * 1000);
    }

  if (rc != SQLITE_OK)
    {
      g_set_error (error, BZ_LABEL_STORE_ERROR, rc,
                   "%s: %s",
                   sql,
                   errmsg != NULL ? errmsg : sqlite3_errmsg (store->db));
      sqlite3_free (errmsg);
      return FALSE;
    }

  sqlite3_free (errmsg);
  return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Connection helpers                                                  */
/* ------------------------------------------------------------------ */

static void
sqlite_log_cb (void       *user_data,
               int         err_code,
               const char *msg)
{
  (void) user_data;
  g_printerr ("[bz-label-store sqlite] code=%d: %s\n", err_code, msg);
}

static gboolean
open_connection (BzLabelStore *store,
                 GError      **error)
{
  int rc;

  rc = sqlite3_open_v2 (store->db_path, &store->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_FULLMUTEX,
                        NULL);
  if (rc != SQLITE_OK)
    {
      g_set_error (error, BZ_LABEL_STORE_ERROR, rc,
                   "sqlite3_open_v2 (%s): %s",
                   store->db_path, sqlite3_errmsg (store->db));
      sqlite3_close (store->db);
      store->db = NULL;
      return FALSE;
    }

  sqlite3_config (SQLITE_CONFIG_LOG, sqlite_log_cb, NULL);
  sqlite3_busy_timeout (store->db, BZ_LABEL_STORE_BUSY_TIMEOUT_MS);
  sqlite3_exec (store->db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
  sqlite3_exec (store->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
  return TRUE;
}

static gboolean
integrity_ok (BzLabelStore *store)
{
  sqlite3_stmt        *stmt = NULL;
  const unsigned char *text;
  int                  rc;
  int                  attempt;

  for (attempt = 0; attempt < 5; attempt++)
    {
      rc = sqlite3_prepare_v2 (store->db, "PRAGMA integrity_check;",
                               -1, &stmt, NULL);
      if (rc != SQLITE_OK)
        return FALSE;

      rc = sqlite3_step (stmt);
      if (rc == SQLITE_ROW)
        {
          text = sqlite3_column_text (stmt, 0);
          sqlite3_finalize (stmt);
          return text != NULL && g_strcmp0 ((const char *) text, "ok") == 0;
        }

      sqlite3_finalize (stmt);
      stmt = NULL;
      if (rc != SQLITE_SCHEMA)
        return FALSE;
      /* Concurrent schema changes (another writer's CREATE TABLE) abort
       * the pragma; re-run it once the schema settles. */
      g_usleep (10 * 1000);
    }

  return FALSE;
}

static gboolean
init_schema (BzLabelStore *store,
             GError      **error)
{
  static const char schema[] =
      "CREATE TABLE IF NOT EXISTS core_labels ("
      "  app_id TEXT PRIMARY KEY NOT NULL,"
      "  label  TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS noncore_labels ("
      "  app_id TEXT NOT NULL,"
      "  label  TEXT NOT NULL,"
      "  PRIMARY KEY (app_id, label)) WITHOUT ROWID;"
      "CREATE TABLE IF NOT EXISTS label_names ("
      "  name TEXT PRIMARY KEY NOT NULL) WITHOUT ROWID;"
      "CREATE TABLE IF NOT EXISTS app_ratings ("
      "  app_id     TEXT PRIMARY KEY NOT NULL,"
      "  aesthetics TEXT NOT NULL DEFAULT '',"
      "  usability  TEXT NOT NULL DEFAULT '',"
      "  features   TEXT NOT NULL DEFAULT '',"
      "  issues     TEXT NOT NULL DEFAULT '') WITHOUT ROWID;";

  if (!exec_ok (store, schema, error))
    return FALSE;
  return exec_ok (store, "PRAGMA user_version=2;", error);
}

static void
remove_db_files (BzLabelStore *store)
{
  char *wal = g_strdup_printf ("%s-wal", store->db_path);
  char *shm = g_strdup_printf ("%s-shm", store->db_path);

  g_remove (store->db_path);
  g_remove (wal);
  g_remove (shm);
  g_free (wal);
  g_free (shm);
}

static gboolean
copy_file (const char *src,
           const char *dst)
{
  gchar   *data;
  gsize    len;
  gboolean ok;

  ok = g_file_get_contents (src, &data, &len, NULL);
  if (!ok)
    return FALSE;

  ok = g_file_set_contents (dst, data, len, NULL);
  g_free (data);
  return ok;
}

static gboolean
reopen_fresh (BzLabelStore *store,
              GError      **error)
{
  if (store->db != NULL)
    {
      sqlite3_close (store->db);
      store->db = NULL;
    }

  remove_db_files (store);

  if (!open_connection (store, error))
    return FALSE;
  return init_schema (store, error);
}

/* ------------------------------------------------------------------ */
/*  Backups                                                             */
/* ------------------------------------------------------------------ */

static gboolean
backup_and_prune (BzLabelStore *store,
                  GError      **error)
{
  GDateTime      *now;
  char           *ts;
  char           *fname;
  char           *backup_dir;
  char           *backup_path;
  char           *lock_path;
  sqlite3        *dst = NULL;
  sqlite3_backup *bk;
  int             rc;
  int             lock_fd = -1;
  gboolean        ok      = FALSE;

  now = g_date_time_new_now_local ();
  /* %f adds microseconds so concurrent writers never collide on the same
   * backup filename (and the zero-padded digits keep the lexicographic
   * order that the prune logic relies on). */
  ts = g_date_time_format (now, BZ_LABEL_STORE_BACKUP_PREFIX "%Y%m%d-%H%M%S%f");
  g_date_time_unref (now);

  backup_dir = g_build_filename (store->backup_dir, "backups", NULL);
  g_mkdir_with_parents (backup_dir, 0755);

  /* Serialize backup + prune across threads and processes: the retention
   * prune cannot safely distinguish a slow in-flight backup from an old
   * one, so concurrent pruning could unlink a file another writer is still
   * copying into (seen as SQLITE_IOERR_DELETE + "file unlinked while open"). */
  lock_path = g_build_filename (backup_dir, BZ_LABEL_STORE_LOCKFILE, NULL);
  lock_fd   = open (lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  g_free (lock_path);
  if (lock_fd < 0 || flock (lock_fd, LOCK_EX) != 0)
    {
      if (lock_fd >= 0)
        close (lock_fd);
      g_set_error (error, BZ_LABEL_STORE_ERROR, errno,
                   "backup: could not lock %s", backup_dir);
      goto out;
    }

  fname       = g_strdup_printf ("%s%s", ts, BZ_LABEL_STORE_BACKUP_SUFFIX);
  backup_path = g_build_filename (backup_dir, fname, NULL);

  rc = sqlite3_open_v2 (backup_path, &dst,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_FULLMUTEX,
                        NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_busy_timeout (dst, BZ_LABEL_STORE_BUSY_TIMEOUT_MS);
      bk = sqlite3_backup_init (dst, "main", store->db, "main");
      if (bk != NULL)
        {
          int attempt;

          /* A concurrent writer can hold the source for a moment; retry
           * transient BUSY/LOCKED results before giving up. */
          for (attempt = 0; attempt < 5; attempt++)
            {
              rc = sqlite3_backup_step (bk, -1);
              if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED)
                break;
              g_usleep (50 * 1000);
            }
          ok = rc == SQLITE_DONE;
          sqlite3_backup_finish (bk);
        }
      else
        g_set_error (error, BZ_LABEL_STORE_ERROR, SQLITE_ERROR,
                     "backup: init %s: %s", backup_path,
                     sqlite3_errmsg (dst));

      if (!ok && (error == NULL || *error == NULL))
        g_set_error (error, BZ_LABEL_STORE_ERROR, rc,
                     "backup: step %s: rc=%d (src: %s)", backup_path, rc,
                     sqlite3_errmsg (store->db));

      sqlite3_close (dst);
    }
  else
    {
      g_set_error (error, BZ_LABEL_STORE_ERROR, rc,
                   "backup: open %s: %s", backup_path,
                   sqlite3_errmsg (dst));
      sqlite3_close (dst);
      dst = NULL;
    }

  if (!ok)
    {
      g_remove (backup_path);
      goto out;
    }

  {
    const char *ext_dir = g_getenv ("BZ_PROJECT_BACKUP_DIR");

#ifdef BZ_PROJECT_BACKUP_DIR_DEFAULT
    if ((ext_dir == NULL || *ext_dir == '\0') &&
        *BZ_PROJECT_BACKUP_DIR_DEFAULT != '\0')
      ext_dir = BZ_PROJECT_BACKUP_DIR_DEFAULT;
#endif

    if (ext_dir != NULL && *ext_dir != '\0')
      {
        char *ext_path;

        g_mkdir_with_parents (ext_dir, 0755);
        ext_path = g_build_filename (ext_dir, fname, NULL);
        copy_file (backup_path, ext_path);
        g_free (ext_path);
      }
  }

  {
    GDir       *d;
    GPtrArray  *files;
    const char *fname_iter;

    d = g_dir_open (backup_dir, 0, NULL);
    if (d != NULL)
      {
        files = g_ptr_array_new_with_free_func (g_free);
        while ((fname_iter = g_dir_read_name (d)) != NULL)
          if (g_str_has_prefix (fname_iter, BZ_LABEL_STORE_BACKUP_PREFIX) && g_str_has_suffix (fname_iter, BZ_LABEL_STORE_BACKUP_SUFFIX))
            g_ptr_array_add (files, g_strdup (fname_iter));
        g_dir_close (d);

        if (files->len > BZ_LABEL_STORE_BACKUP_RETENTION)
          {
            guint i;

            g_ptr_array_sort (files, compare_filenames);
            for (i = 0; i < files->len - BZ_LABEL_STORE_BACKUP_RETENTION; i++)
              {
                char *old_path = g_build_filename (
                    backup_dir,
                    (const char *) g_ptr_array_index (files, i),
                    NULL);
                g_remove (old_path);
                g_free (old_path);
              }
          }

        g_ptr_array_unref (files);
      }
  }

out:
  if (lock_fd >= 0)
    {
      flock (lock_fd, LOCK_UN);
      close (lock_fd);
    }
  g_free (backup_path);
  g_free (fname);
  g_free (backup_dir);
  g_free (ts);
  return ok;
}

static gboolean
restore_from_backup (BzLabelStore *store)
{
  char       *backup_dir;
  GDir       *d;
  GPtrArray  *files;
  const char *fname;
  gboolean    restored = FALSE;
  guint       i;

  backup_dir = g_build_filename (store->backup_dir, "backups", NULL);
  files      = g_ptr_array_new_with_free_func (g_free);

  if (g_file_test (backup_dir, G_FILE_TEST_IS_DIR))
    {
      d = g_dir_open (backup_dir, 0, NULL);
      if (d != NULL)
        {
          while ((fname = g_dir_read_name (d)) != NULL)
            if (g_str_has_prefix (fname, BZ_LABEL_STORE_BACKUP_PREFIX) && g_str_has_suffix (fname, BZ_LABEL_STORE_BACKUP_SUFFIX))
              g_ptr_array_add (files, g_strdup (fname));
          g_dir_close (d);
        }
    }

  if (files->len > 0)
    {
      g_ptr_array_sort (files, compare_filenames);

      for (i = files->len; i > 0 && !restored; i--)
        {
          char *backup_path = g_build_filename (
              backup_dir, (const char *) g_ptr_array_index (files, i - 1), NULL);

          sqlite3_close (store->db);
          store->db = NULL;

          remove_db_files (store);

          if (copy_file (backup_path, store->db_path) &&
              open_connection (store, NULL))
            {
              if (integrity_ok (store))
                {
                  init_schema (store, NULL);
                  restored = TRUE;
                }
              else
                {
                  sqlite3_close (store->db);
                  store->db = NULL;
                }
            }

          g_free (backup_path);
        }
    }

  g_ptr_array_unref (files);
  g_free (backup_dir);
  return restored;
}

/* ------------------------------------------------------------------ */
/*  Empty check + backup info                                          */
/* ------------------------------------------------------------------ */

gboolean
bz_label_store_is_empty (BzLabelStore *store)
{
  sqlite3_stmt *stmt = NULL;
  int           rc;
  gboolean      empty = TRUE;

  g_return_val_if_fail (store != NULL && store->db != NULL, TRUE);

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT COUNT(*) FROM core_labels;", -1, &stmt, NULL);
  if (rc == SQLITE_OK)
    {
      if (sqlite3_step (stmt) == SQLITE_ROW && sqlite3_column_int (stmt, 0) > 0)
        empty = FALSE;
      sqlite3_finalize (stmt);
    }

  if (empty)
    {
      stmt = NULL;
      rc = sqlite3_prepare_v2 (store->db,
                               "SELECT COUNT(*) FROM noncore_labels;", -1, &stmt, NULL);
      if (rc == SQLITE_OK)
        {
          if (sqlite3_step (stmt) == SQLITE_ROW && sqlite3_column_int (stmt, 0) > 0)
            empty = FALSE;
          sqlite3_finalize (stmt);
        }
    }

  if (empty)
    {
      stmt = NULL;
      rc = sqlite3_prepare_v2 (store->db,
                               "SELECT COUNT(*) FROM label_names;", -1, &stmt, NULL);
      if (rc == SQLITE_OK)
        {
          if (sqlite3_step (stmt) == SQLITE_ROW && sqlite3_column_int (stmt, 0) > 0)
            empty = FALSE;
          sqlite3_finalize (stmt);
        }
    }

  return empty;
}

guint
bz_label_store_count_records (BzLabelStore *store)
{
  sqlite3_stmt *stmt = NULL;
  int           rc;
  guint         total = 0;

  g_return_val_if_fail (store != NULL && store->db != NULL, 0);

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT COUNT(*) FROM core_labels;", -1, &stmt, NULL);
  if (rc == SQLITE_OK)
    {
      if (sqlite3_step (stmt) == SQLITE_ROW)
        total += sqlite3_column_int (stmt, 0);
      sqlite3_finalize (stmt);
    }

  stmt = NULL;
  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT COUNT(*) FROM noncore_labels;", -1, &stmt, NULL);
  if (rc == SQLITE_OK)
    {
      if (sqlite3_step (stmt) == SQLITE_ROW)
        total += sqlite3_column_int (stmt, 0);
      sqlite3_finalize (stmt);
    }

  stmt = NULL;
  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT COUNT(*) FROM label_names;", -1, &stmt, NULL);
  if (rc == SQLITE_OK)
    {
      if (sqlite3_step (stmt) == SQLITE_ROW)
        total += sqlite3_column_int (stmt, 0);
      sqlite3_finalize (stmt);
    }

  return total;
}

gboolean
bz_label_store_has_backups (BzLabelStore *store)
{
  char      *backup_dir;
  GDir      *d;
  gboolean   found = FALSE;

  g_return_val_if_fail (store != NULL && store->backup_dir != NULL, FALSE);

  backup_dir = g_build_filename (store->backup_dir, "backups", NULL);

  if (g_file_test (backup_dir, G_FILE_TEST_IS_DIR))
    {
      d = g_dir_open (backup_dir, 0, NULL);
      if (d != NULL)
        {
          const char *fname;
          while ((fname = g_dir_read_name (d)) != NULL)
            {
              if (g_str_has_prefix (fname, BZ_LABEL_STORE_BACKUP_PREFIX) &&
                  g_str_has_suffix (fname, BZ_LABEL_STORE_BACKUP_SUFFIX))
                {
                  found = TRUE;
                  break;
                }
            }
          g_dir_close (d);
        }
    }

  g_free (backup_dir);
  return found;
}

char *
bz_label_store_get_latest_backup_path (BzLabelStore *store)
{
  char      *backup_dir;
  GDir      *d;
  GPtrArray *files;
  char      *result = NULL;

  g_return_val_if_fail (store != NULL && store->backup_dir != NULL, NULL);

  backup_dir = g_build_filename (store->backup_dir, "backups", NULL);
  files      = g_ptr_array_new_with_free_func (g_free);

  if (g_file_test (backup_dir, G_FILE_TEST_IS_DIR))
    {
      d = g_dir_open (backup_dir, 0, NULL);
      if (d != NULL)
        {
          const char *fname;
          while ((fname = g_dir_read_name (d)) != NULL)
            if (g_str_has_prefix (fname, BZ_LABEL_STORE_BACKUP_PREFIX) &&
                g_str_has_suffix (fname, BZ_LABEL_STORE_BACKUP_SUFFIX))
              g_ptr_array_add (files, g_strdup (fname));
          g_dir_close (d);
        }
    }

  if (files->len > 0)
    {
      g_ptr_array_sort (files, compare_filenames);
      result = g_build_filename (backup_dir,
                                 (const char *) g_ptr_array_index (files, files->len - 1),
                                 NULL);
    }

  g_ptr_array_unref (files);
  g_free (backup_dir);
  return result;
}

/* ------------------------------------------------------------------ */
/*  Transactions                                                        */
/* ------------------------------------------------------------------ */

static gboolean
tx_begin (BzLabelStore *store,
          GError      **error)
{
  return exec_ok (store, "BEGIN IMMEDIATE;", error);
}

static gboolean
tx_finish (BzLabelStore *store,
           gboolean      changed,
           GError      **error)
{
  if (!exec_ok (store, "COMMIT;", error))
    {
      sqlite3_exec (store->db, "ROLLBACK;", NULL, NULL, NULL);
      return FALSE;
    }

  if (changed && !backup_and_prune (store, error))
    return FALSE;
  return TRUE;
}

static void
tx_abort (BzLabelStore *store)
{
  sqlite3_exec (store->db, "ROLLBACK;", NULL, NULL, NULL);
}

static gboolean
step_stmt (BzLabelStore *store,
           sqlite3_stmt *stmt,
           GError      **error)
{
  int rc = sqlite3_step (stmt);

  if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    return set_error (store->db, rc, "sqlite3_step", error);
  return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Legacy JSON migration                                               */
/* ------------------------------------------------------------------ */

static char *
legacy_json_path (const char *db_path)
{
  char *dir       = g_path_get_dirname (db_path);
  char *base      = g_path_get_basename (db_path);
  char *dot       = strrchr (base, '.');
  char *stem      = dot != NULL ? g_strndup (base, dot - base) : g_strdup (base);
  char *json_name = g_strdup_printf ("%s.json", stem);
  char *result    = g_build_filename (dir, json_name, NULL);

  g_free (dir);
  g_free (base);
  g_free (stem);
  g_free (json_name);
  return result;
}

static gboolean
insert_core_row (BzLabelStore *store,
                 const char   *app_id,
                 const char   *label,
                 GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT OR IGNORE INTO core_labels(app_id,label) VALUES(?1,?2);",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return set_error (store->db, rc, "prepare core insert", error);

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  sqlite3_finalize (stmt);
  return ok;
}

static gboolean
insert_noncore_row (BzLabelStore *store,
                    const char   *app_id,
                    const char   *label,
                    GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT OR IGNORE INTO noncore_labels(app_id,label) VALUES(?1,?2);",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return set_error (store->db, rc, "prepare noncore insert", error);

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  sqlite3_finalize (stmt);
  return ok;
}

static gboolean
insert_label_name (BzLabelStore *store,
                   const char   *label,
                   GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT OR IGNORE INTO label_names(name) VALUES(?1);",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return set_error (store->db, rc, "prepare name insert", error);

  sqlite3_bind_text (stmt, 1, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  sqlite3_finalize (stmt);
  return ok;
}

static gboolean
migrate_legacy_json_from (BzLabelStore *store,
                          const char   *legacy_path,
                          GError      **error)
{
  g_autofree char *path = NULL;
  JsonParser      *parser;
  JsonNode        *root;
  JsonObject      *root_obj;
  GHashTable      *name_set;
  gboolean         ok = TRUE;

  path = g_strdup (legacy_path);
  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    return FALSE;

  parser = json_parser_new ();
  if (!json_parser_load_from_file (parser, path, NULL))
    {
      g_object_unref (parser);
      return FALSE;
    }

  root     = json_parser_get_root (parser);
  root_obj = json_node_get_object (root);
  if (root_obj == NULL)
    {
      g_object_unref (parser);
      return FALSE;
    }

  name_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (!tx_begin (store, error))
    {
      ok = FALSE;
      goto out;
    }

  if (json_object_has_member (root_obj, "core"))
    {
      JsonObject *core_obj = json_object_get_object_member (root_obj, "core");

      if (core_obj != NULL)
        {
          GList *members = json_object_get_members (core_obj);
          GList *iter;

          for (iter = members; iter != NULL && ok; iter = iter->next)
            {
              const char *app_id = (const char *) iter->data;
              const char *label  = json_object_get_string_member (core_obj, app_id);

              if (app_id != NULL && label != NULL)
                ok = insert_core_row (store, app_id, label, error);
            }
          g_list_free (members);
        }
    }

  if (json_object_has_member (root_obj, "noncore"))
    {
      JsonObject *noncore_obj = json_object_get_object_member (root_obj, "noncore");

      if (noncore_obj != NULL)
        {
          GList *members = json_object_get_members (noncore_obj);
          GList *iter;

          for (iter = members; iter != NULL && ok; iter = iter->next)
            {
              JsonArray  *arr    = json_object_get_array_member (noncore_obj,
                                                                 (const char *) iter->data);
              const char *app_id = (const char *) iter->data;
              guint       i;

              if (arr == NULL || app_id == NULL)
                continue;

              for (i = 0; i < json_array_get_length (arr) && ok; i++)
                {
                  const char *label = json_array_get_string_element (arr, i);

                  if (label != NULL)
                    {
                      g_hash_table_add (name_set, g_strdup (label));
                      ok = insert_noncore_row (store, app_id, label, error);
                    }
                }
            }
          g_list_free (members);
        }
    }

  if (ok)
    {
      if (json_object_has_member (root_obj, "noncore_label_names"))
        {
          JsonArray *names_arr = json_object_get_array_member (
              root_obj, "noncore_label_names");
          guint i;

          if (names_arr != NULL)
            {
              g_hash_table_remove_all (name_set);
              for (i = 0; i < json_array_get_length (names_arr) && ok; i++)
                {
                  const char *name = json_array_get_string_element (names_arr, i);
                  if (name != NULL)
                    g_hash_table_add (name_set, g_strdup (name));
                }
            }
        }

      {
        GHashTableIter iter;
        gpointer       k;

        g_hash_table_iter_init (&iter, name_set);
        while (g_hash_table_iter_next (&iter, &k, NULL) && ok)
          ok = insert_label_name (store, (const char *) k, error);
      }
    }

  if (!ok)
    tx_abort (store);
  else
    ok = tx_finish (store, TRUE, error);

out:
  g_hash_table_unref (name_set);
  g_object_unref (parser);
  return ok;
}

static gboolean
migrate_legacy_json (BzLabelStore *store,
                     GError      **error)
{
  g_autofree char *path = legacy_json_path (store->db_path);

  return migrate_legacy_json_from (store, path, error);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

BzLabelStore *
bz_label_store_open (const char *db_path,
                     const char *backup_dir,
                     GError    **error)
{
  BzLabelStore *store;
  gboolean      existed;

  g_return_val_if_fail (db_path != NULL && *db_path != '\0', NULL);
  g_return_val_if_fail (backup_dir != NULL, NULL);

  store             = g_new0 (BzLabelStore, 1);
  store->db_path    = g_strdup (db_path);
  store->backup_dir = g_strdup (backup_dir);

  existed = g_file_test (db_path, G_FILE_TEST_EXISTS);

  if (!open_connection (store, error))
    goto fail;

  if (!integrity_ok (store))
    {
      g_clear_error (error);

      if (!restore_from_backup (store))
        {
          g_autofree char *legacy    = legacy_json_path (store->db_path);
          g_autofree char *preserved = NULL;
          GError          *mig_error = NULL;

          if (g_strcmp0 (legacy, store->db_path) == 0)
            {
              preserved = g_strdup_printf ("%s.legacy", store->db_path);
              if (g_rename (store->db_path, preserved) != 0)
                {
                  g_free (preserved);
                  preserved = NULL;
                }
            }

          if (!reopen_fresh (store, error))
            goto fail;

          if (preserved != NULL)
            migrate_legacy_json_from (store, preserved, &mig_error);
          else
            migrate_legacy_json_from (store, legacy, &mig_error);

          if (mig_error != NULL)
            {
              g_propagate_error (error, mig_error);
              goto fail;
            }

          if (preserved != NULL)
            g_remove (preserved);
        }
    }

  if (!init_schema (store, error))
    goto fail;

  if (!existed)
    {
      g_clear_error (error);
      migrate_legacy_json (store, NULL);
    }

  return store;

fail:
  bz_label_store_close (store);
  return NULL;
}

void
bz_label_store_close (BzLabelStore *store)
{
  if (store == NULL)
    return;

  if (store->db != NULL)
    {
      sqlite3_close (store->db);
      store->db = NULL;
    }

  g_free (store->db_path);
  g_free (store->backup_dir);
  g_free (store);
}

char *
bz_label_store_get_core_label (BzLabelStore *store,
                               const char   *app_id)
{
  sqlite3_stmt *stmt   = NULL;
  char         *result = NULL;
  int           rc;

  g_return_val_if_fail (store != NULL && store->db != NULL, NULL);
  g_return_val_if_fail (app_id != NULL, NULL);

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT label FROM core_labels WHERE app_id=?1;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    result = g_strdup ((const char *) sqlite3_column_text (stmt, 0));
  sqlite3_finalize (stmt);

  return result != NULL ? result : g_strdup (DEFAULT_CORE_LABEL);
}

gboolean
bz_label_store_set_core_label (BzLabelStore *store,
                               const char   *app_id,
                               const char   *label,
                               GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (app_id != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);

  if (!tx_begin (store, error))
    return FALSE;

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT INTO core_labels(app_id,label) VALUES(?1,?2) "
                           "ON CONFLICT(app_id) DO UPDATE SET label=excluded.label;",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare core upsert", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, TRUE, error);
}

gboolean
bz_label_store_get_app_review (BzLabelStore *store,
                               const char   *app_id,
                               char        **aesthetics,
                               char        **usability,
                               char        **features,
                               char        **issues,
                               GError      **error)
{
  sqlite3_stmt        *stmt   = NULL;
  const unsigned char *t;
  int                  rc;
  gboolean             found  = FALSE;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (app_id != NULL, FALSE);

  if (aesthetics != NULL)
    *aesthetics = NULL;
  if (usability != NULL)
    *usability = NULL;
  if (features != NULL)
    *features = NULL;
  if (issues != NULL)
    *issues = NULL;

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT aesthetics, usability, features, issues "
                           "FROM app_ratings WHERE app_id=?1;",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return set_error (store->db, rc, "prepare review select", error);

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      found = TRUE;
      t = sqlite3_column_text (stmt, 0);
      if (aesthetics != NULL && t != NULL)
        *aesthetics = g_strdup ((const char *) t);
      t = sqlite3_column_text (stmt, 1);
      if (usability != NULL && t != NULL)
        *usability = g_strdup ((const char *) t);
      t = sqlite3_column_text (stmt, 2);
      if (features != NULL && t != NULL)
        *features = g_strdup ((const char *) t);
      t = sqlite3_column_text (stmt, 3);
      if (issues != NULL && t != NULL)
        *issues = g_strdup ((const char *) t);
    }
  sqlite3_finalize (stmt);

  return found;
}

gboolean
bz_label_store_set_app_review (BzLabelStore *store,
                               const char   *app_id,
                               const char   *aesthetics,
                               const char   *usability,
                               const char   *features,
                               const char   *issues,
                               GError      **error)
{
  sqlite3_stmt *stmt = NULL;
  const char   *aes;
  const char   *usa;
  const char   *fea;
  const char   *iss;
  const char   *sql;
  int           rc;
  gboolean      ok;
  gboolean      empty;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (app_id != NULL, FALSE);

  aes = aesthetics != NULL ? aesthetics : "";
  usa = usability != NULL ? usability : "";
  fea = features != NULL ? features : "";
  iss = issues != NULL ? issues : "";

  empty = (aes[0] == '\0' && usa[0] == '\0' &&
           fea[0] == '\0' && iss[0] == '\0');

  if (!tx_begin (store, error))
    return FALSE;

  if (empty)
    sql = "DELETE FROM app_ratings WHERE app_id=?1;";
  else
    sql = "INSERT INTO app_ratings(app_id,aesthetics,usability,features,issues) "
          "VALUES(?1,?2,?3,?4,?5) "
          "ON CONFLICT(app_id) DO UPDATE SET "
          "aesthetics=excluded.aesthetics, usability=excluded.usability, "
          "features=excluded.features, issues=excluded.issues;";

  rc = sqlite3_prepare_v2 (store->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare review upsert", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  if (!empty)
    {
      sqlite3_bind_text (stmt, 2, aes, -1, SQLITE_STATIC);
      sqlite3_bind_text (stmt, 3, usa, -1, SQLITE_STATIC);
      sqlite3_bind_text (stmt, 4, fea, -1, SQLITE_STATIC);
      sqlite3_bind_text (stmt, 5, iss, -1, SQLITE_STATIC);
    }
  ok = step_stmt (store, stmt, error);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, sqlite3_changes (store->db) > 0, error);
}

gboolean
bz_label_store_has_noncore_label (BzLabelStore *store,
                                  const char   *app_id,
                                  const char   *label)
{
  sqlite3_stmt *stmt = NULL;
  int           rc;
  gboolean      found = FALSE;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (app_id != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT 1 FROM noncore_labels WHERE app_id=?1 AND label=?2 LIMIT 1;",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return FALSE;

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, label, -1, SQLITE_STATIC);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    found = TRUE;
  sqlite3_finalize (stmt);

  return found;
}

gboolean
bz_label_store_add_noncore_label (BzLabelStore *store,
                                  const char   *app_id,
                                  const char   *label,
                                  GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;
  gint64        changed = 0;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (app_id != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);

  if (!tx_begin (store, error))
    return FALSE;

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT OR IGNORE INTO noncore_labels(app_id,label) VALUES(?1,?2);",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare noncore insert", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  changed += sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT OR IGNORE INTO label_names(name) VALUES(?1);",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare name insert", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  changed += sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, changed > 0, error);
}

gboolean
bz_label_store_remove_noncore_label (BzLabelStore *store,
                                     const char   *app_id,
                                     const char   *label,
                                     GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;
  gint64        changed;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (app_id != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);

  if (!tx_begin (store, error))
    return FALSE;

  rc = sqlite3_prepare_v2 (store->db,
                           "DELETE FROM noncore_labels WHERE app_id=?1 AND label=?2;",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare noncore delete", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, app_id, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, label, -1, SQLITE_STATIC);
  ok      = step_stmt (store, stmt, error);
  changed = sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, changed > 0, error);
}

gboolean
bz_label_store_add_label_name (BzLabelStore *store,
                               const char   *label,
                               GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;
  gint64        changed;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);

  if (!tx_begin (store, error))
    return FALSE;

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT OR IGNORE INTO label_names(name) VALUES(?1);",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare name insert", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, label, -1, SQLITE_STATIC);
  ok      = step_stmt (store, stmt, error);
  changed = sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, changed > 0, error);
}

gboolean
bz_label_store_remove_label_name (BzLabelStore *store,
                                  const char   *label,
                                  GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;
  gint64        changed = 0;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);

  if (!tx_begin (store, error))
    return FALSE;

  rc = sqlite3_prepare_v2 (store->db,
                           "DELETE FROM label_names WHERE name=?1;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare name delete", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  changed += sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  rc = sqlite3_prepare_v2 (store->db,
                           "DELETE FROM noncore_labels WHERE label=?1;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare noncore delete", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  changed += sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, changed > 0, error);
}

gboolean
bz_label_store_rename_label_name (BzLabelStore *store,
                                  const char   *old_label,
                                  const char   *new_label,
                                  GError      **error)
{
  sqlite3_stmt *stmt;
  int           rc;
  gboolean      ok;
  gint64        changed = 0;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (old_label != NULL, FALSE);
  g_return_val_if_fail (new_label != NULL, FALSE);

  if (g_strcmp0 (old_label, new_label) == 0)
    return TRUE;

  if (!tx_begin (store, error))
    return FALSE;

  rc = sqlite3_prepare_v2 (store->db,
                           "INSERT OR IGNORE INTO label_names(name) VALUES(?1);",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare name insert", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, new_label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  rc = sqlite3_prepare_v2 (store->db,
                           "DELETE FROM label_names WHERE name=?1;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare name delete", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, old_label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  changed += sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  rc = sqlite3_prepare_v2 (store->db,
                           "UPDATE noncore_labels SET label=?1 WHERE label=?2;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      set_error (store->db, rc, "prepare noncore rename", error);
      tx_abort (store);
      return FALSE;
    }

  sqlite3_bind_text (stmt, 1, new_label, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, old_label, -1, SQLITE_STATIC);
  ok = step_stmt (store, stmt, error);
  changed += sqlite3_changes (store->db);
  sqlite3_finalize (stmt);

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, changed > 0, error);
}

static char **
collect_strv (BzLabelStore *store,
              const char   *sql,
              const char   *bind_value)
{
  sqlite3_stmt *stmt;
  GPtrArray    *arr;
  int           rc;

  rc = sqlite3_prepare_v2 (store->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return NULL;

  if (bind_value != NULL)
    sqlite3_bind_text (stmt, 1, bind_value, -1, SQLITE_STATIC);

  arr = g_ptr_array_new_with_free_func (g_free);
  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const unsigned char *text = sqlite3_column_text (stmt, 0);

      if (text != NULL)
        g_ptr_array_add (arr, g_strdup ((const char *) text));
    }
  sqlite3_finalize (stmt);

  if (arr->len == 0)
    {
      g_ptr_array_unref (arr);
      return NULL;
    }

  /* GPtrArray is no longer guaranteed to be NULL-terminated since GLib 2.88
   * (g_ptr_array_new_with_free_func creates arrays without a terminator);
   * append one explicitly so the result works with g_strfreev/g_strv_length. */
  g_ptr_array_add (arr, NULL);

  return (char **) g_ptr_array_free (arr, FALSE);
}

char **
bz_label_store_get_all_label_names (BzLabelStore *store)
{
  g_return_val_if_fail (store != NULL && store->db != NULL, NULL);
  return collect_strv (store, "SELECT name FROM label_names ORDER BY name;", NULL);
}

char **
bz_label_store_get_all_core_label_names (BzLabelStore *store)
{
  g_return_val_if_fail (store != NULL && store->db != NULL, NULL);
  return collect_strv (store, "SELECT label FROM core_labels ORDER BY label;", NULL);
}

char **
bz_label_store_get_noncore_labels (BzLabelStore *store,
                                    const char   *app_id)
{
  g_return_val_if_fail (store != NULL && store->db != NULL, NULL);
  g_return_val_if_fail (app_id != NULL, NULL);
  return collect_strv (store,
                       "SELECT label FROM noncore_labels WHERE app_id=?1 ORDER BY label;",
                       app_id);
}

char **
bz_label_store_get_core_app_ids (BzLabelStore *store)
{
  g_return_val_if_fail (store != NULL && store->db != NULL, NULL);
  return collect_strv (store, "SELECT app_id FROM core_labels ORDER BY app_id;", NULL);
}

char **
bz_label_store_get_noncore_app_ids (BzLabelStore *store)
{
  g_return_val_if_fail (store != NULL && store->db != NULL, NULL);
  return collect_strv (store,
                       "SELECT DISTINCT app_id FROM noncore_labels ORDER BY app_id;", NULL);
}

gboolean
bz_label_store_ensure_app_ids (BzLabelStore      *store,
                               const char *const *app_ids,
                               gsize              n_app_ids,
                               GError           **error)
{
  gsize    i;
  gint64   changed = 0;
  gboolean ok      = TRUE;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (app_ids != NULL || n_app_ids == 0, FALSE);

  if (n_app_ids == 0)
    return TRUE;

  if (!tx_begin (store, error))
    return FALSE;

  for (i = 0; i < n_app_ids && ok; i++)
    {
      sqlite3_stmt *stmt;
      int           rc;

      if (app_ids[i] == NULL)
        continue;

      rc = sqlite3_prepare_v2 (store->db,
                               "INSERT OR IGNORE INTO core_labels(app_id,label) VALUES(?1,'New');",
                               -1, &stmt, NULL);
      if (rc != SQLITE_OK)
        {
          set_error (store->db, rc, "prepare ensure app", error);
          ok = FALSE;
          break;
        }

      sqlite3_bind_text (stmt, 1, app_ids[i], -1, SQLITE_STATIC);
      ok = step_stmt (store, stmt, error);
      changed += sqlite3_changes (store->db);
      sqlite3_finalize (stmt);
    }

  if (!ok)
    {
      tx_abort (store);
      return FALSE;
    }

  return tx_finish (store, changed > 0, error);
}

guint
bz_label_store_count_noncore_label (BzLabelStore *store,
                                    const char   *label)
{
  sqlite3_stmt *stmt = NULL;
  int           rc;
  guint         count = 0;

  g_return_val_if_fail (store != NULL && store->db != NULL, 0);
  g_return_val_if_fail (label != NULL, 0);

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT COUNT(*) FROM noncore_labels WHERE label=?1;",
                           -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text (stmt, 1, label, -1, SQLITE_STATIC);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    count = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  return count;
}

gboolean
bz_label_store_export (BzLabelStore *store,
                       GString      *out,
                       GError      **error)
{
  JsonObject    *root_obj;
  JsonObject    *core_obj;
  JsonObject    *noncore_obj;
  JsonArray     *names_arr;
  JsonNode      *root;
  JsonGenerator *gen;
  gchar         *data;
  gsize          len;
  gboolean       ok = FALSE;
  int            rc;
  sqlite3_stmt  *stmt = NULL;

  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);

  root_obj    = json_object_new ();
  core_obj    = json_object_new ();
  noncore_obj = json_object_new ();
  names_arr   = json_array_new ();

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT app_id, label FROM core_labels;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    goto out;

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const unsigned char *app = sqlite3_column_text (stmt, 0);
      const unsigned char *lbl = sqlite3_column_text (stmt, 1);

      if (app != NULL && lbl != NULL)
        json_object_set_string_member (core_obj, (const char *) app,
                                       (const char *) lbl);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT DISTINCT app_id FROM noncore_labels ORDER BY app_id;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    goto out;

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const unsigned char *app = sqlite3_column_text (stmt, 0);
      JsonArray           *app_arr;
      sqlite3_stmt        *lstmt = NULL;

      if (app == NULL)
        continue;

      rc = sqlite3_prepare_v2 (store->db,
                               "SELECT label FROM noncore_labels WHERE app_id=?1 ORDER BY label;",
                               -1, &lstmt, NULL);
      if (rc != SQLITE_OK)
        continue;

      sqlite3_bind_text (lstmt, 1, (const char *) app, -1, SQLITE_STATIC);
      app_arr = json_array_new ();
      while (sqlite3_step (lstmt) == SQLITE_ROW)
        {
          const unsigned char *lbl = sqlite3_column_text (lstmt, 0);
          if (lbl != NULL)
            json_array_add_string_element (app_arr, (const char *) lbl);
        }
      sqlite3_finalize (lstmt);

      json_object_set_array_member (noncore_obj, (const char *) app, app_arr);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;

  rc = sqlite3_prepare_v2 (store->db,
                           "SELECT name FROM label_names ORDER BY name;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    goto out;

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const unsigned char *name = sqlite3_column_text (stmt, 0);
      if (name != NULL)
        json_array_add_string_element (names_arr, (const char *) name);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;

  json_object_set_int_member (root_obj, "version", 1);
  json_object_set_object_member (root_obj, "core", core_obj);
  json_object_set_object_member (root_obj, "noncore", noncore_obj);
  json_object_set_array_member (root_obj, "noncore_label_names", names_arr);

  root = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (root, root_obj);

  gen = json_generator_new ();
  json_generator_set_pretty (gen, TRUE);
  json_generator_set_root (gen, root);
  data = json_generator_to_data (gen, &len);
  if (data != NULL)
    {
      g_string_append_len (out, data, len);
      g_free (data);
      ok = TRUE;
    }
  g_object_unref (gen);
  json_node_unref (root);

out:
  if (stmt != NULL)
    sqlite3_finalize (stmt);
  /* JsonObject is not a GObject in json-glib >= 1.10; it has its own
   * refcount and must be released with json_object_unref(). */
  json_object_unref (root_obj);
  if (!ok && error != NULL)
    {
      g_set_error (error, BZ_LABEL_STORE_ERROR, SQLITE_ERROR,
                   "export: %s", sqlite3_errmsg (store->db));
    }
  return ok;
}

gboolean
bz_label_store_verify_integrity (BzLabelStore *store,
                                 GError      **error)
{
  g_return_val_if_fail (store != NULL && store->db != NULL, FALSE);

  if (!integrity_ok (store))
    {
      g_set_error (error, BZ_LABEL_STORE_ERROR, SQLITE_CORRUPT,
                   "integrity check failed for %s", store->db_path);
      return FALSE;
    }
  return TRUE;
}
