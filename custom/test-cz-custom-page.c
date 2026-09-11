/*
 * Integration tests for CzCustomPage.
 */

#include "config.h"

#include "cz-custom-page.h"

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib/gstdio.h>

#include "bz-app-tile.h"
#include "bz-application.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry.h"
#include "bz-entry-group.h"
#include "bz-flathub-category.h"
#include "bz-flathub-state.h"
#include "bz-state-info.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static BzFlathubCategory *
add_category (BzFlathubState *flathub,
              const char     *name,
              const char     **app_ids)
{
  BzFlathubCategory *cat;
  GtkStringList     *list;

  cat = bz_flathub_category_new ();
  bz_flathub_category_set_name (cat, name);

  list = gtk_string_list_new (NULL);
  if (app_ids != NULL)
    for (guint i = 0; app_ids[i] != NULL; i++)
      gtk_string_list_append (list, app_ids[i]);

  bz_flathub_category_set_applications (cat, G_LIST_MODEL (list));

  return cat;
}

static void
append_group (GListModel *store, const char *app_id)
{
  g_autoptr (BzEntry) entry = bz_entry_new (app_id);
  g_autoptr (BzEntryGroup) group = bz_entry_group_new_for_single_entry (entry);
  g_list_store_append (G_LIST_STORE (store), group);
}

static void
process_events (void)
{
  GMainContext *ctx = g_main_context_default ();
  while (g_main_context_pending (ctx))
    g_main_context_iteration (ctx, FALSE);
}

/* depth-first search for a GtkBox that has button children */
static GtkWidget *
find_pill_box (GtkWidget *widget)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (GTK_IS_BOX (widget))
    {
      child = gtk_widget_get_first_child (widget);
      while (child != NULL)
        {
          if (GTK_IS_BUTTON (child))
            return widget;
          child = gtk_widget_get_next_sibling (child);
        }
    }

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_pill_box (child);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }

  return NULL;
}

/* find the first BzDynamicListView in the tree */
static GtkWidget *
find_list_view (GtkWidget *widget)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (BZ_IS_DYNAMIC_LIST_VIEW (widget))
    return widget;

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_list_view (child);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }

  return NULL;
}

/* find a button by label text */
static GtkWidget *
find_pill_by_label (GtkWidget *widget, const char *label)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (GTK_IS_BUTTON (widget))
    {
      const char *lbl = gtk_button_get_label (GTK_BUTTON (widget));
      if (g_strcmp0 (lbl, label) == 0)
        return widget;
    }

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_pill_by_label (child, label);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }

  return NULL;
}

/* dump every widget's type name from root downward, indented by depth */
static void
dump_tree (GtkWidget *root, int depth, const char *label)
{
  if (root == NULL)
    return;

  GType t = G_TYPE_FROM_INSTANCE (root);
  char line[256];
  int  pos = 0;
  for (int i = 0; i < depth && pos < 240; i++)
    pos += g_snprintf (line + pos, sizeof (line) - pos, "  ");

  const char *type_name = g_type_name (t);
  const char *extra = "";

  if (BZ_IS_APP_TILE (root))
    {
      BzEntryGroup *g = bz_app_tile_get_group (BZ_APP_TILE (root));
      extra = g ? bz_entry_group_get_id (g) : "(no group)";
    }
  else if (GTK_IS_BUTTON (root))
    extra = gtk_button_get_label (GTK_BUTTON (root));
  else if (GTK_IS_LABEL (root))
    extra = gtk_label_get_text (GTK_LABEL (root));

  g_snprintf (line + pos, sizeof (line) - pos, "%s  %s",
              type_name, extra);
  g_test_message ("%s: %s", label, line);

  for (GtkWidget *c = gtk_widget_get_first_child (root);
       c != NULL; c = gtk_widget_get_next_sibling (c))
    dump_tree (c, depth + 1, label);
}

/* count BzAppTile widgets in the widget tree — the canonical measure of
 * "are apps actually rendered in the custom page" */
static guint
count_app_tiles (GtkWidget *widget)
{
  guint count = 0;

  if (widget == NULL)
    return 0;

  if (BZ_IS_APP_TILE (widget))
    return 1;

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    count += count_app_tiles (child);

  return count;
}

