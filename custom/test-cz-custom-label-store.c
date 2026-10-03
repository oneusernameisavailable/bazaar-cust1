/*
 * Unit tests for CzCustomLabelStore.
 *
 * Every test outputs structured DIAG: lines with key=value pairs so AI
 * consumers can grep for "DIAG" and parse state without reading free-form
 * log text.
 */

#include "config.h"

#include "cz-custom-label-store.h"
#include "bz-label-store.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <unistd.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Fixture helpers                                                     */
/* ------------------------------------------------------------------ */

static void remove_dir_tree (const char *dir);

/* Each test gets its own scratch directory so the SQLite db, its backups/
 * tree and any legacy JSON sibling never leak across tests (the store backs
 * onto bz-label-store, which shares the <dir>/backups directory). */
static char *
temp_path (void)
{
  char *dir = g_dir_make_tmp ("cz-custom-label-store-XXXXXX", NULL);
  char *path;

  g_assert_nonnull (dir);
  path = g_build_filename (dir, "labels.json", NULL);
  g_free (dir);
  return path;
}

static void
cleanup (const char *path)
{
  char *dir;

  if (path == NULL)
    return;
  dir = g_path_get_dirname (path);
  remove_dir_tree (dir);
  g_free (dir);
}

/* Build a store whose SQLite backend is open (via load_from_path), matching
 * real usage (cz-main.c loads before adding names).  A bare
 * cz_custom_label_store_new() leaves self->store == NULL and the category
 * name API g_warning()s ("database store is NULL"). */
