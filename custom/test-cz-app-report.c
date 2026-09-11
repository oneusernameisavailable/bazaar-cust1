/*
 * Unit tests for CzAppReport — the CSV app/label report export.
 *
 * Every test outputs structured DIAG: lines with key=value pairs so AI
 * consumers can grep for "DIAG" and parse state without reading free-form
 * log text.
 */

#include "config.h"

#include "cz-app-report.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <utime.h>

#include "bz-application.h"
#include "bz-entry.h"
#include "bz-flathub-category.h"
#include "bz-flathub-state.h"
#include "bz-label-store.h"
#include "bz-state-info.h"

typedef struct
{
  char         *temp_dir;
  char         *db_path;
  BzLabelStore *store;
} Fixture;

static void
seed_state (void)
{
  BzStateInfo    *state   = bz_state_info_get_default ();
  BzFlathubState *flathub;
  GListStore     *cats;
  GListStore     *all;
  BzFlathubCategory *cat;
  GtkStringList     *list;

  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats    = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  cat = bz_flathub_category_new ();
  bz_flathub_category_set_name (cat, "trending");
  list = gtk_string_list_new ((const char *[]) { "org.test.App1", "org.test.App4", NULL });
  bz_flathub_category_set_applications (cat, G_LIST_MODEL (list));
  g_list_store_append (cats, cat);

  cat = bz_flathub_category_new ();
  bz_flathub_category_set_name (cat, "game");
  list = gtk_string_list_new ((const char *[]) { "org.test.App2", "org.test.App3", "org.test.App5", NULL });
  bz_flathub_category_set_applications (cat, G_LIST_MODEL (list));
  g_list_store_append (cats, cat);

  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  {
    const char *ids[] = {
      "org.test.App1", "org.test.App2", "org.test.App3",
      "org.test.App4", "org.test.App5", NULL,
    };
    for (guint i = 0; ids[i] != NULL; i++)
      {
        g_autoptr (BzEntry) entry = bz_entry_new (ids[i]);
        g_autoptr (BzEntryGroup) group = bz_entry_group_new_for_single_entry (entry);
        g_list_store_append (all, group);
        g_test_message ("DIAG:action=seed-app | app=%s", ids[i]);
      }
  }

  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);
}

static void
setup_fixture (Fixture *fix,
               gconstpointer data)
{
  fix->temp_dir = g_dir_make_tmp ("cz-app-report-test-XXXXXX", NULL);
  g_assert_nonnull (fix->temp_dir);

  fix->db_path = g_build_filename (fix->temp_dir, "custom-labels.db", NULL);
  fix->store   = bz_label_store_open (fix->db_path, fix->temp_dir, NULL);
  g_assert_nonnull (fix->store);

  seed_state ();
}