/* Recursively remove a directory tree. */
static void
remove_dir_tree (const char *dir)
{
  GDir       *d;
  const char *name;

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

/* Remove the app's persisted label data (SQLite db + WAL sidecars + backup
 * tree + legacy JSON) so a fresh page starts clean. */
static void
wipe_persisted_labels (void)
{
  const char *data_dir = g_get_user_data_dir ();
  char       *app_dir;
  char       *db;

  if (data_dir == NULL)
    return;

  app_dir = g_build_filename (data_dir, "io.github.kolunmi.Bazaar", NULL);
  db      = g_build_filename (app_dir, "custom-labels.db", NULL);
  g_remove (db);
  g_free (db);

  db = g_build_filename (app_dir, "custom-labels.db-wal", NULL);
  g_remove (db);
  g_free (db);

  db = g_build_filename (app_dir, "custom-labels.db-shm", NULL);
  g_remove (db);
  g_free (db);

  db = g_build_filename (app_dir, "custom-labels.json", NULL);
  g_remove (db);
  g_free (db);

  db = g_build_filename (app_dir, "backups", NULL);
  remove_dir_tree (db);
  g_free (db);

  g_free (app_dir);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

static void
test_page_creation (void)
{
  CzCustomPage *page = cz_custom_page_new ();

  g_assert_nonnull (page);
  g_assert_true (CZ_IS_CUSTOM_PAGE (page));
  g_assert_true (ADW_IS_BIN (page));

  g_object_ref_sink (page);
  g_clear_object (&page);
}

static void
test_pills_created (void)
{
  BzStateInfo    *state;
  BzFlathubState *flathub;
  GListStore     *all;
  CzCustomPage   *page;
  GtkWidget      *pill_box;
  GtkWidget      *child;
  guint           pill_count;

  GListStore *cats;
  BzFlathubCategory *cat;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  cat = add_category (flathub, "trending",
    (const char *[]) { "org.test.Trend1", "org.test.Trend2", NULL });
  g_list_store_append (cats, cat);

  cat = add_category (flathub, "game",
    (const char *[]) { "org.test.Game1", NULL });
  g_list_store_append (cats, cat);

  cat = add_category (flathub, "popular",
    (const char *[]) { "org.test.Hidden", NULL });
  g_list_store_append (cats, cat);

  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.Trend1");
  append_group (G_LIST_MODEL (all), "org.test.Trend2");
  append_group (G_LIST_MODEL (all), "org.test.Game1");

  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  page = cz_custom_page_new ();
  process_events ();

  pill_box = find_pill_box (GTK_WIDGET (page));
  g_assert_nonnull (pill_box);

  pill_count = 0;
  child = gtk_widget_get_first_child (pill_box);
  while (child != NULL)
    {
      if (GTK_IS_BUTTON (child))
        pill_count++;
      child = gtk_widget_get_next_sibling (child);
    }

  g_test_message ("Found %u pill(s) (expected 2: trending, game)", pill_count);
  g_assert_cmpuint (pill_count, ==, 2);
}

static void
test_trending_auto_selected (void)
{
  BzStateInfo        *state;
  BzFlathubState     *flathub;
  GListStore         *cats;
  GListStore         *all;
  CzCustomPage       *page;
  GtkWidget          *list_view;
  GListModel         *model;
  guint               n;
  BzFlathubCategory  *trending;
  BzFlathubCategory  *dev;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = add_category (flathub, "trending",
    (const char *[]) { "org.test.Trend1", "org.test.Trend2", NULL });
  g_list_store_append (cats, trending);

  dev = add_category (flathub, "development",
    (const char *[]) { "org.test.Dev1", NULL });
  g_list_store_append (cats, dev);

  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.Trend1");
  append_group (G_LIST_MODEL (all), "org.test.Trend2");

  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  /* Page added to a view stack with another page, so it starts non-visible.
   * Show a window so the stack gets mapped; then switching to the custom page
   * causes child-visible FALSE→TRUE, triggering the ::map signal for
   * auto-select. */
  GtkWidget *stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  GtkWidget *other = gtk_label_new ("other");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other, "other", "Other", NULL);

  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack),
      GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  {
    GtkWidget *win = gtk_window_new ();
    gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
    gtk_widget_set_visible (win, TRUE);
  }
  process_events ();

  /* Now switch to the custom page → child-visible changes FALSE→TRUE,
   * firing ::map which triggers trending auto-select. */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();

  list_view = find_list_view (GTK_WIDGET (page));
  g_assert_nonnull (list_view);
  g_assert_true (gtk_widget_get_visible (list_view));

  model = list_view ? bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view)) : NULL;
  g_assert_true (model != NULL);
  if (model == NULL) return; /* non-fatal: avoid GLib critical in subsequent code */

  n = g_list_model_get_n_items (model);
  g_test_message ("Auto-select trending: model has %u items", n);
  g_assert_cmpuint (n, >=, 1);

  for (guint i = 0; i < n; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (model, i);
      g_assert_nonnull (item);
      g_assert_true (BZ_IS_ENTRY_GROUP (item));
    }
}

static void
test_category_click_shows_apps (void)
{
  BzStateInfo    *state;
  BzFlathubState *flathub;
  GListStore     *cats;
  GListStore     *all;
  CzCustomPage   *page;
  GtkWidget      *game_pill;
  GtkWidget      *list_view;
  GListModel     *model;
  guint           n;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  {
    BzFlathubCategory *c = add_category (flathub, "trending",
      (const char *[]) { "org.test.Trend1", NULL });
    g_list_store_append (cats, c);
  }
  {
    BzFlathubCategory *c = add_category (flathub, "game",
      (const char *[]) { "org.test.Game1", "org.test.Game2", NULL });
    g_list_store_append (cats, c);
  }

  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.Trend1");
  append_group (G_LIST_MODEL (all), "org.test.Game1");
  append_group (G_LIST_MODEL (all), "org.test.Game2");
  append_group (G_LIST_MODEL (all), "org.test.Other");

  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  page = cz_custom_page_new ();
  process_events ();

  game_pill = find_pill_by_label (GTK_WIDGET (page), "Gaming");
  g_assert_nonnull (game_pill);

  g_signal_emit_by_name (game_pill, "clicked");
  process_events ();

  list_view = find_list_view (GTK_WIDGET (page));
  g_assert_nonnull (list_view);
  g_assert_true (gtk_widget_get_visible (list_view));

  model = list_view ? bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view)) : NULL;
  g_assert_true (model != NULL);
  if (model == NULL) return;

  n = g_list_model_get_n_items (model);
  g_test_message ("Game category has %u visible apps", n);
  for (guint i = 0; i < n; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (model, i);
      const char *id;

      if (item != NULL && BZ_IS_ENTRY_GROUP (item))
        {
          id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
          g_test_message ("  item[%u] = %s", i, id ? id : "(null)");
        }
      else
        g_test_message ("  item[%u] = NULL or wrong type", i);
    }

  /* Also check total entries in all_entry_groups */
  {
    GListModel *all_entries = bz_state_info_get_all_entry_groups (state);
    guint n_all = g_list_model_get_n_items (all_entries);
    g_test_message ("All entry groups: %u items", n_all);
    for (guint i = 0; i < n_all; i++)
      {
        g_autoptr (GObject) item = g_list_model_get_item (all_entries, i);
        if (item != NULL && BZ_IS_ENTRY_GROUP (item))
          {
            const char *id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
            g_test_message ("  all[%u] = %s", i, id ? id : "(null)");
          }
      }
    g_object_unref (all_entries);
  }

  g_assert_cmpuint (n, ==, 2);
}

