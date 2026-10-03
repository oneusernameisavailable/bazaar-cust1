/*
 * Unit tests for cz-custom-filter.
 *
 * Verifies cz_category_get_show_in_list, cz_category_get_short_name,
 * cz_category_build_id_set, cz_category_app_filter, and the
 * CzCategoryPrimary dedup engine (rule A primary-category assignment).
 */

#include "config.h"

#include "cz-custom-filter.h"

#include <glib.h>

#include "bz-application.h"
#include "bz-entry.h"
#include "bz-entry-group.h"
#include "bz-state-info.h"

/* --- test environment ------------------------------------------------ */

/* cz_category_build_id_set only reaches its applications-list fallback when
 * a default BzStateInfo with a (possibly empty) all-entry-groups model is
 * present.  Mirror the running app so spotlight/fallback membership works. */
static void
install_test_state (void)
{
  BzStateInfo *state = bz_state_info_get_default ();
  GListStore  *all   = NULL;

  g_assert_nonnull (state);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
}

static BzFlathubCategory *
make_category (const char *name)
{
  BzFlathubCategory *cat;

  cat = bz_flathub_category_new ();
  bz_flathub_category_set_name (cat, name);

  return cat;
}

/* Entry group with zero appstream category flags (uncategorized app). */
static void
append_zero_group (GListStore *store,
                   const char *app_id)
{
  g_autoptr (BzEntry) entry = bz_entry_new (app_id);
  g_autoptr (BzEntryGroup) group = bz_entry_group_new_for_single_entry (entry);

  g_list_store_append (store, group);
}

/* Addon/extension entry group: zero appstream categories (bz-entry-group
 * never assigns them to addons) plus is_addon=TRUE.  These must not leak into
 * the catch-all tab, where clicking them would open the addons popup rather
 * than an app page. */
static void
append_addon_group (GListStore *store,
                    const char *addon_id)
{
  g_autoptr (BzEntry) entry = bz_entry_new (addon_id);
  g_autoptr (BzEntryGroup) group = NULL;

  g_object_set (entry, "kinds", (gint) BZ_ENTRY_KIND_ADDON, NULL);
  group = bz_entry_group_new_for_single_entry (entry);
  bz_entry_group_add (group, entry, NULL, FALSE);

  g_assert_true (bz_entry_group_is_addon (group));
  g_assert_cmpuint (bz_entry_group_get_categories (group), ==, 0);

  g_list_store_append (store, group);
}

/* Addon/extension group that is EOL: must be kept off the Addons tab. */
static void
append_eol_addon_group (GListStore *store,
                        const char *addon_id)
{
  g_autoptr (BzEntry) entry = bz_entry_new (addon_id);
  g_autoptr (BzEntryGroup) group = NULL;

  g_object_set (entry, "kinds", (gint) BZ_ENTRY_KIND_ADDON, NULL);
  g_object_set (entry, "eol", "2025-01-01", NULL);
  group = bz_entry_group_new_for_single_entry (entry);
  bz_entry_group_add (group, entry, NULL, FALSE);

  g_assert_true (bz_entry_group_is_addon (group));
  g_assert_nonnull (bz_entry_group_get_eol (group));

  g_list_store_append (store, group);
}

/* show_in_list */

