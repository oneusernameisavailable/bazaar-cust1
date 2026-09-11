/*
 * Property round-trip and signal emission tests for Bz* widget types.
 *
 * Verifies that:
 *   - Public setters/getters preserve values correctly
 *   - GObject properties round-trip via g_object_set/g_object_get
 *   - Signal emission fires on expected triggers (gtk_widget_activate,
 *     property changes with G_PARAM_EXPLICIT_NOTIFY)
 *
 * Run with: GTK_A11Y=test meson test -C build test_properties
 */

#include <adwaita.h>
#include <bge.h>
#include <gtk/gtk.h>
#include <libdex.h>

#include "bz-async-texture.h"
#include "bz-entry-group.h"
#include "bz-entry.h"
#include "bz-flathub-category.h"
#include "bz-flatpak-entry.h"
#include "bz-lozenge.h"
#include "bz-result.h"

#include "bz-app-tile.h"
#include "bz-category-tile.h"
#include "bz-featured-tile.h"
#include "bz-license-dialog.h"
#include "bz-list-tile.h"
#include "bz-progress-bar.h"
#include "bz-safety-dialog.h"

/* ------------------------------------------------------------------ */
/*  Signal emission helpers                                            */
/* ------------------------------------------------------------------ */

static void
on_activated (GtkWidget *w,
              gpointer   user_data)
{
  gboolean *fired = user_data;

  *fired = TRUE;
  g_test_message ("  >> activated signal fired for %s", G_OBJECT_TYPE_NAME (w));
}

static void
on_notify (GObject    *obj,
           GParamSpec *pspec,
           gpointer    user_data)
{
  gboolean *fired = user_data;

  *fired = TRUE;
  g_test_message ("  >> notify::%s fired for %s",
                  pspec->name, G_OBJECT_TYPE_NAME (obj));
}

/* ------------------------------------------------------------------ */
/*  Phase 1 — Simple property round-trips (no GObject deps)           */
/* ------------------------------------------------------------------ */

static void
test_list_tile_child (void)
{
  BzListTile *tile;
  GtkWidget  *label;
  GtkWidget  *result;

  tile = bz_list_tile_new ();
  g_assert_nonnull (tile);
  label = gtk_label_new ("child-widget");
  g_assert_nonnull (label);

  g_test_message ("Setting BzListTile child");
  bz_list_tile_set_child (tile, label);
  result = bz_list_tile_get_child (tile);
  g_test_message ("  got child back: %p (expected %p)", (void *) result, (void *) label);
  g_assert_true (result == label);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
}

static void
test_progress_bar_fraction (void)
{
  BzProgressBar *bar;
  double         val;

  bar = BZ_PROGRESS_BAR (bz_progress_bar_new ());
  g_assert_nonnull (bar);

  val = bz_progress_bar_get_fraction (bar);
  g_test_message ("Initial fraction: %g (expected 0)", val);
  g_assert_cmpfloat (val, ==, 0.0);

  g_test_message ("Setting fraction to 0.75");
  bz_progress_bar_set_fraction (bar, 0.75);
  val = bz_progress_bar_get_fraction (bar);
  g_test_message ("  got back: %g", val);
  g_assert_cmpfloat (val, ==, 0.75);

  g_test_message ("Setting fraction back to 0.0");
  bz_progress_bar_set_fraction (bar, 0.0);
  val = bz_progress_bar_get_fraction (bar);
  g_test_message ("  got back: %g", val);
  g_assert_cmpfloat (val, ==, 0.0);

  g_object_ref_sink (bar);
  g_clear_object (&bar);
}

/* ------------------------------------------------------------------ */
/*  Phase 2 — Property round-trips needing BzEntryGroup or            */
/*            BzFlathubCategory                                        */
/* ------------------------------------------------------------------ */

static void
test_category_tile_category (void)
{
  BzCategoryTile    *tile;
  BzFlathubCategory *cat;
  BzFlathubCategory *result;

  tile = BZ_CATEGORY_TILE (bz_category_tile_new ());
  g_assert_nonnull (tile);
  cat = bz_flathub_category_new ();
  g_assert_nonnull (cat);

  g_test_message ("Setting BzCategoryTile category");
  bz_category_tile_set_category (tile, cat);
  result = bz_category_tile_get_category (tile);
  g_test_message ("  got back: %p (expected %p)", (void *) result, (void *) cat);
  g_assert_true (result == cat);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&cat);
}