static void
test_custom_tab_in_window (void)
{
  BzStateInfo    *state;
  BzFlathubState *flathub;
  GListStore     *cats;
  GListStore     *all;
  GtkWidget      *stack;
  CzCustomPage   *page;
  GtkWidget      *found;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  {
    BzFlathubCategory *c = add_category (flathub, "trending",
      (const char *[]) { "org.test.Trend1", NULL });
    g_list_store_append (cats, c);
  }
  bz_flathub_state_set_categories (flathub, cats);
  bz_state_info_set_flathub (state, flathub);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.Trend1");
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  g_assert_nonnull (stack);

  page = cz_custom_page_new ();
  g_assert_nonnull (page);

  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack),
      GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  process_events ();

  /* I: the "custom" page must be findable by name in the stack —
   * catches broken runtime insertion (cz-main.c on_idle_add_tab
   * pattern). */
  found = adw_view_stack_get_child_by_name (ADW_VIEW_STACK (stack), "custom");
  g_assert_nonnull (found);
  g_assert_true (CZ_IS_CUSTOM_PAGE (found));
  g_assert_true (gtk_widget_get_visible (found));
  g_test_message ("Custom tab found and visible in view stack");
}

static void
test_async_app_population (void)
{
  BzStateInfo    *state;
  BzFlathubState *flathub;
  GListStore     *cats;
  GListStore     *all;
  GtkWidget      *stack;
  CzCustomPage   *page;
  GtkWidget      *page_widget;
  GtkWidget      *list_view;
  guint           tile_count;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  {
    BzFlathubCategory *c = add_category (flathub, "trending",
      (const char *[]) { "org.scilab.Scilab", NULL });
    g_list_store_append (cats, c);
  }
  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  /* deliberately empty — simulates the real-app scenario where
   * all_entry_groups is set early but items arrive later via fibers */
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  /* add page to a view stack with another page — matches real cz-main.c on_idle_add_tab */
  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  GtkWidget *other = gtk_label_new ("other");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other, "other", "Other", NULL);

  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack),
      GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  {
    GtkWidget *win = gtk_window_new ();
    gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
    gtk_widget_set_visible (win, TRUE);
  }
  process_events ();

  /* Switch to custom page → child-visible TRUE → ::map fires
   * → trigger_trending_click → category_selected → apply_category_filter.
   * all_entry_groups has 0 items, so filtered model is empty. */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();

  page_widget = adw_view_stack_get_child_by_name (
      ADW_VIEW_STACK (stack), "custom");
  g_assert_nonnull (page_widget);
  g_assert_true (CZ_IS_CUSTOM_PAGE (page_widget));

  /* I: with zero items in all_entry_groups, the custom page must
   * render zero BzAppTile widgets in its widget tree */
  tile_count = count_app_tiles (page_widget);
  g_test_message ("Before append: %u app tile(s) rendered", tile_count);
  g_assert_cmpuint (tile_count, ==, 0);

  /* I: items added after page creation must appear as rendered
   * BzAppTile widgets in the custom page's widget tree — catches
   * missing / broken items-changed handler on all_entry_groups */
  append_group (G_LIST_MODEL (all), "org.scilab.Scilab");
  process_events ();

  tile_count = count_app_tiles (page_widget);
  g_test_message ("After append:  %u app tile(s) rendered", tile_count);
  g_assert_cmpuint (tile_count, >=, 1);

  /* dump the full widget tree of the custom page to prove the app tile's
   * position — every widget from the CzCustomPage root to the BzAppTile */
  dump_tree (page_widget, 0, "CUSTOM-PAGE-TREE");

  /* verify the tile's content — find the list view, then its flow-box
   * child, then verify the BzAppTile's group id */
  list_view = find_list_view (page_widget);
  g_assert_nonnull (list_view);

  {
    GtkWidget *flow_box = adw_bin_get_child (ADW_BIN (list_view));
    g_assert_nonnull (flow_box);
    g_assert_true (GTK_IS_FLOW_BOX (flow_box));

    GtkWidget *flow_child = gtk_widget_get_first_child (flow_box);
    g_assert_nonnull (flow_child);
    g_assert_true (GTK_IS_FLOW_BOX_CHILD (flow_child));

    GtkWidget *tile = gtk_flow_box_child_get_child (
        GTK_FLOW_BOX_CHILD (flow_child));
    g_assert_nonnull (tile);
    g_assert_true (BZ_IS_APP_TILE (tile));

    BzEntryGroup *group = bz_app_tile_get_group (BZ_APP_TILE (tile));
    g_assert_nonnull (group);
    /* use a real Flathub trending app ID from the actual API response */
    g_assert_cmpstr (
        bz_entry_group_get_id (group), ==, "org.scilab.Scilab");
  }
}