static void
test_show_in_list_returns_true_for_known_categories (void)
{
  const char *shown[] = {
    "audiovideo", "development", "education", "game",
    "graphics", "network", "office", "science",
    "system", "utility", "trending", "mobile",
    "adwaita", "kde", "addons", NULL
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

/* primary engine */

static void
set_apps (BzFlathubCategory *cat,
          const char        **apps)
{
  GtkStringList *list = gtk_string_list_new (NULL);

  if (apps != NULL)
    for (guint i = 0; apps[i] != NULL; i++)
      gtk_string_list_append (list, apps[i]);

  bz_flathub_category_set_applications (cat, G_LIST_MODEL (list));
}

static void
assert_set_equals (GHashTable  *set,
                   const char **expected)
{
  guint n = g_hash_table_size (set);
  guint i;

  for (i = 0; expected[i] != NULL; i++)
    g_assert_true (g_hash_table_contains (set, expected[i]));

  g_assert_cmpuint (n, ==, i);
}

static void
test_primary_new_returns_null_on_null_model (void)
{
  g_assert_null (cz_category_primary_new (NULL));
}

static void
test_primary_new_returns_null_without_visible_tabs (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) cat = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  cat = make_category ("popular");
  g_list_store_append (cats, cat);

  g_assert_null (cz_category_primary_new (G_LIST_MODEL (cats)));
}

static void
test_primary_dedupes_to_smallest_tab (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  g_autoptr (BzFlathubCategory) trending = NULL;
  g_autoptr (BzFlathubCategory) utility = NULL;
  g_autoptr (CzCategoryPrimary) eng = NULL;
  g_autoptr (GHashTable) set = NULL;

  /* Store order deliberately differs from cz_category_info[] order. */
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = make_category ("trending");
  set_apps (trending, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, trending);

  utility = make_category ("utility");
  set_apps (utility, (const char *[]) { "org.test.A", "org.test.B", NULL });
  g_list_store_append (cats, utility);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", "org.test.B", "org.test.C", NULL });
  g_list_store_append (cats, game);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  /* Rule A (no metadata here): smallest tab wins.
   * A -> trending (size 1), B -> utility (size 2), C -> game (size 3). */
  set = cz_category_primary_build_id_set (eng, trending);
  assert_set_equals (set, (const char *[]) { "org.test.A", NULL });

  set = cz_category_primary_build_id_set (eng, utility);
  assert_set_equals (set, (const char *[]) { "org.test.B", NULL });

  set = cz_category_primary_build_id_set (eng, game);
  assert_set_equals (set, (const char *[]) { "org.test.C", NULL });
}

static void
test_primary_tie_breaks_to_table_order (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  g_autoptr (BzFlathubCategory) trending = NULL;
  g_autoptr (CzCategoryPrimary) eng = NULL;
  g_autoptr (GHashTable) set = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  /* Both size 1: "game" precedes "trending" in cz_category_info[], so it
   * wins even though trending is earlier in the store. */
  trending = make_category ("trending");
  set_apps (trending, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, trending);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, game);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  set = cz_category_primary_build_id_set (eng, trending);
  assert_set_equals (set, (const char *[]) { NULL });

  set = cz_category_primary_build_id_set (eng, game);
  assert_set_equals (set, (const char *[]) { "org.test.A", NULL });
}

static void
test_primary_falls_back_for_hidden_tab (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  g_autoptr (BzFlathubCategory) trending = NULL;
  g_autoptr (BzFlathubCategory) popular = NULL;
  g_autoptr (CzCategoryPrimary) eng = NULL;
  g_autoptr (GHashTable) set = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = make_category ("trending");
  set_apps (trending, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, trending);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", "org.test.B", NULL });
  g_list_store_append (cats, game);

  /* Hidden tab: not part of the engine, must keep original membership. */
  popular = make_category ("popular");
  set_apps (popular, (const char *[]) { "org.test.H", NULL });
  g_list_store_append (cats, popular);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  set = cz_category_primary_build_id_set (eng, popular);
  assert_set_equals (set, (const char *[]) { "org.test.H", NULL });
}

static void
test_primary_invalidate_picks_up_model_changes (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  g_autoptr (BzFlathubCategory) trending = NULL;
  g_autoptr (CzCategoryPrimary) eng = NULL;
  g_autoptr (GHashTable) set = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = make_category ("trending");
  set_apps (trending, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, trending);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, game);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  /* Tie on first memo: game wins the tie (table order). */
  set = cz_category_primary_build_id_set (eng, trending);
  assert_set_equals (set, (const char *[]) { NULL });

  /* Grow "game": without invalidation the memo still resolves A -> game. */
  set_apps (game, (const char *[]) { "org.test.A", "org.test.X", NULL });
  set = cz_category_primary_build_id_set (eng, trending);
  assert_set_equals (set, (const char *[]) { NULL });

  /* After invalidation the memo rebuilds: trending (size 1) now beats
   * game (size 2), and X stays in game. */
  cz_category_primary_invalidate (eng);
  set = cz_category_primary_build_id_set (eng, trending);
  assert_set_equals (set, (const char *[]) { "org.test.A", NULL });

  set = cz_category_primary_build_id_set (eng, game);
  assert_set_equals (set, (const char *[]) { "org.test.X", NULL });
}