static void
teardown_fixture (Fixture *fix,
                  gconstpointer data)
{
  bz_label_store_close (fix->store);
  g_free (fix->db_path);
  g_free (fix->temp_dir);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                               */
/* ------------------------------------------------------------------ */

static void
test_suggest_path_creates_reports_dir (Fixture *fix,
                                       gconstpointer data)
{
  g_autofree char *path      = NULL;
  g_autofree char *reports   = NULL;
  g_autofree char *dir_part  = NULL;
  g_autofree char *expected  = NULL;
  char            *db_name;

  path = cz_app_report_suggest_path (fix->db_path);
  g_assert_nonnull (path);
  g_test_message ("DIAG:action=suggest | path=%s", path);

  expected = g_build_filename (fix->temp_dir, "reports", NULL);
  g_assert_true (g_str_has_prefix (path, expected));
  g_assert_true (g_str_has_suffix (path, ".csv"));

  db_name = g_path_get_basename (path);
  g_assert_true (g_str_has_prefix (db_name, "bazaar-app-report-"));
  g_free (db_name);

  dir_part = g_path_get_dirname (path);
  g_assert_true (g_file_test (dir_part, G_FILE_TEST_IS_DIR));

  reports = g_build_filename (fix->temp_dir, "reports", NULL);
  g_assert_cmpstr (dir_part, ==, reports);
}

static void
test_generate_writes_all_rows (Fixture *fix,
                               gconstpointer data)
{
  g_autofree char *path      = NULL;
  g_autofree char *contents  = NULL;
  g_autofree char *expected  = NULL;
  gsize            len       = 0;
  gchar          **lines     = NULL;
  guint            i;

  /* Seed a few labels + one full review and one partial review. */
  g_assert_true (bz_label_store_set_core_label (fix->store, "org.test.App1", "4-Stars", NULL));
  g_assert_true (bz_label_store_add_noncore_label (fix->store, "org.test.App1", "Loved", NULL));
  g_assert_true (bz_label_store_add_noncore_label (fix->store, "org.test.App1", "Useful", NULL));
  g_assert_true (bz_label_store_set_app_review (fix->store, "org.test.App1",
                                                "Good UI", "Fast", "Nice", "No bugs", NULL));
  g_assert_true (bz_label_store_set_app_review (fix->store, "org.test.App2",
                                                NULL, "Slow", NULL, NULL, NULL));
  /* Force CSV quoting on App3 to exercise the RFC-4180 writer. */
  g_assert_true (bz_label_store_add_noncore_label (fix->store, "org.test.App3", "A, B", NULL));
  g_assert_true (bz_label_store_set_app_review (fix->store, "org.test.App3",
                                                "Nice, \"slick\"", NULL, NULL, NULL, NULL));

  path = cz_app_report_suggest_path (fix->db_path);
  g_assert_true (cz_app_report_generate (fix->store, bz_state_info_get_default (),
                                         path, NULL));

  g_assert_true (g_file_get_contents (path, &contents, &len, NULL));
  g_test_message ("DIAG:action=read | path=%s | len=%zu", path, len);

  lines = g_strsplit (contents, "\n", -1);
  g_assert_cmpstr (lines[0], ==,
                   "App Name,App ID,Category,Core Label,Custom Labels,"
                   "Aesthetics,Usability,Features,Issues");
  g_assert_cmpstr (lines[1], ==,
                   ",org.test.App1,Trending,4-Stars,Loved; Useful,"
                   "Good UI,Fast,Nice,No bugs");
  g_assert_cmpstr (lines[2], ==,
                   ",org.test.App2,Gaming,New,,,Slow,,");
  g_assert_cmpstr (lines[3], ==,
                   ",org.test.App3,Gaming,New,\"A, B\",\"Nice, \"\"slick\"\"\",,,");
  g_assert_cmpstr (lines[4], ==,
                   ",org.test.App4,Trending,New,,,,,");
  g_assert_cmpstr (lines[5], ==,
                   ",org.test.App5,Gaming,New,,,,,");
  g_assert_cmpstr (lines[6], ==, "");

  expected = g_build_filename (fix->temp_dir, "reports", NULL);
  g_assert_true (g_file_test (expected, G_FILE_TEST_IS_DIR));

  g_strfreev (lines);
}

static void
test_generate_empty_state (Fixture *fix,
                           gconstpointer data)
{
  g_autofree char *path     = NULL;
  g_autofree char *contents = NULL;
  gchar          **lines    = NULL;

  path = cz_app_report_suggest_path (fix->db_path);
  g_assert_true (cz_app_report_generate (fix->store, bz_state_info_get_default (),
                                         path, NULL));
  g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));

  lines = g_strsplit (contents, "\n", -1);
  g_assert_cmpstr (lines[0], ==,
                   "App Name,App ID,Category,Core Label,Custom Labels,"
                   "Aesthetics,Usability,Features,Issues");
  g_assert_cmpstr (lines[1], ==,
                   ",org.test.App1,Trending,New,,,,,");
  g_assert_cmpstr (lines[2], ==,
                   ",org.test.App2,Gaming,New,,,,,");
  g_assert_cmpstr (lines[3], ==,
                   ",org.test.App3,Gaming,New,,,,,");
  g_assert_cmpstr (lines[4], ==,
                   ",org.test.App4,Trending,New,,,,,");
  g_assert_cmpstr (lines[5], ==,
                   ",org.test.App5,Gaming,New,,,,,");
  g_assert_cmpstr (lines[6], ==, "");

  g_strfreev (lines);
}

static guint
count_csv_files (const char *reports_dir)
{
  GDir       *dir;
  const char *name;
  guint       n = 0;

  dir = g_dir_open (reports_dir, 0, NULL);
  if (dir == NULL)
    return 0;
  while ((name = g_dir_read_name (dir)) != NULL)
    if (g_str_has_suffix (name, ".csv"))
      n++;
  g_dir_close (dir);
  return n;
}

static void
test_prune_keeps_newest (Fixture *fix,
                         gconstpointer data)
{
  g_autofree char *reports = NULL;
  struct utimbuf   times;
  int              i;

  reports = g_build_filename (fix->temp_dir, "reports", NULL);
  g_mkdir_with_parents (reports, 0700);

  /* 55 files, index 0 oldest .. 54 newest. */
  for (i = 0; i < 55; i++)
    {
      g_autofree char *name = g_strdup_printf ("%02d.csv", i);
      g_autofree char *path = g_build_filename (reports, name, NULL);

      g_file_set_contents (path, "row", 3, NULL);
      times.actime = i;
      times.modtime = i;
      g_utime (path, &times);
    }

  g_assert_cmpuint (count_csv_files (reports), ==, 55);

  cz_app_report_prune (reports, 50);
  g_assert_cmpuint (count_csv_files (reports), ==, 50);

  /* Newest (5..54) survive, oldest (0..4) gone. */
  for (i = 0; i < 55; i++)
    {
      g_autofree char *name = g_strdup_printf ("%02d.csv", i);
      g_autofree char *path = g_build_filename (reports, name, NULL);

      if (i >= 5)
        g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
      else
        g_assert_false (g_file_test (path, G_FILE_TEST_IS_REGULAR));
    }
}