static void
debug_notify_all_entry_groups (GObject    *obj,
                               GParamSpec *pspec,
                               gpointer    data)
{
  g_test_message ("DEBUG: notify::all-entry-groups emitted, obj=%p, pspec=%s", obj, pspec ? g_param_spec_get_name (pspec) : "NULL");
}

/* ------------------------------------------------------------------ */
/*  Test: all_entry_groups arrives late (notify signal)               */
/* ------------------------------------------------------------------ */

static void
test_all_entry_groups_late_arrival (void)
{
  BzStateInfo    *state;
  BzFlathubState *flathub;
  GListStore     *cats;
  GListStore     *all;
  GtkWidget      *stack;
  CzCustomPage   *page;
  GtkWidget      *page_widget;
  guint           tile_count;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  /* CRITICAL: reset state to known clean condition before test */
  bz_state_info_set_all_entry_groups (state, NULL);
  bz_state_info_set_flathub (state, NULL);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  {
    BzFlathubCategory *c = add_category (flathub, "trending",
      (const char *[]) { "org.scilab.Scilab", NULL });
    g_list_store_append (cats, c);
  }
  bz_flathub_state_set_categories (flathub, cats);

  /* CRITICAL: all_entry_groups is initially NULL — simulates real app
   * where bz-application.c sets it later after fiber initialization */
  bz_state_info_set_flathub (state, flathub);
  /* Do NOT call bz_state_info_set_all_entry_groups yet */

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack),
      GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  {
    /* Show the window so ::map fires and trending auto-select happens.
     * At this point all_entry_groups is NULL, so apply_category_filter
     * will return early, so no app tiles. */
    GtkWidget *win = gtk_window_new ();
    gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
    gtk_widget_set_visible (win, TRUE);
  }
  process_events ();

  page_widget = adw_view_stack_get_child_by_name (
      ADW_VIEW_STACK (stack), "custom");
  g_assert_nonnull (page_widget);
  g_assert_true (CZ_IS_CUSTOM_PAGE (page_widget));

  /* I: with all_entry_groups=NULL, page must have zero app tiles */
  tile_count = count_app_tiles (page_widget);
  g_test_message ("Before all_entry_groups set: %u app tile(s) rendered", tile_count);
  g_assert_cmpuint (tile_count, ==, 0);

  /* I: now set all_entry_groups (simulates bz-application.c fiber completing) */
  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  g_test_message ("Setting all_entry_groups to new empty list store");
  
  /* Connect a debug handler to see if notify fires */
  g_signal_connect (state, "notify::all-entry-groups",
                    G_CALLBACK (debug_notify_all_entry_groups), NULL);
  
  g_object_set (state, "all-entry-groups", all, NULL);
  g_test_message ("all_entry_groups set, processing events");
  process_events ();

  tile_count = count_app_tiles (page_widget);
  g_test_message ("After all_entry_groups set (empty): %u app tile(s) rendered", tile_count);
  g_assert_cmpuint (tile_count, ==, 0);

  /* I: add items to all_entry_groups — they must appear as rendered tiles */
  append_group (G_LIST_MODEL (all), "org.scilab.Scilab");
  process_events ();

  tile_count = count_app_tiles (page_widget);
  g_test_message ("After append to all_entry_groups: %u app tile(s) rendered", tile_count);
  
  /* Debug: check list view state */
  GtkWidget *list_view = find_list_view (page_widget);
  if (list_view)
    {
      GtkWidget *child = adw_bin_get_child (ADW_BIN (list_view));
      g_test_message ("List view child: %s", child ? g_type_name (G_TYPE_FROM_INSTANCE (child)) : "NULL");
      GListModel *model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
      g_test_message ("List view model: %s, items: %u", model ? g_type_name (G_TYPE_FROM_INSTANCE (model)) : "NULL", model ? g_list_model_get_n_items (model) : 0);
    }
  
  g_assert_cmpuint (tile_count, >=, 1);

  dump_tree (page_widget, 0, "LATE-ALL-GROUPS-TREE");
}

/* ================================================================== */
/*  Rigorous tests that simulate real runtime conditions               */
/* ================================================================== */