static void
test_primary_ref_keeps_engine_alive (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  CzCategoryPrimary *eng = NULL;
  CzCategoryPrimary *eng2 = NULL;
  g_autoptr (GHashTable) set = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, game);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  eng2 = cz_category_primary_ref (eng);
  g_assert_true (eng2 == eng);

  cz_category_primary_unref (eng);   /* one of two refs */

  set = cz_category_primary_build_id_set (eng2, game);
  assert_set_equals (set, (const char *[]) { "org.test.A", NULL });

  cz_category_primary_unref (eng2);  /* final ref */
}

static void
test_primary_resolve_tab_smallest_wins (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  g_autoptr (BzFlathubCategory) trending = NULL;
  g_autoptr (BzFlathubCategory) utility = NULL;
  g_autoptr (CzCategoryPrimary) eng = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = make_category ("trending");
  set_apps (trending, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, trending);

  utility = make_category ("utility");
  set_apps (utility, (const char *[]) { "org.test.A", "org.test.B", NULL });
  g_list_store_append (cats, utility);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", "org.test.B", "org.test.C", NULL });
  g_list_store_append (cats, game);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  /* Rule A: smallest tab wins. A -> trending (1), B -> utility (2),
   * C -> game (3). */
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.A"), ==, "trending");
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.B"), ==, "utility");
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.C"), ==, "game");
}

static void
test_primary_resolve_tab_tie_order_and_missing (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  g_autoptr (BzFlathubCategory) trending = NULL;
  g_autoptr (CzCategoryPrimary) eng = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = make_category ("trending");
  set_apps (trending, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, trending);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, game);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  /* Both size 1, table-order tie: game precedes trending. */
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.A"), ==, "game");

  /* App claimed by no visible tab resolves to NULL. */
  g_assert_null (cz_category_primary_resolve_tab (eng, "org.test.NoTab"));
}

static void
test_primary_resolve_tab_invalidate_refreshes (void)
{
  g_autoptr (GListStore) cats = NULL;
  g_autoptr (BzFlathubCategory) game = NULL;
  g_autoptr (BzFlathubCategory) trending = NULL;
  g_autoptr (CzCategoryPrimary) eng = NULL;

  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = make_category ("trending");
  set_apps (trending, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, trending);

  game = make_category ("game");
  set_apps (game, (const char *[]) { "org.test.A", NULL });
  g_list_store_append (cats, game);

  eng = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (eng);

  /* Tie on first memo: game wins table order. */
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.A"), ==, "game");

  /* Grow "game": stale memo still resolves A -> game. */
  set_apps (game, (const char *[]) { "org.test.A", "org.test.X", NULL });
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.A"), ==, "game");

  /* After invalidation, trending (size 1) beats game (size 2). */
  cz_category_primary_invalidate (eng);
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.A"), ==, "trending");
  g_assert_cmpstr (cz_category_primary_resolve_tab (eng, "org.test.X"), ==, "game");
}

static void
test_show_in_list_uncategorized_true (void)
{
  g_autoptr (BzFlathubCategory) cat = make_category ("uncategorized");

  g_assert_true (cz_category_get_show_in_list (cat));
}

