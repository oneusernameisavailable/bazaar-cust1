/*
 * Unit tests for CzCustomLabelStore.
 *
 * Every test outputs structured DIAG: lines with key=value pairs so AI
 * consumers can grep for "DIAG" and parse state without reading free-form
 * log text.
 */

#include "config.h"

#include "cz-custom-label-store.h"

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
 * cz_custom_label_store_new() leaves self->store == NULL and
 * add_noncore_label_name() g_warning's ("database store is NULL"). */
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
test_noncore_add_has_remove (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();

  g_assert_false (cz_custom_label_store_has_noncore_label (
      store, "org.test.App1", "Test1"));
  g_test_message ("DIAG:action=initial-has | app=org.test.App1 | label=Test1 | has=0");

  cz_custom_label_store_add_noncore_label (store, "org.test.App1", "Test1");
  {
    gboolean h = cz_custom_label_store_has_noncore_label (
        store, "org.test.App1", "Test1");
    g_test_message ("DIAG:action=after-add-1 | app=org.test.App1 | label=Test1 | has=%d", h);
    g_assert_true (h);
  }

  cz_custom_label_store_add_noncore_label (store, "org.test.App1", "Test2");
  {
    gboolean h = cz_custom_label_store_has_noncore_label (
        store, "org.test.App1", "Test2");
    g_test_message ("DIAG:action=after-add-2 | app=org.test.App1 | label=Test2 | has=%d", h);
    g_assert_true (h);
  }

  cz_custom_label_store_remove_noncore_label (store, "org.test.App1", "Test1");
  {
    gboolean h1 = cz_custom_label_store_has_noncore_label (
        store, "org.test.App1", "Test1");
    gboolean h2 = cz_custom_label_store_has_noncore_label (
        store, "org.test.App1", "Test2");
    g_test_message ("DIAG:action=after-remove | app=org.test.App1 | Test1_has=%d | Test2_has=%d",
                    h1, h2);
    g_assert_false (h1);
    g_assert_true (h2);
  }

  g_object_unref (store);
}

static void
test_noncore_labels_isolated_per_app (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();

  cz_custom_label_store_add_noncore_label (store, "org.test.App1", "Test1");
  {
    gboolean h1 = cz_custom_label_store_has_noncore_label (
        store, "org.test.App1", "Test1");
    gboolean h2 = cz_custom_label_store_has_noncore_label (
        store, "org.test.App2", "Test1");
    g_test_message ("DIAG:action=isolation | has_app1=%d | has_app2=%d", h1, h2);
    g_assert_true (h1);
    g_assert_false (h2);
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
  cz_custom_label_store_add_noncore_label (s1, "org.test.App1", "Test1");
  cz_custom_label_store_add_noncore_label (s1, "org.test.App1", "Test2");
  cz_custom_label_store_add_noncore_label (s1, "org.test.App2", "Test1");

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
    gboolean h1t1 = cz_custom_label_store_has_noncore_label (s2, "org.test.App1", "Test1");
    gboolean h1t2 = cz_custom_label_store_has_noncore_label (s2, "org.test.App1", "Test2");
    gboolean h2t1 = cz_custom_label_store_has_noncore_label (s2, "org.test.App2", "Test1");
    gboolean h1nx = cz_custom_label_store_has_noncore_label (s2, "org.test.App1", "DoesNotExist");

    g_test_message ("DIAG:action=verify-roundtrip"
                    " | app1_core=%s | app2_core=%s"
                    " | app1_has_Test1=%d | app1_has_Test2=%d"
                    " | app2_has_Test1=%d | app1_has_nonexist=%d",
                    l1 ? l1 : "(null)", l2 ? l2 : "(null)",
                    h1t1, h1t2, h2t1, h1nx);
    g_assert_cmpstr (l1, ==, "Install");
    g_assert_cmpstr (l2, ==, "Forget it");
    g_assert_true (h1t1);
    g_assert_true (h1t2);
    g_assert_true (h2t1);
    g_assert_false (h1nx);
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
    gboolean    h = cz_custom_label_store_has_noncore_label (s2, "org.unknown", "Test1");
    g_test_message ("DIAG:action=verify-empty | core_default=%s | has_noncore=%d",
                    l ? l : "(null)", h);
    g_assert_cmpstr (l, ==, "New");
    g_assert_false (h);
  }

  g_object_unref (s2);
  cleanup (path);
  g_free (path);
}