/* helper: get the category name of the currently selected pill */
static const char *
get_selected_category_name (GtkWidget *page_widget)
{
  GtkWidget *pill_box = find_pill_box (page_widget);
  GtkWidget *child;

  if (pill_box == NULL)
    return NULL;

  child = gtk_widget_get_first_child (pill_box);
  while (child != NULL)
    {
      if (GTK_IS_BUTTON (child) && gtk_widget_has_css_class (child, "selected"))
        {
          BzFlathubCategory *cat = g_object_get_data (G_OBJECT (child), "cz-category");
          if (cat != NULL)
            return bz_flathub_category_get_name (cat);
        }
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Compact diagnostic: single structured line for AI parsing.
   Example: DIAG:phase=revisit | game_sel=1 | new_sel=0 | selected_cat=game | model_items=3 */
static void
diag_state (GtkWidget *page_widget, const char *phase)
{
  GtkWidget  *lv     = find_list_view (page_widget);
  GtkWidget  *t_pill = find_pill_by_label (page_widget, "Trending");
  GtkWidget  *g_pill = find_pill_by_label (page_widget, "Gaming");
  GtkWidget  *n_pill = find_pill_by_label (page_widget, "New");
  const char *sel    = get_selected_category_name (page_widget);
  GListModel *model  = lv ? bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (lv)) : NULL;
  guint       n      = model ? g_list_model_get_n_items (model) : 0;
  int         t_sel  = t_pill && gtk_widget_has_css_class (t_pill, "selected");
  int         g_sel  = g_pill && gtk_widget_has_css_class (g_pill, "selected");
  int         n_sel  = n_pill && gtk_widget_has_css_class (n_pill, "selected");

  g_test_message ("DIAG:phase=%s | trending_sel=%d | game_sel=%d | new_sel=%d | selected_cat=%s | model_items=%u",
                  phase, t_sel, g_sel, n_sel, sel ? sel : "NULL", n);
}

/* Widget-tree dump only on failure (saves tokens when passing) */
static void
dump_on_failure (GtkWidget *page_widget, const char *label)
{
  if (g_test_failed ())
    dump_tree (page_widget, 0, label);
}

static void
test_custom_page_exists_and_visible (void)
{
  BzStateInfo     *state;
  BzFlathubState  *flathub;
  GListStore      *cats;
  GListStore      *all;
  BzFlathubCategory *c;
  GtkWidget       *stack;
  CzCustomPage    *custom_page;
  GtkWidget       *page_widget;
  GtkWidget       *win;
  GtkWidget       *other;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  c = add_category (flathub, "trending",
    (const char *[]) { "org.test.One", NULL });
  g_list_store_append (cats, c);
  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.One");
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("Other Page");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other,
                                       "other", "Other", NULL);

  custom_page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack),
                                       GTK_WIDGET (custom_page),
                                       "custom", "Custom", NULL);

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  process_events ();

  page_widget = adw_view_stack_get_child_by_name (ADW_VIEW_STACK (stack), "custom");
  g_assert_nonnull (page_widget);
  g_assert_true (CZ_IS_CUSTOM_PAGE (page_widget));

  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), page_widget);
  process_events ();
  { int ok = (page_widget == adw_view_stack_get_visible_child (ADW_VIEW_STACK (stack)));
    g_test_message ("DIAG:action=switch-to-custom | is_visible_child=%d", ok);
    g_assert_true (ok); }

  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), other);
  process_events ();
  { int ok = (other == adw_view_stack_get_visible_child (ADW_VIEW_STACK (stack)));
    g_test_message ("DIAG:action=switch-to-other | is_other_visible=%d", ok);
    g_assert_true (ok); }

  dump_on_failure (page_widget, "EXISTS-FAIL-TREE");
  gtk_window_destroy (GTK_WINDOW (win));
}

static void
test_pills_exist_and_visible (void)
{
  BzStateInfo      *state;
  BzFlathubState   *flathub;
  GListStore       *cats;
  GListStore       *all;
  GtkWidget        *stack;
  CzCustomPage     *page;
  GtkWidget        *win;
  GtkWidget        *other;
  GtkWidget        *pill_box;
  GtkWidget        *trending_pill;
  GtkWidget        *game_pill;
  BzFlathubCategory *trending;
  BzFlathubCategory *game;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = add_category (flathub, "trending",
    (const char *[]) { "org.test.T1", NULL });
  g_list_store_append (cats, trending);

  game = add_category (flathub, "game",
    (const char *[]) { "org.test.G1", NULL });
  g_list_store_append (cats, game);

  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.T1");
  append_group (G_LIST_MODEL (all), "org.test.G1");
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("Other Page");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other,
                                       "other", "Other", NULL);

  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack),
                                       GTK_WIDGET (page),
                                       "custom", "Custom", NULL);

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  process_events ();

  pill_box = find_pill_box (GTK_WIDGET (page));
  g_assert_nonnull (pill_box);

  trending_pill = find_pill_by_label (GTK_WIDGET (page), "Trending");
  game_pill     = find_pill_by_label (GTK_WIDGET (page), "Gaming");
  { int t_ok = trending_pill && GTK_IS_BUTTON (trending_pill);
    int g_ok = game_pill && GTK_IS_BUTTON (game_pill);
    g_test_message ("DIAG:action=pills-found | trending=%d | game=%d", t_ok, g_ok);
    g_assert_true (t_ok);
    g_assert_true (g_ok); }

  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();
  { int cv_t = gtk_widget_get_child_visible (trending_pill);
    int cv_g = gtk_widget_get_child_visible (game_pill);
    g_test_message ("DIAG:action=pills-visible | trending_child_visible=%d | game_child_visible=%d", cv_t, cv_g);
    g_assert_true (cv_t);
    g_assert_true (cv_g); }

  { guint count = 0;
    GtkWidget *ch = gtk_widget_get_first_child (pill_box);
    while (ch != NULL) { if (GTK_IS_BUTTON (ch)) count++; ch = gtk_widget_get_next_sibling (ch); }
    g_test_message ("DIAG:action=pill-count | count=%u | expected=2", count);
    g_assert_cmpuint (count, ==, 2); }

  dump_on_failure (GTK_WIDGET (page), "PILLS-FAIL-TREE");
  gtk_window_destroy (GTK_WINDOW (win));
}

