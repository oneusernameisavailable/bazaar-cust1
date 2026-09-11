/*
 * Unit tests for cz-custom-filter.
 *
 * Verifies cz_category_get_show_in_list, cz_category_get_short_name,
 * cz_category_build_id_set, and cz_category_app_filter.
 */

#include "config.h"

#include "cz-custom-filter.h"

#include <glib.h>

#include "bz-entry.h"
#include "bz-entry-group.h"

static BzFlathubCategory *
make_category (const char *name)
{
  BzFlathubCategory *cat;

  cat = bz_flathub_category_new ();
  bz_flathub_category_set_name (cat, name);

  return cat;
}

/* show_in_list */

static void
test_show_in_list_returns_true_for_known_categories (void)
{
  const char *shown[] = {
    "audiovideo", "development", "education", "game",
    "graphics", "network", "office", "science",
    "system", "utility", "trending", "mobile",
    "adwaita", "kde", NULL
  };

  for (guint i = 0; shown[i] != NULL; i++)
    {
      g_autoptr (BzFlathubCategory) cat = make_category (shown[i]);
      g_assert_true (cz_category_get_show_in_list (cat));
    }
}

static void
test_show_in_list_returns_false_for_hidden_categories (void)
{
  const char *hidden[] = {
    "popular", "recently-added", "recently-updated",
    "game-only", "emulators", "launchers", "game-tools", NULL
  };

  for (guint i = 0; hidden[i] != NULL; i++)
    {
      g_autoptr (BzFlathubCategory) cat = make_category (hidden[i]);
      g_assert_false (cz_category_get_show_in_list (cat));
    }
}

static void
test_show_in_list_returns_false_for_unknown_category (void)
{
  g_autoptr (BzFlathubCategory) cat = make_category ("non-existent");
  g_assert_false (cz_category_get_show_in_list (cat));
}

/* short_name */

static void
test_short_name_for_known_categories (void)
{
  typedef struct { const char *name; const char *expected; } Pair;

  Pair pairs[] = {
    {       "audiovideo",   "Media" },
    {      "development", "Develop" },
    {        "education",   "Learn" },
    {             "game",    "Play" },
    {         "graphics",  "Create" },
    {          "network","Internet" },
    {           "office",    "Work" },
    {          "science", "Science" },
    {           "system",  "System" },
    {          "utility",   "Tools" },
    {         "trending","Trending" },
    {           "mobile",  "Mobile" },
    {          "adwaita", "Adwaita" },
    {              "kde","KDE Apps" },
    {          "popular", "Popular" },
    {   "recently-added",     "New" },
    { "recently-updated","Updated" },
    {              NULL,      NULL },
  };

  for (guint i = 0; pairs[i].name != NULL; i++)
    {
      g_autoptr (BzFlathubCategory) cat = make_category (pairs[i].name);
      const char *got = cz_category_get_short_name (cat);
      g_assert_cmpstr (got, ==, pairs[i].expected);
    }
}

static void
test_short_name_returns_name_for_unknown_category (void)
{
  g_autoptr (BzFlathubCategory) cat = make_category ("unknown-thing");
  g_assert_cmpstr (cz_category_get_short_name (cat), ==, "unknown-thing");
}

/* build_id_set */

static void
test_build_id_set_returns_empty_set_on_fresh_category (void)
{
  g_autoptr (BzFlathubCategory) cat = make_category ("trending");
  g_autoptr (GHashTable) set = cz_category_build_id_set (cat);
  g_assert_nonnull (set);
  g_assert_cmpuint (g_hash_table_size (set), ==, 0);
}

static void
test_build_id_set_returns_populated_set (void)
{
  g_autoptr (BzFlathubCategory) cat = make_category ("trending");
  GtkStringList *list = gtk_string_list_new (NULL);

  gtk_string_list_append (list, "org.test.App1");
  gtk_string_list_append (list, "org.test.App2");
  gtk_string_list_append (list, "org.test.App3");
  bz_flathub_category_set_applications (cat, G_LIST_MODEL (list));

  g_autoptr (GHashTable) set = cz_category_build_id_set (cat);
  g_assert_nonnull (set);
  g_assert_cmpuint (g_hash_table_size (set), ==, 3);
  g_assert_true (g_hash_table_contains (set, "org.test.App1"));
  g_assert_true (g_hash_table_contains (set, "org.test.App2"));
  g_assert_true (g_hash_table_contains (set, "org.test.App3"));
  g_assert_false (g_hash_table_contains (set, "org.test.Other"));
}

/* app_filter */

static BzEntryGroup *
make_group (const char *id)
{
  g_autoptr (BzEntry) entry = bz_entry_new (id);
  return bz_entry_group_new_for_single_entry (entry);
}

static void
test_app_filter_passes_matching_id (void)
{
  g_autoptr (GHashTable) set = NULL;
  g_autoptr (BzEntryGroup) group = NULL;

  set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_hash_table_add (set, g_strdup ("org.test.App1"));

  group = make_group ("org.test.App1");
  g_assert_true (cz_category_app_filter (group, set));
}

static void
test_app_filter_rejects_non_matching_id (void)
{
  g_autoptr (GHashTable) set = NULL;
  g_autoptr (BzEntryGroup) group = NULL;

  set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_hash_table_add (set, g_strdup ("org.test.App1"));

  group = make_group ("org.test.Other");
  g_assert_false (cz_category_app_filter (group, set));
}

static void
test_app_filter_rejects_null_item (void)
{
  g_autoptr (GHashTable) set = NULL;
  set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_assert_false (cz_category_app_filter (NULL, set));
}

static void
test_app_filter_rejects_non_group (void)
{
  g_autoptr (GHashTable) set = NULL;
  g_autoptr (BzFlathubCategory) cat = make_category ("trending");
  set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_assert_false (cz_category_app_filter (cat, set));
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_test_add_func ("/cz-custom-filter/show-in-list/known-true",
    test_show_in_list_returns_true_for_known_categories);
  g_test_add_func ("/cz-custom-filter/show-in-list/hidden-false",
    test_show_in_list_returns_false_for_hidden_categories);
  g_test_add_func ("/cz-custom-filter/show-in-list/unknown-false",
    test_show_in_list_returns_false_for_unknown_category);

  g_test_add_func ("/cz-custom-filter/short-name/known",
    test_short_name_for_known_categories);
  g_test_add_func ("/cz-custom-filter/short-name/unknown-returns-name",
    test_short_name_returns_name_for_unknown_category);

  g_test_add_func ("/cz-custom-filter/build-id-set/empty",
    test_build_id_set_returns_empty_set_on_fresh_category);
  g_test_add_func ("/cz-custom-filter/build-id-set/populated",
    test_build_id_set_returns_populated_set);

  g_test_add_func ("/cz-custom-filter/app-filter/matching",
    test_app_filter_passes_matching_id);
  g_test_add_func ("/cz-custom-filter/app-filter/non-matching",
    test_app_filter_rejects_non_matching_id);
  g_test_add_func ("/cz-custom-filter/app-filter/null-item",
    test_app_filter_rejects_null_item);
  g_test_add_func ("/cz-custom-filter/app-filter/non-group",
    test_app_filter_rejects_non_group);

  return g_test_run ();
}