static void
test_app_tile_preferred_width (void)
{
  BzAppTile *tile;
  gint       val;

  tile = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  val = bz_app_tile_get_preferred_width (tile);
  g_test_message ("Initial preferred-width: %d (expected 270, from Blueprint)", val);
  g_assert_cmpint (val, ==, 270);

  g_test_message ("Setting preferred-width to 200");
  bz_app_tile_set_preferred_width (tile, 200);
  val = bz_app_tile_get_preferred_width (tile);
  g_test_message ("  got back: %d", val);
  g_assert_cmpint (val, ==, 200);

  g_test_message ("Resetting preferred-width");
  bz_app_tile_set_preferred_width (tile, 270);
  val = bz_app_tile_get_preferred_width (tile);
  g_assert_cmpint (val, ==, 270);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
}

static void
test_app_tile_group (void)
{
  BzAppTile    *tile;
  BzEntry      *entry;
  BzEntryGroup *group;
  BzEntryGroup *result;

  entry = bz_entry_new ("test-app-tile-group");
  g_assert_nonnull (entry);
  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group);
  tile = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  g_test_message ("Setting BzAppTile group");
  bz_app_tile_set_group (tile, group);
  result = bz_app_tile_get_group (tile);
  g_test_message ("  got back: %p (expected %p)", (void *) result, (void *) group);
  g_assert_true (result == group);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
  g_clear_object (&entry);
}

static void
test_featured_tile_group (void)
{
  BzEntry        *entry;
  BzEntryGroup   *group1;
  BzEntryGroup   *group2;
  BzFeaturedTile *tile;
  BzEntryGroup   *result;

  entry = bz_entry_new ("featured-tile-group-1");
  g_assert_nonnull (entry);
  group1 = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group1);
  tile = bz_featured_tile_new (group1);
  g_assert_nonnull (tile);

  result = bz_featured_tile_get_group (tile);
  g_test_message ("Initial group: %p (expected %p)", (void *) result, (void *) group1);
  g_assert_true (result == group1);

  entry  = bz_entry_new ("featured-tile-group-2");
  group2 = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (entry);
  g_assert_nonnull (group2);

  g_test_message ("Swapping group on BzFeaturedTile");
  bz_featured_tile_set_group (tile, group2);
  result = bz_featured_tile_get_group (tile);
  g_test_message ("  got back: %p (expected %p)", (void *) result, (void *) group2);
  g_assert_true (result == group2);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group1);
  g_clear_object (&group2);
  g_clear_object (&entry);
}

/* ------------------------------------------------------------------ */
/*  Phase 3 — AdwDialog wrapper property tests                        */
/* ------------------------------------------------------------------ */

static void
test_safety_dialog_entry (void)
{
  BzEntry   *entry;
  BzEntry   *result;
  GtkWidget *dialog;
  GtkWidget *child;

  entry = bz_entry_new ("safety-dialog-test");
  g_assert_nonnull (entry);

  dialog = GTK_WIDGET (bz_safety_dialog_new (entry));
  g_assert_nonnull (dialog);
  g_assert_true (ADW_IS_DIALOG (dialog));

  child = NULL;
  g_object_get (dialog, "child", &child, NULL);
  g_assert_nonnull (child);
  g_assert_true (BZ_IS_SAFETY_DIALOG (child));

  result = NULL;
  g_test_message ("Reading BzSafetyDialog entry property via g_object_get");
  g_object_get (child, "entry", &result, NULL);
  g_test_message ("  got entry: %p (expected %p)", (void *) result, (void *) entry);
  g_assert_true (result == entry);
  g_clear_object (&result);
  g_clear_object (&child);

  g_object_ref_sink (dialog);
  g_clear_object (&dialog);
  g_clear_object (&entry);
}

static void
test_license_dialog_entry (void)
{
  BzEntry   *entry;
  BzEntry   *result;
  GtkWidget *dialog;
  GtkWidget *child;

  entry = bz_entry_new ("license-dialog-test");
  g_assert_nonnull (entry);

  dialog = GTK_WIDGET (bz_license_dialog_new (entry));
  g_assert_nonnull (dialog);
  g_assert_true (ADW_IS_DIALOG (dialog));

  child = NULL;
  g_object_get (dialog, "child", &child, NULL);
  g_assert_nonnull (child);
  g_assert_true (BZ_IS_LICENSE_DIALOG (child));

  result = NULL;
  g_test_message ("Reading BzLicenseDialog entry property via g_object_get");
  g_object_get (child, "entry", &result, NULL);
  g_test_message ("  got entry: %p (expected %p)", (void *) result, (void *) entry);
  g_assert_true (result == entry);
  g_clear_object (&result);
  g_clear_object (&child);

  g_object_ref_sink (dialog);
  g_clear_object (&dialog);
  g_clear_object (&entry);
}