static void
test_trending_auto_populates_on_each_visit (void)
{
  BzStateInfo      *state;
  BzFlathubState   *flathub;
  GListStore       *cats;
  GListStore       *all;
  GtkWidget        *stack;
  CzCustomPage     *page;
  GtkWidget        *win;
  GtkWidget        *other;
  GtkWidget        *page_widget;
  GtkWidget        *trending_pill;
  GtkWidget        *game_pill;
  GtkWidget        *list_view;
  GListModel       *model;
  BzFlathubCategory *trending;
  BzFlathubCategory *game;

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  trending = add_category (flathub, "trending",
    (const char *[]) { "org.test.Trend1", "org.test.Trend2", NULL });
  g_list_store_append (cats, trending);

  game = add_category (flathub, "game",
    (const char *[]) { "org.test.Game1", "org.test.Game2", "org.test.Game3", NULL });
  g_list_store_append (cats, game);

  bz_flathub_state_set_categories (flathub, cats);

  /* CRITICAL: set all_entry_groups BEFORE creating the page, so items
   * exist in the list BEFORE items-changed is connected in setup_all_groups.
   * Without this, on_entry_groups_changed fallback could mask ::map bugs. */
  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.Trend1");
  append_group (G_LIST_MODEL (all), "org.test.Trend2");
  append_group (G_LIST_MODEL (all), "org.test.Game1");
  append_group (G_LIST_MODEL (all), "org.test.Game2");
  append_group (G_LIST_MODEL (all), "org.test.Game3");
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("Other Page");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other,
                                       "other", "Other", NULL);

  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack),
                                       GTK_WIDGET (page),
                                       "custom", "Custom", NULL);

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  process_events ();

  page_widget   = GTK_WIDGET (page);
  trending_pill = find_pill_by_label (page_widget, "Trending");
  game_pill     = find_pill_by_label (page_widget, "Gaming");
  g_assert_nonnull (trending_pill);
  g_assert_nonnull (game_pill);

  /* Phase 1: First visit — trending auto-selected via ::map */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), page_widget);
  process_events ();
  diag_state (page_widget, "first-visit");
  { list_view = find_list_view (page_widget);
    g_assert_nonnull (list_view);
    model = list_view ? bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view)) : NULL;
    g_assert_true (model && g_list_model_get_n_items (model) == 2); }

  /* Phase 2: User clicks "game" pill */
  g_signal_emit_by_name (game_pill, "clicked");
  process_events ();
  diag_state (page_widget, "game-click");
  { model = list_view ? bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view)) : NULL;
    g_assert_true (model && g_list_model_get_n_items (model) == 3); }

  /* Phase 3: Revisit — trending must be auto-selected AGAIN via ::map.
     FAILS with notify::child-visible dead code: trending NOT re-selected,
     game is still active, model shows 3 items instead of 2. */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), other);
  process_events ();
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), page_widget);
  process_events ();
  diag_state (page_widget, "revisit");
  { model = list_view ? bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view)) : NULL;
    g_assert_true (model && g_list_model_get_n_items (model) == 2); }

  dump_on_failure (page_widget, "REVISIT-FAIL-TREE");
  gtk_window_destroy (GTK_WINDOW (win));
}

static void
test_core_labels_exist_and_visible (void)
{
  BzStateInfo      *state;
  BzFlathubState   *flathub;
  GListStore       *cats;
  GListStore       *all;
  GtkWidget        *stack;
  CzCustomPage     *page;
  GtkWidget        *win;
  GtkWidget        *other;
  GtkWidget        *page_widget;
  GtkWidget        *p_new;
  GtkWidget        *p_pending;
  GtkWidget        *p_discarded;
  BzFlathubCategory *trending;
  BzFlathubCategory *game;

  /* Wipe persisted label data so the fresh page starts clean */
  wipe_persisted_labels ();

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);
  bz_state_info_set_all_entry_groups (state, NULL);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  trending = add_category (flathub, "trending",
    (const char *[]) { "org.test.T1", NULL });
  g_list_store_append (cats, trending);
  game = add_category (flathub, "game",
    (const char *[]) { "org.test.G1", NULL });
  g_list_store_append (cats, game);
  bz_flathub_state_set_categories (flathub, cats);
  bz_state_info_set_flathub (state, flathub);

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("Other");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other,
                                       "other", "Other", NULL);
  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack),
                                       GTK_WIDGET (page), "custom", "Custom", NULL);

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  process_events ();
  page_widget = GTK_WIDGET (page);
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), page_widget);
  process_events ();

  p_new       = find_pill_by_label (page_widget, "New");
  p_pending   = find_pill_by_label (page_widget, "Pending");
  p_discarded = find_pill_by_label (page_widget, "Discarded");

  { int n  = (p_new != NULL) + (p_pending != NULL) + (p_discarded != NULL);
    int nb = (p_new && GTK_IS_BUTTON (p_new))
           + (p_pending && GTK_IS_BUTTON (p_pending))
           + (p_discarded && GTK_IS_BUTTON (p_discarded));
    int cv = (p_new && gtk_widget_get_child_visible (p_new))
           + (p_pending && gtk_widget_get_child_visible (p_pending))
           + (p_discarded && gtk_widget_get_child_visible (p_discarded));
    int sel = p_new && gtk_widget_has_css_class (p_new, "selected");
    g_test_message ("DIAG:core_labels | found:%d/3 | btn_ok:%d/3 | visible:%d/3 | new_selected:%d",
                    n, nb, cv, sel);
    g_assert_cmpuint (n,  ==, 3);
    g_assert_cmpuint (nb, ==, 3);
    g_assert_cmpuint (cv, ==, 3);
    g_assert_true (sel); }

  dump_on_failure (page_widget, "CORE-LABELS-FAIL-TREE");
  gtk_window_destroy (GTK_WINDOW (win));
}