static void
test_review_store_roundtrip (Fixture *fix,
                             gconstpointer data)
{
  g_autofree char *aes = NULL;
  g_autofree char *usa = NULL;
  g_autofree char *fea = NULL;
  g_autofree char *iss = NULL;
  BzLabelStore    *reopened;
  gboolean         found;

  /* Full review roundtrip. */
  g_assert_true (bz_label_store_set_app_review (fix->store, "org.test.App1",
                                                "Good UI", "Fast", "Nice", "No bugs", NULL));
  found = bz_label_store_get_app_review (fix->store, "org.test.App1",
                                         &aes, &usa, &fea, &iss, NULL);
  g_assert_true (found);
  g_assert_cmpstr (aes, ==, "Good UI");
  g_assert_cmpstr (usa, ==, "Fast");
  g_assert_cmpstr (fea, ==, "Nice");
  g_assert_cmpstr (iss, ==, "No bugs");
  g_test_message ("DIAG:action=review-roundtrip | app=org.test.App1 | aes=%s usa=%s", aes, usa);

  /* Partial review: missing fields come back as empty strings. */
  g_assert_true (bz_label_store_set_app_review (fix->store, "org.test.App2",
                                                NULL, "Slow", NULL, NULL, NULL));
  g_clear_pointer (&aes, g_free);
  g_clear_pointer (&usa, g_free);
  g_clear_pointer (&fea, g_free);
  g_clear_pointer (&iss, g_free);
  found = bz_label_store_get_app_review (fix->store, "org.test.App2",
                                         &aes, &usa, &fea, &iss, NULL);
  g_assert_true (found);
  g_assert_cmpstr (aes, ==, "");
  g_assert_cmpstr (usa, ==, "Slow");
  g_assert_cmpstr (fea, ==, "");
  g_assert_cmpstr (iss, ==, "");

  /* Persists across a reopen of the same DB file. */
  bz_label_store_close (fix->store);
  fix->store = bz_label_store_open (fix->db_path, fix->temp_dir, NULL);
  g_assert_nonnull (fix->store);

  g_clear_pointer (&aes, g_free);
  g_clear_pointer (&usa, g_free);
  g_clear_pointer (&fea, g_free);
  g_clear_pointer (&iss, g_free);
  found = bz_label_store_get_app_review (fix->store, "org.test.App1",
                                         &aes, &usa, &fea, &iss, NULL);
  g_assert_true (found);
  g_assert_cmpstr (aes, ==, "Good UI");
  g_assert_cmpstr (iss, ==, "No bugs");
  g_test_message ("DIAG:action=review-persist | app=org.test.App1 | aes=%s", aes);
}

static void
test_review_store_empty_deletes (Fixture *fix,
                                 gconstpointer data)
{
  g_autofree char *aes = NULL;
  g_autofree char *usa = NULL;
  g_autofree char *fea = NULL;
  g_autofree char *iss = NULL;
  gboolean         found;

  g_assert_true (bz_label_store_set_app_review (fix->store, "org.test.App1",
                                                "x", "y", "z", "w", NULL));
  found = bz_label_store_get_app_review (fix->store, "org.test.App1",
                                         &aes, &usa, &fea, &iss, NULL);
  g_assert_true (found);

  /* Clearing every field removes the row. */
  g_assert_true (bz_label_store_set_app_review (fix->store, "org.test.App1",
                                                NULL, NULL, NULL, NULL, NULL));
  g_clear_pointer (&aes, g_free);
  g_clear_pointer (&usa, g_free);
  g_clear_pointer (&fea, g_free);
  g_clear_pointer (&iss, g_free);
  found = bz_label_store_get_app_review (fix->store, "org.test.App1",
                                         &aes, &usa, &fea, &iss, NULL);
  g_assert_false (found);
  g_assert_null (aes);
  g_test_message ("DIAG:action=review-delete | app=org.test.App1 | found=%d", found);
}

int
main (int argc, char *argv[])
{
  int ret;

  gtk_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_type_ensure (bz_entry_get_type ());
  g_type_ensure (bz_entry_group_get_type ());

  g_test_add ("/app-report/suggest-path", Fixture,
              NULL, setup_fixture, test_suggest_path_creates_reports_dir, teardown_fixture);
  g_test_add ("/app-report/generate-rows", Fixture,
              NULL, setup_fixture, test_generate_writes_all_rows, teardown_fixture);
  g_test_add ("/app-report/generate-empty", Fixture,
              NULL, setup_fixture, test_generate_empty_state, teardown_fixture);
  g_test_add ("/app-report/prune", Fixture,
              NULL, setup_fixture, test_prune_keeps_newest, teardown_fixture);
  g_test_add ("/app-report/review-store", Fixture,
              NULL, setup_fixture, test_review_store_roundtrip, teardown_fixture);
  g_test_add ("/app-report/review-empty-deletes", Fixture,
              NULL, setup_fixture, test_review_store_empty_deletes, teardown_fixture);

  ret = g_test_run ();
  return ret;
}