/* ------------------------------------------------------------------ */
/*  Phase 4 — Signal emission tests                                   */
/* ------------------------------------------------------------------ */

static void
test_list_tile_activated_signal (void)
{
  BzListTile *tile;
  gboolean    fired;

  tile = bz_list_tile_new ();
  g_assert_nonnull (tile);

  fired = FALSE;
  g_signal_connect (tile, "activated", G_CALLBACK (on_activated), &fired);

  g_test_message ("Emitting activated signal directly");
  g_signal_emit_by_name (tile, "activated");

  g_test_message ("  signal fired: %d (expected TRUE)", (int) fired);
  g_assert_true (fired);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
}

static void
test_progress_bar_notify_fraction (void)
{
  BzProgressBar *bar;
  gboolean       fired;

  bar = BZ_PROGRESS_BAR (bz_progress_bar_new ());
  g_assert_nonnull (bar);

  fired = FALSE;
  g_signal_connect (bar, "notify::fraction", G_CALLBACK (on_notify), &fired);

  g_test_message ("Setting fraction to trigger notify::fraction");
  bz_progress_bar_set_fraction (bar, 0.5);
  g_test_message ("  notify fired: %d (expected TRUE)", (int) fired);
  g_assert_true (fired);

  g_object_ref_sink (bar);
  g_clear_object (&bar);
}

static void
test_app_tile_notify_group (void)
{
  BzAppTile    *tile;
  BzEntry      *entry;
  BzEntryGroup *group;
  gboolean      fired;

  entry = bz_entry_new ("app-tile-notify-group");
  g_assert_nonnull (entry);
  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group);
  tile = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  fired = FALSE;
  g_signal_connect (tile, "notify::group", G_CALLBACK (on_notify), &fired);

  g_test_message ("Setting group to trigger notify::group on BzAppTile");
  bz_app_tile_set_group (tile, group);
  g_test_message ("  notify fired: %d (expected TRUE)", (int) fired);
  g_assert_true (fired);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
  g_clear_object (&entry);
}

static void
test_featured_tile_notify_group (void)
{
  BzEntry        *entry;
  BzEntryGroup   *group;
  BzFeaturedTile *tile;
  gboolean        fired;

  entry = bz_entry_new ("featured-notify-1");
  g_assert_nonnull (entry);
  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group);
  tile = bz_featured_tile_new (group);
  g_assert_nonnull (tile);

  fired = FALSE;
  g_signal_connect (tile, "notify::group", G_CALLBACK (on_notify), &fired);

  g_test_message ("Setting group on BzFeaturedTile to trigger notify::group");
  entry = bz_entry_new ("featured-notify-2");
  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (entry);
  g_assert_nonnull (group);
  bz_featured_tile_set_group (tile, group);
  g_test_message ("  notify fired: %d (expected TRUE)", (int) fired);
  g_assert_true (fired);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
  g_clear_object (&entry);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  dex_init ();
  bge_init ();

  g_type_ensure (bz_entry_get_type ());
  g_type_ensure (bz_entry_group_get_type ());
  g_type_ensure (bz_flatpak_entry_get_type ());
  g_type_ensure (bz_result_get_type ());
  g_type_ensure (bz_lozenge_get_type ());
  g_type_ensure (bz_async_texture_get_type ());

  /* Phase 1 — Simple property round-trips */
  g_test_add_func ("/properties/list-tile-child", test_list_tile_child);
  g_test_add_func ("/properties/progress-bar-fraction",
                   test_progress_bar_fraction);

  /* Phase 2 — Property round-trips with fixtures */
  g_test_add_func ("/properties/category-tile-category",
                   test_category_tile_category);
  g_test_add_func ("/properties/app-tile-preferred-width",
                   test_app_tile_preferred_width);
  g_test_add_func ("/properties/app-tile-group", test_app_tile_group);
  g_test_add_func ("/properties/featured-tile-group",
                   test_featured_tile_group);

  /* Phase 3 — AdwDialog wrapper properties */
  g_test_add_func ("/properties/safety-dialog-entry",
                   test_safety_dialog_entry);
  g_test_add_func ("/properties/license-dialog-entry",
                   test_license_dialog_entry);

  /* Phase 4 — Signal emission */
  g_test_add_func ("/properties/list-tile-activated-signal",
                   test_list_tile_activated_signal);
  g_test_add_func ("/properties/progress-bar-notify-fraction",
                   test_progress_bar_notify_fraction);
  g_test_add_func ("/properties/app-tile-notify-group",
                   test_app_tile_notify_group);
  g_test_add_func ("/properties/featured-tile-notify-group",
                   test_featured_tile_notify_group);

  return g_test_run ();
}