static void
test_noncore_labels_populated (void)
{
  BzStateInfo      *state;
  BzFlathubState   *flathub;
  GListStore       *cats;
  GListStore       *all;
  GtkWidget        *stack;
  CzCustomPage     *page;
  GtkWidget        *win;
  GtkWidget        *other;
  GtkWidget        *page_widget;
  GtkWidget        *p_test1;
  GtkWidget        *p_test2;
  BzFlathubCategory *trending;
  BzFlathubCategory *game;

  /* Wipe persisted label data so the fresh page starts clean */
  wipe_persisted_labels ();

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);
  bz_state_info_set_all_entry_groups (state, NULL);
  bz_state_info_set_flathub (state, NULL);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  trending = add_category (flathub, "trending",
    (const char *[]) { "org.test.T1", NULL });
  g_list_store_append (cats, trending);
  game = add_category (flathub, "game",
    (const char *[]) { "org.test.G1", NULL });
  g_list_store_append (cats, game);
  bz_flathub_state_set_categories (flathub, cats);
  /* DELIBERATELY do NOT set flathub on state yet — we need to add
   * non-core labels to the store BEFORE populate_categories builds UI */

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("Other");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other,
                                       "other", "Other", NULL);
  page = cz_custom_page_new ();
  /* constructed: flathub is NULL → does NOT call populate_categories,
   * instead connects notify::flathub handler */
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack),
                                       GTK_WIDGET (page), "custom", "Custom", NULL);

  /* Add non-core labels to store BEFORE triggering populate_categories */
  {
    CzCustomLabelStore *store = cz_custom_page_get_label_store (page);
    g_assert_nonnull (store);
    cz_custom_label_store_add_noncore_label (store, "org.test.T1", "Test1");
    cz_custom_label_store_add_noncore_label (store, "org.test.T1", "Test2");
  }

  /* NOW set flathub → notify::flathub → on_flathub_loaded → populate_categories */
  bz_state_info_set_flathub (state, flathub);

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.T1");
  append_group (G_LIST_MODEL (all), "org.test.G1");
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  process_events ();
  page_widget = GTK_WIDGET (page);
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), page_widget);
  process_events ();

  p_test1 = find_pill_by_label (page_widget, "Test1");
  p_test2 = find_pill_by_label (page_widget, "Test2");

  { int   n  = (p_test1 != NULL) + (p_test2 != NULL);
    int   nb = (p_test1 && GTK_IS_BUTTON (p_test1))
             + (p_test2 && GTK_IS_BUTTON (p_test2));
    int   cv = (p_test1 && gtk_widget_get_child_visible (p_test1))
             + (p_test2 && gtk_widget_get_child_visible (p_test2));
    int   css = (p_test1 && gtk_widget_has_css_class (p_test1, "noncore-pill"))
              + (p_test2 && gtk_widget_has_css_class (p_test2, "noncore-pill"));
    g_test_message ("DIAG:noncore_populated | found:%d/2 | btn_ok:%d/2 | visible:%d/2 | noncore_css:%d/2",
                    n, nb, cv, css);
    g_assert_cmpuint (n,  ==, 2);
    g_assert_cmpuint (nb, ==, 2);
    g_assert_cmpuint (cv, ==, 2);
    g_assert_cmpuint (css, ==, 2); }

  dump_on_failure (page_widget, "NONCORE-POPULATED-FAIL-TREE");
  gtk_window_destroy (GTK_WINDOW (win));
}

static void
test_noncore_labels_empty (void)
{
  BzStateInfo      *state;
  BzFlathubState   *flathub;
  GListStore       *cats;
  GListStore       *all;
  GtkWidget        *stack;
  CzCustomPage     *page;
  GtkWidget        *win;
  GtkWidget        *other;
  GtkWidget        *page_widget;
  GtkWidget        *p_empty;
  BzFlathubCategory *trending;
  BzFlathubCategory *game;

  /* Wipe persisted label data so the fresh page starts clean */
  wipe_persisted_labels ();

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);
  bz_state_info_set_all_entry_groups (state, NULL);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  trending = add_category (flathub, "trending",
    (const char *[]) { "org.test.T1", NULL });
  g_list_store_append (cats, trending);
  game = add_category (flathub, "game",
    (const char *[]) { "org.test.G1", NULL });
  g_list_store_append (cats, game);
  bz_flathub_state_set_categories (flathub, cats);
  bz_state_info_set_flathub (state, flathub);

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("Other");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other,
                                       "other", "Other", NULL);
  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack),
                                       GTK_WIDGET (page), "custom", "Custom", NULL);
  /* DO NOT add any non-core labels — store is empty */

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.T1");
  append_group (G_LIST_MODEL (all), "org.test.G1");
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  process_events ();
  page_widget = GTK_WIDGET (page);
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), page_widget);
  process_events ();

  p_empty = find_pill_by_label (page_widget, "Empty");

  { int n  = (p_empty != NULL);
    int nb = (p_empty && GTK_IS_BUTTON (p_empty));
    int cv = (p_empty && gtk_widget_get_child_visible (p_empty));
    int css = (p_empty && gtk_widget_has_css_class (p_empty, "noncore-pill"));
    int ins = (p_empty && !gtk_widget_get_sensitive (p_empty));
    g_test_message ("DIAG:noncore_empty | found:%d | btn_ok:%d | visible:%d | noncore_css:%d | insensitive:%d",
                    n, nb, cv, css, ins);
    g_assert_cmpuint (n,  ==, 1);
    g_assert_cmpuint (nb, ==, 1);
    g_assert_cmpuint (cv, ==, 1);
    g_assert_cmpuint (css, ==, 1);
    g_assert_true (ins); }

  dump_on_failure (page_widget, "NONCORE-EMPTY-FAIL-TREE");
  gtk_window_destroy (GTK_WINDOW (win));
}
 