static void
test_build_uncategorized_returns_unclaimed_zero_bit (void)
{
  g_autoptr (GListStore) cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  g_autoptr (GListStore) all  = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  g_autoptr (BzFlathubCategory) trending = make_category ("trending");
  g_autoptr (BzFlathubCategory) popular  = make_category ("popular");
  g_auto (GStrv) ids;
  const char *expect[] = { "org.test.Alpha", "org.test.Zulu", NULL };
  GStrv       p;

  set_apps (trending, (const char *[]){ "org.test.Claimed", NULL });
  set_apps (popular, (const char *[]){ "org.test.Taken", NULL });
  g_list_store_append (cats, trending);
  g_list_store_append (cats, popular);

  append_zero_group (all, "org.test.Alpha");    /* zero bits, unclaimed  */
  append_zero_group (all, "org.test.Zulu");     /* zero bits, unclaimed  */
  append_zero_group (all, "org.test.Claimed");  /* zero bits, but claimed */
  append_zero_group (all, "org.test.Taken");    /* zero bits, but claimed */

  ids = (GStrv) cz_category_build_uncategorized (G_LIST_MODEL (cats),
                                                 G_LIST_MODEL (all));
  g_assert_nonnull (ids);

  for (p = ids; *p != NULL; p++)
    g_assert_true (g_strv_contains (expect, *p));

  g_assert_cmpint (g_strv_length (ids), ==, 2);
}

static void
test_build_uncategorized_excludes_addon_groups (void)
{
  g_autoptr (GListStore) cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  g_autoptr (GListStore) all  = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  g_autoptr (BzFlathubCategory) trending = make_category ("trending");
  g_auto (GStrv) ids;
  const char *expect[] = { "org.test.Zulu", NULL };
  GStrv       p;

  set_apps (trending, (const char *[]){ "org.test.Claimed", NULL });
  g_list_store_append (cats, trending);

  append_addon_group (all, "org.test.Ext");    /* zero bits + addon -> skip */
  append_zero_group (all, "org.test.Zulu");               /* truly unclaimed */
  append_zero_group (all, "org.test.Claimed");            /* claimed  -> skip */

  ids = (GStrv) cz_category_build_uncategorized (G_LIST_MODEL (cats),
                                                 G_LIST_MODEL (all));
  g_assert_nonnull (ids);

  for (p = ids; *p != NULL; p++)
    g_assert_true (g_strv_contains (expect, *p));

  g_assert_cmpint (g_strv_length (ids), ==, 1);
}

static void
test_build_addons_returns_only_living_addons (void)
{
  g_autoptr (GListStore) all    = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  g_auto (GStrv) ids;
  const char *expect[] = { "org.test.Ext", NULL };
  GStrv       p;

  append_addon_group (all, "org.test.Ext");         /* addon, no EOL -> keep */
  append_eol_addon_group (all, "org.test.DeadExt"); /* addon EOL      -> skip */
  append_zero_group (all, "org.test.One");          /* plain app      -> skip */

  ids = (GStrv) cz_category_build_addons (G_LIST_MODEL (all));
  g_assert_nonnull (ids);

  for (p = ids; *p != NULL; p++)
    g_assert_true (g_strv_contains (expect, *p));

  g_assert_cmpint (g_strv_length (ids), ==, 1);
}