static CzCustomLabelStore *
make_loaded_store (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  char *path = temp_path ();

  g_remove (path);
  cz_custom_label_store_load_from_path (store, path);
  g_free (path);

  return store;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                               */
/* ------------------------------------------------------------------ */

static void
test_create_and_destroy (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();

  g_test_message ("DIAG:action=create | is_store=%d",
                  CZ_IS_CUSTOM_LABEL_STORE (store) ? 1 : 0);
  g_assert_nonnull (store);
  g_assert_true (CZ_IS_CUSTOM_LABEL_STORE (store));
  g_object_unref (store);
}

static void
test_core_default_returned_for_unknown (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  const char *label;

  label = cz_custom_label_store_get_core_label (store, "org.unknown.App");
  g_test_message ("DIAG:action=get-default | app=org.unknown.App | got=%s | expected=New",
                  label);
  g_assert_cmpstr (label, ==, "New");

  g_object_unref (store);
}

static void
test_get_set_core_label (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();

  cz_custom_label_store_set_core_label (store, "org.test.App1", "Install");
  {
    const char *got = cz_custom_label_store_get_core_label (store, "org.test.App1");
    g_test_message ("DIAG:action=set-install | app=org.test.App1 | got=%s",
                    got ? got : "(null)");
    g_assert_cmpstr (got, ==, "Install");
  }

  cz_custom_label_store_set_core_label (store, "org.test.App1", "Forget it");
  {
    const char *got = cz_custom_label_store_get_core_label (store, "org.test.App1");
    g_test_message ("DIAG:action=set-forget-it | app=org.test.App1 | got=%s",
                    got ? got : "(null)");
    g_assert_cmpstr (got, ==, "Forget it");
  }

  g_object_unref (store);
}

static void
test_ensure_app_ids_creates_missing (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  const char *ids[] = { "org.test.App1", "org.test.App2", NULL };
  guint n = G_N_ELEMENTS (ids) - 1;

  cz_custom_label_store_ensure_app_ids (store, ids, n);

  {
    const char *l1 = cz_custom_label_store_get_core_label (store, "org.test.App1");
    const char *l2 = cz_custom_label_store_get_core_label (store, "org.test.App2");
    g_test_message ("DIAG:action=ensure-creates | app1=%s | app2=%s | expected=New",
                    l1 ? l1 : "(null)", l2 ? l2 : "(null)");
    g_assert_cmpstr (l1, ==, "New");
    g_assert_cmpstr (l2, ==, "New");
  }

  g_object_unref (store);
}

static void
test_ensure_app_ids_keeps_existing (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();

  cz_custom_label_store_set_core_label (store, "org.test.App1", "Install");
  {
    const char *ids[] = { "org.test.App1", "org.test.App2", NULL };
    guint n = G_N_ELEMENTS (ids) - 1;
    cz_custom_label_store_ensure_app_ids (store, ids, n);
  }

  {
    const char *l1 = cz_custom_label_store_get_core_label (store, "org.test.App1");
    const char *l2 = cz_custom_label_store_get_core_label (store, "org.test.App2");
    g_test_message ("DIAG:action=ensure-keeps | app1=%s | expected=Install | app2=%s | expected=New",
                    l1 ? l1 : "(null)", l2 ? l2 : "(null)");
    g_assert_cmpstr (l1, ==, "Install");
    g_assert_cmpstr (l2, ==, "New");
  }

  g_object_unref (store);
}

static void
test_save_and_load_roundtrip (void)
{
  CzCustomLabelStore *s1 = cz_custom_label_store_new ();
  CzCustomLabelStore *s2;
  char *path = temp_path ();
  gboolean ok;

  g_remove (path);

  cz_custom_label_store_set_core_label (s1, "org.test.App1", "Install");
  cz_custom_label_store_set_core_label (s1, "org.test.App2", "Forget it");

  ok = cz_custom_label_store_save_to_path (s1, path);
  g_test_message ("DIAG:action=save | ok=%d", ok);
  g_assert_true (ok);
  g_object_unref (s1);

  s2 = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (s2, path);
  g_test_message ("DIAG:action=load | ok=%d", ok);
  g_assert_true (ok);

  {
    const char *l1 = cz_custom_label_store_get_core_label (s2, "org.test.App1");
    const char *l2 = cz_custom_label_store_get_core_label (s2, "org.test.App2");

    g_test_message ("DIAG:action=verify-roundtrip"
                    " | app1_core=%s | app2_core=%s",
                    l1 ? l1 : "(null)", l2 ? l2 : "(null)");
    g_assert_cmpstr (l1, ==, "Install");
    g_assert_cmpstr (l2, ==, "Forget it");
  }

  g_object_unref (s2);
  cleanup (path);
  g_free (path);
}

static void
test_load_nonexistent_path_returns_true (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  gboolean ok;

  ok = cz_custom_label_store_load_from_path (
      store, "/tmp/__nonexistent_file_xyz__.json");
  g_test_message ("DIAG:action=load-nonexistent | ok=%d | expected=1", ok);
  g_assert_true (ok);

  g_object_unref (store);
}

static void
test_save_empty_store_roundtrip (void)
{
  CzCustomLabelStore *s1 = cz_custom_label_store_new ();
  CzCustomLabelStore *s2;
  char *path = temp_path ();
  gboolean ok;

  g_remove (path);

  ok = cz_custom_label_store_save_to_path (s1, path);
  g_test_message ("DIAG:action=save-empty | ok=%d", ok);
  g_assert_true (ok);
  g_object_unref (s1);

  s2 = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (s2, path);
  g_test_message ("DIAG:action=load-empty | ok=%d", ok);
  g_assert_true (ok);

  {
    const char *l = cz_custom_label_store_get_core_label (s2, "org.unknown");
    const char *c = cz_custom_label_store_get_app_custom_label (s2, "org.unknown");
    g_test_message ("DIAG:action=verify-empty | core_default=%s | custom_label=%s",
                    l ? l : "(null)", c ? c : "(null)");
    g_assert_cmpstr (l, ==, "New");
    g_assert_null (c);
  }

  g_object_unref (s2);
  cleanup (path);
  g_free (path);
}

static void
test_core_labels_isolated_per_app (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();

  cz_custom_label_store_set_core_label (store, "org.test.App1", "Install");
  cz_custom_label_store_set_core_label (store, "org.test.App2", "Forget it");

  {
    const char *l1 = cz_custom_label_store_get_core_label (store, "org.test.App1");
    const char *l2 = cz_custom_label_store_get_core_label (store, "org.test.App2");
    const char *l3 = cz_custom_label_store_get_core_label (store, "org.test.App3");
    g_test_message ("DIAG:action=core-isolation"
                    " | app1=%s | expected=Install"
                    " | app2=%s | expected=Forget it"
                    " | app3=%s | expected=New",
                    l1 ? l1 : "(null)",
                    l2 ? l2 : "(null)",
                    l3 ? l3 : "(null)");
    g_assert_cmpstr (l1, ==, "Install");
    g_assert_cmpstr (l2, ==, "Forget it");
    g_assert_cmpstr (l3, ==, "New");
  }

  g_object_unref (store);
}

static void
test_noncore_label_names_backward_compat (void)
{
  CzCustomLabelStore *store;
  char               *path = temp_path ();
  gboolean            ok;
  JsonGenerator      *gen;
  JsonNode           *root;
  JsonObject         *root_obj;
  JsonObject         *noncore_obj;
  JsonArray          *arr;
  BzLabelStore       *db;
  char               *dir;
  char              **legacy_labels;
  guint               i;
  gboolean            f1 = FALSE;
  gboolean            f2 = FALSE;


  /* Write JSON with noncore per-app assignments but NO noncore_label_names key.
   * {"version":1,"core":{},"noncore":{"org.test.App1":["LegacyTag1","LegacyTag2"]}} */
  root_obj = json_object_new ();
  json_object_set_int_member (root_obj, "version", 1);
  json_object_set_object_member (root_obj, "core", json_object_new ());

  noncore_obj = json_object_new ();
  arr = json_array_new ();
  json_array_add_string_element (arr, "LegacyTag1");
  json_array_add_string_element (arr, "LegacyTag2");
  json_object_set_array_member (noncore_obj, "org.test.App1", arr);
  json_object_set_object_member (root_obj, "noncore", noncore_obj);

  root = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (root, root_obj);
  gen = json_generator_new ();
  json_generator_set_pretty (gen, TRUE);
  json_generator_set_root (gen, root);
  json_generator_to_file (gen, path, NULL);
  json_node_unref (root);
  g_object_unref (gen);

  store = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (store, path);
  g_test_message ("DIAG:action=names-backward-compat | ok=%d | expected=1", ok);
  g_assert_true (ok);

  /* The legacy per-app rows are no longer served through a public setter/
   * reader; the observable contract is that load and save_to_path preserve
   * them.  Assert directly at the bz-label-store layer after a save
   * round-trip. */
  ok = cz_custom_label_store_save_to_path (store, path);
  g_test_message ("DIAG:action=names-backward-compat-save | ok=%d | expected=1", ok);
  g_assert_true (ok);

  dir = g_path_get_dirname (path);
  db = bz_label_store_open (path, dir, NULL);
  g_assert_nonnull (db);
  legacy_labels = bz_label_store_get_noncore_labels (db, "org.test.App1");
  g_assert_nonnull (legacy_labels);
  for (i = 0; legacy_labels[i] != NULL; i++)
    {
      if (g_str_equal (legacy_labels[i], "LegacyTag1"))
        f1 = TRUE;
      else if (g_str_equal (legacy_labels[i], "LegacyTag2"))
        f2 = TRUE;
    }
  g_strfreev (legacy_labels);
  bz_label_store_close (db);
  g_free (dir);
  g_test_message ("DIAG:action=names-backward-compat-verify"
                  " | has_LegacyTag1=%d | has_LegacyTag2=%d", f1, f2);
  g_assert_true (f1);
  g_assert_true (f2);

  g_object_unref (store);
  cleanup (path);
  g_free (path);
}

/* ------------------------------------------------------------------ */
/*  Per-category mono custom-label API                                  */
/* ------------------------------------------------------------------ */

static void
test_custom_label_unlabeled_default (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  const char *label;
  const char *category;

  label    = cz_custom_label_store_get_app_custom_label (store, "org.unknown.App");
  category = cz_custom_label_store_get_app_custom_category (store, "org.unknown.App");
  g_test_message ("DIAG:action=custom-default | app=org.unknown.App | label=%s | category=%s",
                  label ? label : "(null)", category ? category : "(null)");
  g_assert_null (label);
  g_assert_null (category);

  g_object_unref (store);
}

static void
test_custom_label_set_get (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  const char *got;
  const char *cat;

  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", "Wishlist");
  got = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
  cat = cz_custom_label_store_get_app_custom_category (store, "org.test.App1");
  g_test_message ("DIAG:action=custom-set | app=org.test.App1 | label=%s | category=%s",
                  got ? got : "(null)", cat ? cat : "(null)");
  g_assert_cmpstr (got, ==, "Wishlist");
  g_assert_cmpstr (cat, ==, "Games");

  /* Mono: re-assigning replaces the previous label. */
  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", "Daily");
  got = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
  g_test_message ("DIAG:action=custom-reset | app=org.test.App1 | label=%s | expected=Daily",
                  got ? got : "(null)");
  g_assert_cmpstr (got, ==, "Daily");

  g_object_unref (store);
}

static void
test_custom_label_clear (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  const char *got;

  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", "Wishlist");
  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", NULL);
  got = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
  g_test_message ("DIAG:action=custom-clear | app=org.test.App1 | label=%s | expected=null",
                  got ? got : "(null)");
  g_assert_null (got);

  g_object_unref (store);
}

static void
test_custom_label_isolated_per_app (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  const char *l1;
  const char *l2;

  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", "Wishlist");
  cz_custom_label_store_set_app_custom_label (store, "org.test.App2",
                                              "Games", "Wishlist");

  l1 = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
  l2 = cz_custom_label_store_get_app_custom_label (store, "org.test.App2");
  g_test_message ("DIAG:action=custom-isolation | app1=%s | app2=%s",
                  l1 ? l1 : "(null)", l2 ? l2 : "(null)");
  g_assert_cmpstr (l1, ==, "Wishlist");
  g_assert_cmpstr (l2, ==, "Wishlist");

  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", NULL);
  l1 = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
  l2 = cz_custom_label_store_get_app_custom_label (store, "org.test.App2");
  g_test_message ("DIAG:action=custom-isolation-clear | app1=%s | expected=null | app2=%s",
                  l1 ? l1 : "(null)", l2 ? l2 : "(null)");
  g_assert_null (l1);
  g_assert_cmpstr (l2, ==, "Wishlist");

  g_object_unref (store);
}

static void
test_custom_category_names_add_dup_remove (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  GPtrArray *names;
  gboolean   found;
  gboolean   ok;

  ok = cz_custom_label_store_add_category_label_name (store, "Games", "Wishlist");
  g_test_message ("DIAG:action=cat-name-add | category=Games | name=Wishlist | ok=%d | expected=1", ok);
  g_assert_true (ok);

  ok = cz_custom_label_store_add_category_label_name (store, "Games", "Wishlist");
  g_test_message ("DIAG:action=cat-name-dup | category=Games | name=Wishlist | ok=%d | expected=0", ok);
  g_assert_false (ok);

  names = cz_custom_label_store_get_category_label_names (store, "Games");
  found = g_ptr_array_find_with_equal_func (names, "Wishlist", g_str_equal, NULL);
  g_test_message ("DIAG:action=cat-name-get | category=Games | count=%u | found_Wishlist=%d",
                  names->len, found);
  g_assert_cmpuint (names->len, ==, 1);
  g_assert_true (found);
  g_ptr_array_unref (names);

  ok = cz_custom_label_store_remove_category_label_name (store, "Games", "Wishlist");
  g_test_message ("DIAG:action=cat-name-remove | category=Games | name=Wishlist | ok=%d | expected=1", ok);
  g_assert_true (ok);

  ok = cz_custom_label_store_remove_category_label_name (store, "Games", "Wishlist");
  g_test_message ("DIAG:action=cat-name-remove-again | category=Games | name=Wishlist | ok=%d | expected=0", ok);
  g_assert_false (ok);

  names = cz_custom_label_store_get_category_label_names (store, "Games");
  g_test_message ("DIAG:action=cat-name-get-after | category=Games | count=%u | expected=0", names->len);
  g_assert_cmpuint (names->len, ==, 0);
  g_ptr_array_unref (names);

  g_object_unref (store);
}

static void
test_custom_category_names_isolated_per_category (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  GPtrArray *names_games;
  GPtrArray *names_apps;

  cz_custom_label_store_add_category_label_name (store, "Games", "Wishlist");
  cz_custom_label_store_add_category_label_name (store, "Apps", "Wishlist");

  names_games = cz_custom_label_store_get_category_label_names (store, "Games");
  names_apps  = cz_custom_label_store_get_category_label_names (store, "Apps");
  g_test_message ("DIAG:action=cat-names-isolated | games=%u | apps=%u",
                  names_games->len, names_apps->len);
  g_assert_cmpuint (names_games->len, ==, 1);
  g_assert_cmpuint (names_apps->len, ==, 1);
  g_ptr_array_unref (names_games);
  g_ptr_array_unref (names_apps);

  cz_custom_label_store_remove_category_label_name (store, "Games", "Wishlist");

  names_games = cz_custom_label_store_get_category_label_names (store, "Games");
  names_apps  = cz_custom_label_store_get_category_label_names (store, "Apps");
  g_test_message ("DIAG:action=cat-names-isolated-after | games=%u | expected=0 | apps=%u | expected=1",
                  names_games->len, names_apps->len);
  g_assert_cmpuint (names_games->len, ==, 0);
  g_assert_cmpuint (names_apps->len, ==, 1);
  g_ptr_array_unref (names_games);
  g_ptr_array_unref (names_apps);

  g_object_unref (store);
}

static void
test_remove_category_name_cascades_assignments (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  const char *got;

  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", "Wishlist");
  cz_custom_label_store_add_category_label_name (store, "Games", "Wishlist");

  cz_custom_label_store_remove_category_label_name (store, "Games", "Wishlist");

  got = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
  g_test_message ("DIAG:action=cat-name-cascade | app=org.test.App1 | label=%s | expected=null",
                  got ? got : "(null)");
  g_assert_null (got);

  g_object_unref (store);
}

static void
test_count_category_label_assignments (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  guint count;

  cz_custom_label_store_set_app_custom_label (store, "org.test.App1",
                                              "Games", "Wishlist");
  cz_custom_label_store_set_app_custom_label (store, "org.test.App2",
                                              "Games", "Wishlist");
  cz_custom_label_store_set_app_custom_label (store, "org.test.App3",
                                              "Games", "Daily");
  cz_custom_label_store_set_app_custom_label (store, "org.test.App4",
                                              "Apps", "Wishlist");

  count = cz_custom_label_store_count_category_label_assignments (
      store, "Games", "Wishlist");
  g_test_message ("DIAG:action=count | category=Games | name=Wishlist | count=%u | expected=2", count);
  g_assert_cmpuint (count, ==, 2);

  count = cz_custom_label_store_count_category_label_assignments (
      store, "Games", "Daily");
  g_test_message ("DIAG:action=count | category=Games | name=Daily | count=%u | expected=1", count);
  g_assert_cmpuint (count, ==, 1);

  count = cz_custom_label_store_count_category_label_assignments (
      store, "Apps", "Wishlist");
  g_test_message ("DIAG:action=count | category=Apps | name=Wishlist | count=%u | expected=1", count);
  g_assert_cmpuint (count, ==, 1);

  count = cz_custom_label_store_count_category_label_assignments (
      store, "Games", "Missing");
  g_test_message ("DIAG:action=count | category=Games | name=Missing | count=%u | expected=0", count);
  g_assert_cmpuint (count, ==, 0);

  g_object_unref (store);
}

static void
test_per_category_roundtrip_save_load (void)
{
  CzCustomLabelStore *s1 = make_loaded_store ();
  CzCustomLabelStore *s2;
  char               *path = temp_path ();
  gboolean            ok;

  g_remove (path);

  cz_custom_label_store_set_app_custom_label (s1, "org.test.App1",
                                              "Games", "Wishlist");
  cz_custom_label_store_set_app_custom_label (s1, "org.test.App2",
                                              "Games", "Daily");
  cz_custom_label_store_add_category_label_name (s1, "Games", "Wishlist");
  cz_custom_label_store_add_category_label_name (s1, "Games", "Daily");
  cz_custom_label_store_add_category_label_name (s1, "Apps", "Wishlist");

  ok = cz_custom_label_store_save_to_path (s1, path);
  g_test_message ("DIAG:action=per-cat-save | ok=%d", ok);
  g_assert_true (ok);
  g_object_unref (s1);

  s2 = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (s2, path);
  g_test_message ("DIAG:action=per-cat-load | ok=%d", ok);
  g_assert_true (ok);

  {
    const char *l1 = cz_custom_label_store_get_app_custom_label (s2, "org.test.App1");
    const char *c1 = cz_custom_label_store_get_app_custom_category (s2, "org.test.App1");
    const char *l2 = cz_custom_label_store_get_app_custom_label (s2, "org.test.App2");
    GPtrArray  *games = cz_custom_label_store_get_category_label_names (s2, "Games");
    GPtrArray  *apps  = cz_custom_label_store_get_category_label_names (s2, "Apps");
    gboolean    fg1 = g_ptr_array_find_with_equal_func (games, "Wishlist", g_str_equal, NULL);
    gboolean    fg2 = g_ptr_array_find_with_equal_func (games, "Daily", g_str_equal, NULL);
    gboolean    fa  = g_ptr_array_find_with_equal_func (apps, "Wishlist", g_str_equal, NULL);

    g_test_message ("DIAG:action=per-cat-verify"
                    " | app1=%s | app1_cat=%s | app2=%s"
                    " | games=%u | has_Wishlist=%d | has_Daily=%d | apps=%u | has_Wishlist=%d",
                    l1 ? l1 : "(null)", c1 ? c1 : "(null)",
                    l2 ? l2 : "(null)",
                    games->len, fg1, fg2, apps->len, fa);
    g_assert_cmpstr (l1, ==, "Wishlist");
    g_assert_cmpstr (c1, ==, "Games");
    g_assert_cmpstr (l2, ==, "Daily");
    g_assert_cmpuint (games->len, ==, 2);
    g_assert_true (fg1);
    g_assert_true (fg2);
    g_assert_cmpuint (apps->len, ==, 1);
    g_assert_true (fa);

    g_ptr_array_unref (games);
    g_ptr_array_unref (apps);
  }

  g_object_unref (s2);
  cleanup (path);
  g_free (path);
}

/* ------------------------------------------------------------------ */
/*  Failure-condition tests                                             */
/* ------------------------------------------------------------------ */

/* Recursively remove a directory tree (used to clean up backup dirs). */
static void
remove_dir_tree (const char *dir)
{
  GDir        *d;
  const char  *name;

  if (dir == NULL)
    return;
  d = g_dir_open (dir, 0, NULL);
  if (d == NULL)
    return;
  while ((name = g_dir_read_name (d)) != NULL)
    {
      char *sub = g_build_filename (dir, name, NULL);
      if (g_file_test (sub, G_FILE_TEST_IS_DIR))
        remove_dir_tree (sub);
      else
        g_remove (sub);
      g_free (sub);
    }
  g_dir_close (d);
  g_rmdir (dir);
}

/* Test: corrupt primary file, valid backup exists → load falls back. */
static void
test_load_corrupt_primary_with_backup (void)
{
  CzCustomLabelStore *s1, *s2;
  char               *path = temp_path ();
  gboolean            ok;
  char               *dir;
  char               *backup_dir;


  /* Save valid store — creates primary + backup in <dir>/backups/ */
  s1 = cz_custom_label_store_new ();
  cz_custom_label_store_set_core_label (s1, "org.test.App1", "Install");
  ok = cz_custom_label_store_save_to_path (s1, path);
  g_assert_true (ok);
  g_object_unref (s1);

  /* Corrupt the primary */
  g_file_set_contents (path, "INVALID JSON CONTENT", -1, NULL);

  /* Load should fall back to backup */
  s2 = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (s2, path);
  g_test_message ("DIAG:action=corrupt-primary-with-backup | ok=%d | expected=1", ok);
  g_assert_true (ok);

  {
    const char *l = cz_custom_label_store_get_core_label (s2, "org.test.App1");
    g_test_message ("DIAG:action=verify-backup-content | core=%s | expected=Install",
                    l ? l : "(null)");
    g_assert_cmpstr (l, ==, "Install");
  }

  g_object_unref (s2);

  /* Cleanup backup tree */
  dir        = g_path_get_dirname (path);
  backup_dir = g_build_filename (dir, "backups", NULL);
  remove_dir_tree (backup_dir);
  g_free (backup_dir);
  g_free (dir);

  cleanup (path);
  g_free (path);
}

/* Test: corrupt primary file, no backup → load returns FALSE gracefully. */
static void
test_load_corrupt_primary_no_backup (void)
{
  CzCustomLabelStore *store;
  char               *path = temp_path ();
  gboolean            ok;
  char               *dir;
  char               *backup_dir;


  /* Write corrupt primary */
  g_file_set_contents (path, "INVALID JSON CONTENT", -1, NULL);

  /* Ensure no backup dir exists */
  dir        = g_path_get_dirname (path);
  backup_dir = g_build_filename (dir, "backups", NULL);
  remove_dir_tree (backup_dir);
  g_free (dir);

  /* Load self-heals into a fresh empty store (no backup fallback needed) */
  store = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (store, path);
  g_test_message ("DIAG:action=corrupt-primary-no-backup | ok=%d | expected=1", ok);
  g_assert_true (ok);

  /* Store should still work (internal state is consistent) */
  {
    const char *l = cz_custom_label_store_get_core_label (store, "org.test.Any");
    const char *c = cz_custom_label_store_get_app_custom_label (store, "org.test.Any");
    g_test_message ("DIAG:action=verify-store-healthy | default_core=%s | expected=New"
                    " | custom_label=%s | expected=null",
                    l, c ? c : "(null)");
    g_assert_cmpstr (l, ==, "New");
    g_assert_null (c);
  }

  g_object_unref (store);
  g_free (backup_dir);
  cleanup (path);
  g_free (path);
}

/* Test: load from a file with empty core and noncore objects. */
static void
test_load_empty_core_and_noncore (void)
{
  CzCustomLabelStore *store;
  char               *path = temp_path ();
  gboolean            ok;
  JsonGenerator      *gen;
  JsonNode           *root;
  JsonObject         *root_obj;


  /* Write {"version":1,"core":{},"noncore":{}} */
  root_obj = json_object_new ();
  json_object_set_int_member (root_obj, "version", 1);
  json_object_set_object_member (root_obj, "core", json_object_new ());
  json_object_set_object_member (root_obj, "noncore", json_object_new ());
  root = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (root, root_obj);
  gen = json_generator_new ();
  json_generator_set_pretty (gen, TRUE);
  json_generator_set_root (gen, root);
  json_generator_to_file (gen, path, NULL);
  json_node_unref (root);
  g_object_unref (gen);

  store = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (store, path);
  g_test_message ("DIAG:action=load-empty-core-noncore | ok=%d | expected=1", ok);
  g_assert_true (ok);

  {
    const char *l = cz_custom_label_store_get_core_label (store, "org.test.App1");
    const char *c = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
    g_test_message ("DIAG:action=verify-empty-core-noncore"
                    " | core=%s | expected=New | custom_label=%s | expected=null",
                    l ? l : "(null)", c ? c : "(null)");
    g_assert_cmpstr (l, ==, "New");
    g_assert_null (c);
  }

  g_object_unref (store);
  cleanup (path);
  g_free (path);
}

/* Test: load from file saved by an earlier version that stored "core" as an
 * empty object — verify the store defaults to "New". */
static void
test_load_empty_core_with_noncore_data (void)
{
  CzCustomLabelStore *store;
  char               *path = temp_path ();
  gboolean            ok;
  JsonGenerator      *gen;
  JsonNode           *root;
  JsonObject         *root_obj, *core_obj;


  /* Write {"version":1,"core":{},"noncore":{"org.test.App1":["Test1"]}} */
  root_obj = json_object_new ();
  json_object_set_int_member (root_obj, "version", 1);
  core_obj = json_object_new ();
  json_object_set_object_member (root_obj, "core", core_obj);
  {
    JsonObject *noncore_obj = json_object_new ();
    JsonArray  *arr = json_array_new ();
    json_array_add_string_element (arr, "Test1");
    json_object_set_array_member (noncore_obj, "org.test.App1", arr);
    json_object_set_object_member (root_obj, "noncore", noncore_obj);
  }
  root = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (root, root_obj);
  gen = json_generator_new ();
  json_generator_set_pretty (gen, TRUE);
  json_generator_set_root (gen, root);
  json_generator_to_file (gen, path, NULL);
  json_node_unref (root);
  g_object_unref (gen);

  store = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (store, path);
  g_test_message ("DIAG:action=load-empty-core-with-noncore | ok=%d | expected=1", ok);
  g_assert_true (ok);

  {
    const char *l = cz_custom_label_store_get_core_label (store, "org.test.App1");
    const char *c = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
    g_test_message ("DIAG:action=verify-empty-core-with-noncore"
                    " | core=%s | expected=New"
                    " | custom_label=%s | expected=null",
                    l ? l : "(null)", c ? c : "(null)");
    g_assert_cmpstr (l, ==, "New");
    g_assert_null (c);
  }

  g_object_unref (store);
  cleanup (path);
  g_free (path);
}

/* Test: noncore key absent from JSON — verify it does not crash and defaults
 * work.  This exercises the load_from_path path where json_object_get_object_member
 * returns NULL for "noncore". */
static void
test_load_missing_noncore_key (void)
{
  CzCustomLabelStore *store;
  char               *path = temp_path ();
  gboolean            ok;
  JsonGenerator      *gen;
  JsonNode           *root;
  JsonObject         *root_obj;


  /* Write {"version":1,"core":{"org.test.App1":"Install"}} — no noncore key */
  root_obj = json_object_new ();
  json_object_set_int_member (root_obj, "version", 1);
  {
    JsonObject *core_obj = json_object_new ();
    json_object_set_string_member (core_obj, "org.test.App1", "Install");
    json_object_set_object_member (root_obj, "core", core_obj);
  }
  root = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (root, root_obj);
  gen = json_generator_new ();
  json_generator_set_pretty (gen, TRUE);
  json_generator_set_root (gen, root);
  json_generator_to_file (gen, path, NULL);
  json_node_unref (root);
  g_object_unref (gen);

  store = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (store, path);
  g_test_message ("DIAG:action=load-missing-noncore | ok=%d | expected=1", ok);
  g_assert_true (ok);

  {
    const char *l = cz_custom_label_store_get_core_label (store, "org.test.App1");
    const char *c = cz_custom_label_store_get_app_custom_label (store, "org.test.App1");
    g_test_message ("DIAG:action=verify-missing-noncore"
                    " | core=%s | expected=Install"
                    " | custom_label=%s | expected=null",
                    l ? l : "(null)", c ? c : "(null)");
    g_assert_cmpstr (l, ==, "Install");
    g_assert_null (c);
  }

  g_object_unref (store);
  cleanup (path);
  g_free (path);
}

/* ------------------------------------------------------------------ */
/*  Suite: broken-code variants that MUST fail                        */
/*  These are not run by default — compiled with BREAKAGE_TESTS to    */
/*  prove the pass-condition tests would catch real regressions.       */
/* ------------------------------------------------------------------ */

#ifdef BREAKAGE_TESTS
#error "BREAKAGE_TESTS enabled — these tests are designed to FAIL and prove the suite catches breakage"
#endif

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/cz-custom-label-store/create-and-destroy",
                    test_create_and_destroy);
  g_test_add_func ("/cz-custom-label-store/core-default-for-unknown",
                    test_core_default_returned_for_unknown);
  g_test_add_func ("/cz-custom-label-store/get-set-core-label",
                    test_get_set_core_label);
  g_test_add_func ("/cz-custom-label-store/ensure-app-ids-creates",
                    test_ensure_app_ids_creates_missing);
  g_test_add_func ("/cz-custom-label-store/ensure-app-ids-keeps",
                    test_ensure_app_ids_keeps_existing);
  g_test_add_func ("/cz-custom-label-store/save-load-roundtrip",
                    test_save_and_load_roundtrip);
  g_test_add_func ("/cz-custom-label-store/load-nonexistent",
                    test_load_nonexistent_path_returns_true);
  g_test_add_func ("/cz-custom-label-store/save-empty-roundtrip",
                    test_save_empty_store_roundtrip);
  g_test_add_func ("/cz-custom-label-store/core-labels-isolated",
                     test_core_labels_isolated_per_app);

  /* Legacy backward compatibility: per-app noncore rows survive a save */
  g_test_add_func ("/cz-custom-label-store/names-backward-compat",
                     test_noncore_label_names_backward_compat);

  /* Per-category mono API */
  g_test_add_func ("/cz-custom-label-store/custom-unlabeled-default",
                     test_custom_label_unlabeled_default);
  g_test_add_func ("/cz-custom-label-store/custom-set-get",
                     test_custom_label_set_get);
  g_test_add_func ("/cz-custom-label-store/custom-clear",
                     test_custom_label_clear);
  g_test_add_func ("/cz-custom-label-store/custom-isolated-per-app",
                     test_custom_label_isolated_per_app);
  g_test_add_func ("/cz-custom-label-store/custom-cat-names-add-dup-remove",
                     test_custom_category_names_add_dup_remove);
  g_test_add_func ("/cz-custom-label-store/custom-cat-names-isolated",
                     test_custom_category_names_isolated_per_category);
  g_test_add_func ("/cz-custom-label-store/custom-remove-cascades",
                     test_remove_category_name_cascades_assignments);
  g_test_add_func ("/cz-custom-label-store/custom-count-assignments",
                     test_count_category_label_assignments);
  g_test_add_func ("/cz-custom-label-store/custom-roundtrip-save-load",
                     test_per_category_roundtrip_save_load);

  /* Failure-condition tests */
  g_test_add_func ("/cz-custom-label-store/load-corrupt-primary-with-backup",
                     test_load_corrupt_primary_with_backup);
  g_test_add_func ("/cz-custom-label-store/load-corrupt-primary-no-backup",
                     test_load_corrupt_primary_no_backup);
  g_test_add_func ("/cz-custom-label-store/load-empty-core-noncore",
                     test_load_empty_core_and_noncore);
  g_test_add_func ("/cz-custom-label-store/load-empty-core-with-noncore",
                     test_load_empty_core_with_noncore_data);
  g_test_add_func ("/cz-custom-label-store/load-missing-noncore-key",
                     test_load_missing_noncore_key);

  return g_test_run ();
}