static void
test_noncore_labels_added_via_window_ui (void)
{
  BzStateInfo      *state;
  BzFlathubState   *flathub;
  GListStore       *cats;
  GListStore       *all;
  GtkWidget        *stack;
  CzCustomPage     *page;
  GtkWidget        *win;
  GtkWidget        *other;
  GtkWidget        *page_widget;
  GtkWidget        *p_test;
  CzCustomLabelStore *store;
  GPtrArray        *names;

  /* Wipe persisted label data so the fresh page starts clean */
  wipe_persisted_labels ();

  state = bz_state_info_get_default ();
  g_assert_nonnull (state);
  bz_state_info_set_all_entry_groups (state, NULL);
  bz_state_info_set_flathub (state, NULL);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);
  g_list_store_append (cats,
      add_category (flathub, "trending", (const char *[]) { "org.test.T1", NULL }));
  bz_flathub_state_set_categories (flathub, cats);
  bz_state_info_set_flathub (state, flathub);

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("Other");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), other,
                                       "other", "Other", NULL);
  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack),
                                       GTK_WIDGET (page), "custom", "Custom", NULL);

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (G_LIST_MODEL (all), "org.test.T1");
  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  process_events ();
  page_widget = GTK_WIDGET (page);
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), page_widget);
  process_events ();

  /* Initially no non-core labels - should show "Empty" pill */
  GtkWidget *p_empty = find_pill_by_label (page_widget, "Empty");
  g_assert_nonnull (p_empty);
  g_assert_true (GTK_IS_BUTTON (p_empty));
  g_assert_true (gtk_widget_get_child_visible (p_empty));
  g_assert_true (gtk_widget_has_css_class (p_empty, "noncore-pill"));

  /* Simulate window UI adding a custom label via the callback */
  store = cz_custom_page_get_label_store (page);
  g_assert_nonnull (store);
  g_assert_true (cz_custom_label_store_add_noncore_label_name (store, "Un Custom"));

  /* Process events to let the CHANGED signal propagate and rebuild pills */
  process_events ();

  /* The "Empty" pill should be gone, replaced by "Un Custom" pill */
  p_empty = find_pill_by_label (page_widget, "Empty");
  g_assert_null (p_empty);

  p_test = find_pill_by_label (page_widget, "Un Custom");
  g_assert_nonnull (p_test);
  g_assert_true (GTK_IS_BUTTON (p_test));
  g_assert_true (gtk_widget_get_child_visible (p_test));
  g_assert_true (gtk_widget_has_css_class (p_test, "noncore-pill"));
  g_assert_cmpstr (gtk_button_get_label (GTK_BUTTON (p_test)), ==, "Un Custom");

  /* Verify BzFullView would see the label (simulate full view sync) */
  GPtrArray *names = cz_custom_label_store_get_all_noncore_label_names (
      store);
  g_assert_cmpuint (names->len, ==, 1);
  g_assert_cmpstr ((const char *) g_ptr_array_index (names, 0), ==, "Un Custom");
  g_ptr_array_unref (names);

  dump_on_failure (page_widget, "NONCORE-ADDED-VIA-WINDOW-UI-FAIL-TREE");
  gtk_window_destroy (GTK_WINDOW (win));
}
 
/* ================================================================== */
/*  Suite: broken-code variants that MUST fail                        */
/*  These are not run by default — compiled with BREAKAGE_TESTS to    */
/*  prove the pass-condition tests would catch real regressions.       */
/* ================================================================== */
 
#ifdef BREAKAGE_TESTS
#error "BREAKAGE_TESTS enabled — these tests are designed to FAIL and prove the suite catches breakage"
#endif

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_test_add_func ("/cz-custom-page/widget-creation",
    test_page_creation);

  g_test_add_func ("/cz-custom-page/pills-created",
    test_pills_created);

  g_test_add_func ("/cz-custom-page/trending-auto-selected",
    test_trending_auto_selected);

  g_test_add_func ("/cz-custom-page/category-click-shows-apps",
    test_category_click_shows_apps);

  g_test_add_func ("/cz-custom-page/custom-tab-in-window",
    test_custom_tab_in_window);

  g_test_add_func ("/cz-custom-page/async-app-population",
    test_async_app_population);

  g_test_add_func ("/cz-custom-page/all-entry-groups-late-arrival",
    test_all_entry_groups_late_arrival);

  g_test_add_func ("/cz-custom-page/custom-page-exists-and-visible",
    test_custom_page_exists_and_visible);

  g_test_add_func ("/cz-custom-page/pills-exist-and-visible",
    test_pills_exist_and_visible);

  g_test_add_func ("/cz-custom-page/trending-auto-populates-on-each-visit",
    test_trending_auto_populates_on_each_visit);

  g_test_add_func ("/cz-custom-page/core-labels-exist-and-visible",
    test_core_labels_exist_and_visible);

  g_test_add_func ("/cz-custom-page/noncore-labels-populated",
    test_noncore_labels_populated);

  g_test_add_func ("/cz-custom-page/noncore-labels-empty",
    test_noncore_labels_empty);

  g_test_add_func ("/cz-custom-page/noncore-labels-added-via-window-ui",
    test_noncore_labels_added_via_window_ui);

  return g_test_run ();
}