static void
test_primary_with_uncategorized_tab (void)
{
  g_autoptr (GListStore) cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  g_autoptr (GListStore) all  = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  g_autoptr (CzCategoryPrimary) primary = NULL;
  g_autoptr (BzFlathubCategory) trending = make_category ("trending");
  g_autoptr (BzFlathubCategory) mobile   = make_category ("mobile");
  g_autoptr (BzFlathubCategory) unknown  = make_category ("uncategorized");
  const char *expect_unknown[] = { "org.test.Alpha", NULL };
  const char *expect_trending[] = { "org.test.Beta", NULL };
  const char *expect_mobile[] = { "org.test.Gamma", NULL };

  set_apps (trending, (const char *[]){ "org.test.Beta", NULL });
  set_apps (mobile, (const char *[]){ "org.test.Gamma", NULL });
  set_apps (unknown, (const char *[]){ "org.test.Alpha", NULL });
  g_list_store_append (cats, trending);
  g_list_store_append (cats, mobile);
  g_list_store_append (cats, unknown);

  /* Engine feeds the uncategorized tab through its virtual membership, so
   * the groups list only matters to the appstream-tab path; zero-bit apps
   * are covered by the collections alone here. */
  append_zero_group (all, "org.test.Alpha");
  append_zero_group (all, "org.test.Beta");
  append_zero_group (all, "org.test.Gamma");

  primary = cz_category_primary_new (G_LIST_MODEL (cats));
  g_assert_nonnull (primary);

  {
    g_autoptr (GObject) item = g_list_model_get_item (G_LIST_MODEL (cats), 2);
    g_autoptr (GHashTable) set =
      cz_category_primary_build_id_set (primary, BZ_FLATHUB_CATEGORY (item));

    g_assert_nonnull (set);
    assert_set_equals (set, expect_unknown);
    g_assert_false (g_hash_table_contains (set, "org.test.Beta"));
    g_assert_false (g_hash_table_contains (set, "org.test.Gamma"));
  }

  {
    g_autoptr (GObject) item = g_list_model_get_item (G_LIST_MODEL (cats), 0);
    g_autoptr (GHashTable) set =
      cz_category_primary_build_id_set (primary, BZ_FLATHUB_CATEGORY (item));

    g_assert_nonnull (set);
    assert_set_equals (set, expect_trending);
  }

  {
    g_autoptr (GObject) item = g_list_model_get_item (G_LIST_MODEL (cats), 1);
    g_autoptr (GHashTable) set =
      cz_category_primary_build_id_set (primary, BZ_FLATHUB_CATEGORY (item));

    g_assert_nonnull (set);
    assert_set_equals (set, expect_mobile);
  }
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  install_test_state ();

  g_test_add_func ("/cz-custom-filter/show-in-list/known-true",
    test_show_in_list_returns_true_for_known_categories);
  g_test_add_func ("/cz-custom-filter/show-in-list/hidden-false",
    test_show_in_list_returns_false_for_hidden_categories);
  g_test_add_func ("/cz-custom-filter/show-in-list/unknown-false",
    test_show_in_list_returns_false_for_unknown_category);
  g_test_add_func ("/cz-custom-filter/show-in-list/uncategorized-true",
    test_show_in_list_uncategorized_true);
  g_test_add_func ("/cz-custom-filter/uncategorized/unclaimed-zero-bit",
    test_build_uncategorized_returns_unclaimed_zero_bit);
  g_test_add_func ("/cz-custom-filter/uncategorized/excludes-addon-groups",
    test_build_uncategorized_excludes_addon_groups);
  g_test_add_func ("/cz-custom-filter/addons/non-eol-only",
    test_build_addons_returns_only_living_addons);
  g_test_add_func ("/cz-custom-filter/primary/with-uncategorized-tab",
    test_primary_with_uncategorized_tab);

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

  g_test_add_func ("/cz-custom-filter/primary/null-model",
    test_primary_new_returns_null_on_null_model);
  g_test_add_func ("/cz-custom-filter/primary/no-visible-tabs",
    test_primary_new_returns_null_without_visible_tabs);
  g_test_add_func ("/cz-custom-filter/primary/dedupe-smallest-tab",
    test_primary_dedupes_to_smallest_tab);
  g_test_add_func ("/cz-custom-filter/primary/tie-break-table-order",
    test_primary_tie_breaks_to_table_order);
  g_test_add_func ("/cz-custom-filter/primary/fallback-hidden-tab",
    test_primary_falls_back_for_hidden_tab);
  g_test_add_func ("/cz-custom-filter/primary/invalidate-refreshes",
    test_primary_invalidate_picks_up_model_changes);
  g_test_add_func ("/cz-custom-filter/primary/ref-count",
    test_primary_ref_keeps_engine_alive);
  g_test_add_func ("/cz-custom-filter/primary/resolve-smallest-tab",
    test_primary_resolve_tab_smallest_wins);
  g_test_add_func ("/cz-custom-filter/primary/resolve-tie-order-and-missing",
    test_primary_resolve_tab_tie_order_and_missing);
  g_test_add_func ("/cz-custom-filter/primary/resolve-invalidate-refreshes",
    test_primary_resolve_tab_invalidate_refreshes);

  return g_test_run ();
}