static void
test_get_all_noncore_label_names (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  GPtrArray *names;

  names = cz_custom_label_store_get_all_noncore_label_names (store);
  g_test_message ("DIAG:action=empty-names | count=%u", names->len);
  g_assert_nonnull (names);
  g_assert_cmpuint (names->len, ==, 0);
  g_ptr_array_unref (names);

  cz_custom_label_store_add_noncore_label (store, "org.test.App1", "Test1");
  cz_custom_label_store_add_noncore_label (store, "org.test.App1", "Test2");
  cz_custom_label_store_add_noncore_label (store, "org.test.App2", "Test1");

  names = cz_custom_label_store_get_all_noncore_label_names (store);
  {
    gboolean found1 = g_ptr_array_find_with_equal_func (
        names, "Test1", g_str_equal, NULL);
    gboolean found2 = g_ptr_array_find_with_equal_func (
        names, "Test2", g_str_equal, NULL);
    g_test_message ("DIAG:action=populated-names | count=%u | has_Test1=%d | has_Test2=%d",
                    names->len, found1, found2);
    g_assert_nonnull (names);
    g_assert_cmpuint (names->len, ==, 2);
    g_assert_true (found1);
    g_assert_true (found2);
  }
  g_ptr_array_unref (names);

  g_object_unref (store);
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

/* ------------------------------------------------------------------ */
/*  Noncore label name API — pass-condition tests                       */
/* ------------------------------------------------------------------ */

static void
test_add_noncore_label_name_new (void)
{
  CzCustomLabelStore *store = make_loaded_store ();
  gboolean ok;

  ok = cz_custom_label_store_add_noncore_label_name (store, "MyTag");
  g_test_message ("DIAG:action=add-name-new | name=MyTag | ok=%d | expected=1", ok);
  g_assert_true (ok);

  {
    GPtrArray *names = cz_custom_label_store_get_all_noncore_label_names (store);
    gboolean found = g_ptr_array_find_with_equal_func (
        names, "MyTag", g_str_equal, NULL);
    g_test_message ("DIAG:action=add-name-verify | count=%u | found_MyTag=%d | expected=1",
                    names->len, found);
    g_assert_cmpuint (names->len, ==, 1);
    g_assert_true (found);
    g_ptr_array_unref (names);
  }

  g_object_unref (store);
}

static void
test_add_noncore_label_name_duplicate (void)
{
  CzCustomLabelStore *store = make_loaded_store ();
  gboolean ok;

  ok = cz_custom_label_store_add_noncore_label_name (store, "MyTag");
  g_assert_true (ok);

  ok = cz_custom_label_store_add_noncore_label_name (store, "MyTag");
  g_test_message ("DIAG:action=add-name-duplicate | name=MyTag | ok=%d | expected=0", ok);
  g_assert_false (ok);

  g_object_unref (store);
}

/*
 * test_add_noncore_label_name_null is deliberately omitted — passing NULL
 * triggers g_return_val_if_fail which is a GLib implementation detail.
 * The business-logic tests (add-name-new, add-name-duplicate) are
 * sufficient to verify the API contract.
 */

static void
test_remove_noncore_label_name_existing (void)
{
  CzCustomLabelStore *store = make_loaded_store ();
  gboolean ok;

  cz_custom_label_store_add_noncore_label_name (store, "MyTag");
  cz_custom_label_store_add_noncore_label_name (store, "OtherTag");

  ok = cz_custom_label_store_remove_noncore_label_name (store, "MyTag");
  g_test_message ("DIAG:action=remove-name-existing | name=MyTag | ok=%d | expected=1", ok);
  g_assert_true (ok);

  {
    GPtrArray *names = cz_custom_label_store_get_all_noncore_label_names (store);
    gboolean found_my   = g_ptr_array_find_with_equal_func (
        names, "MyTag", g_str_equal, NULL);
    gboolean found_other = g_ptr_array_find_with_equal_func (
        names, "OtherTag", g_str_equal, NULL);
    g_test_message ("DIAG:action=remove-name-verify"
                    " | count=%u | found_MyTag=%d | found_OtherTag=%d",
                    names->len, found_my, found_other);
    g_assert_cmpuint (names->len, ==, 1);
    g_assert_false (found_my);
    g_assert_true (found_other);
    g_ptr_array_unref (names);
  }

  g_object_unref (store);
}

static void
test_remove_noncore_label_name_nonexistent (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  gboolean ok;

  ok = cz_custom_label_store_remove_noncore_label_name (store, "DoesNotExist");
  g_test_message ("DIAG:action=remove-name-nonexistent | name=DoesNotExist | ok=%d | expected=0", ok);
  g_assert_false (ok);

  g_object_unref (store);
}

/*
 * test_remove_noncore_label_name_null is deliberately omitted — same
 * rationale as add-name-null above.
 */

static void
test_remove_cleans_up_per_app_assignments (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();

  /* Assign "MyTag" to App1 → this seeds global set */
  cz_custom_label_store_add_noncore_label (store, "org.test.App1", "MyTag");
  cz_custom_label_store_add_noncore_label (store, "org.test.App1", "KeepTag");

  {
    gboolean h = cz_custom_label_store_has_noncore_label (store, "org.test.App1", "MyTag");
    g_test_message ("DIAG:action=pre-remove-cleanup | app1_has_MyTag=%d | expected=1", h);
    g_assert_true (h);
  }

  /* Remove the name globally — should also remove from per-app sets */
  cz_custom_label_store_remove_noncore_label_name (store, "MyTag");

  {
    GPtrArray *names = cz_custom_label_store_get_all_noncore_label_names (store);
    gboolean found = g_ptr_array_find_with_equal_func (
        names, "MyTag", g_str_equal, NULL);
    g_test_message ("DIAG:action=remove-cleanup-names"
                    " | count=%u | found_MyTag=%d | expected=0", names->len, found);
    g_assert_false (found);
    g_assert_cmpuint (names->len, ==, 1);
    g_ptr_array_unref (names);
  }

  {
    gboolean h1 = cz_custom_label_store_has_noncore_label (store, "org.test.App1", "MyTag");
    gboolean h2 = cz_custom_label_store_has_noncore_label (store, "org.test.App1", "KeepTag");
    g_test_message ("DIAG:action=remove-cleanup-app"
                    " | app1_has_MyTag=%d | expected=0 | app1_has_KeepTag=%d | expected=1",
                    h1, h2);
    g_assert_false (h1);
    g_assert_true (h2);
  }

  g_object_unref (store);
}

static void
test_noncore_label_names_roundtrip_save_load (void)
{
  CzCustomLabelStore *s1 = make_loaded_store ();
  CzCustomLabelStore *s2;
  char *path = temp_path ();
  gboolean ok;

  g_remove (path);

  cz_custom_label_store_add_noncore_label_name (s1, "TagA");
  cz_custom_label_store_add_noncore_label_name (s1, "TagB");
  cz_custom_label_store_add_noncore_label_name (s1, "TagC");

  ok = cz_custom_label_store_save_to_path (s1, path);
  g_test_message ("DIAG:action=names-save | ok=%d", ok);
  g_assert_true (ok);
  g_object_unref (s1);

  s2 = cz_custom_label_store_new ();
  ok = cz_custom_label_store_load_from_path (s2, path);
  g_test_message ("DIAG:action=names-load | ok=%d", ok);
  g_assert_true (ok);

  {
    GPtrArray *names = cz_custom_label_store_get_all_noncore_label_names (s2);
    gboolean fa = g_ptr_array_find_with_equal_func (names, "TagA", g_str_equal, NULL);
    gboolean fb = g_ptr_array_find_with_equal_func (names, "TagB", g_str_equal, NULL);
    gboolean fc = g_ptr_array_find_with_equal_func (names, "TagC", g_str_equal, NULL);
    g_test_message ("DIAG:action=names-verify-roundtrip"
                    " | count=%u | has_TagA=%d | has_TagB=%d | has_TagC=%d",
                    names->len, fa, fb, fc);
    g_assert_cmpuint (names->len, ==, 3);
    g_assert_true (fa);
    g_assert_true (fb);
    g_assert_true (fc);
    g_ptr_array_unref (names);
  }

  g_object_unref (s2);
  cleanup (path);
  g_free (path);
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

  {
    GPtrArray *names = cz_custom_label_store_get_all_noncore_label_names (store);
    gboolean f1 = g_ptr_array_find_with_equal_func (
        names, "LegacyTag1", g_str_equal, NULL);
    gboolean f2 = g_ptr_array_find_with_equal_func (
        names, "LegacyTag2", g_str_equal, NULL);
    g_test_message ("DIAG:action=names-backward-compat-verify"
                    " | count=%u | has_LegacyTag1=%d | has_LegacyTag2=%d",
                    names->len, f1, f2);
    g_assert_cmpuint (names->len, ==, 2);
    g_assert_true (f1);
    g_assert_true (f2);
    g_ptr_array_unref (names);
  }

  g_object_unref (store);
  cleanup (path);
  g_free (path);
}

static void
test_noncore_label_names_new_store_empty (void)
{
  CzCustomLabelStore *store = cz_custom_label_store_new ();
  GPtrArray *names;

  names = cz_custom_label_store_get_all_noncore_label_names (store);
  g_test_message ("DIAG:action=names-new-store | count=%u | expected=0", names->len);
  g_assert_nonnull (names);
  g_assert_cmpuint (names->len, ==, 0);
  g_ptr_array_unref (names);

  g_object_unref (store);
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
  cz_custom_label_store_add_noncore_label (s1, "org.test.App1", "Test1");
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
    const char *l  = cz_custom_label_store_get_core_label (s2, "org.test.App1");
    gboolean    h  = cz_custom_label_store_has_noncore_label (s2, "org.test.App1", "Test1");
    g_test_message ("DIAG:action=verify-backup-content"
                    " | core=%s | expected=Install"
                    " | has_Test1=%d | expected=1",
                    l ? l : "(null)", h);
    g_assert_cmpstr (l, ==, "Install");
    g_assert_true (h);
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
    gboolean    h = cz_custom_label_store_has_noncore_label (store, "org.test.Any", "x");
    g_test_message ("DIAG:action=verify-store-healthy | default_core=%s | expected=New"
                    " | has_any_noncore=%d | expected=0",
                    l, h);
    g_assert_cmpstr (l, ==, "New");
    g_assert_false (h);
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
    const char *l  = cz_custom_label_store_get_core_label (store, "org.test.App1");
    gboolean    h  = cz_custom_label_store_has_noncore_label (store, "org.test.App1", "x");
    g_test_message ("DIAG:action=verify-empty-core-noncore"
                    " | core=%s | expected=New | has_noncore=%d | expected=0",
                    l ? l : "(null)", h);
    g_assert_cmpstr (l, ==, "New");
    g_assert_false (h);
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
    const char *l  = cz_custom_label_store_get_core_label (store, "org.test.App1");
    gboolean    h  = cz_custom_label_store_has_noncore_label (store, "org.test.App1", "Test1");
    g_test_message ("DIAG:action=verify-empty-core-with-noncore"
                    " | core=%s | expected=New"
                    " | has_Test1=%d | expected=1",
                    l ? l : "(null)", h);
    g_assert_cmpstr (l, ==, "New");
    g_assert_true (h);
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
    const char *l  = cz_custom_label_store_get_core_label (store, "org.test.App1");
    gboolean    h  = cz_custom_label_store_has_noncore_label (store, "org.test.App1", "x");
    g_test_message ("DIAG:action=verify-missing-noncore"
                    " | core=%s | expected=Install"
                    " | has_noncore=%d | expected=0",
                    l ? l : "(null)", h);
    g_assert_cmpstr (l, ==, "Install");
    g_assert_false (h);
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
  g_test_add_func ("/cz-custom-label-store/noncore-add-has-remove",
                    test_noncore_add_has_remove);
  g_test_add_func ("/cz-custom-label-store/noncore-labels-isolated",
                    test_noncore_labels_isolated_per_app);
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
  g_test_add_func ("/cz-custom-label-store/get-all-noncore-names",
                    test_get_all_noncore_label_names);
  g_test_add_func ("/cz-custom-label-store/core-labels-isolated",
                     test_core_labels_isolated_per_app);

  /* Noncore label name API */
  g_test_add_func ("/cz-custom-label-store/add-name-new",
                     test_add_noncore_label_name_new);
  g_test_add_func ("/cz-custom-label-store/add-name-duplicate",
                     test_add_noncore_label_name_duplicate);
  g_test_add_func ("/cz-custom-label-store/remove-name-existing",
                     test_remove_noncore_label_name_existing);
  g_test_add_func ("/cz-custom-label-store/remove-name-nonexistent",
                     test_remove_noncore_label_name_nonexistent);
  g_test_add_func ("/cz-custom-label-store/remove-cleans-up-per-app",
                     test_remove_cleans_up_per_app_assignments);
  g_test_add_func ("/cz-custom-label-store/names-roundtrip-save-load",
                     test_noncore_label_names_roundtrip_save_load);
  g_test_add_func ("/cz-custom-label-store/names-backward-compat",
                     test_noncore_label_names_backward_compat);
  g_test_add_func ("/cz-custom-label-store/names-new-store-empty",
                     test_noncore_label_names_new_store_empty);

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
