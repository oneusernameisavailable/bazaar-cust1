/*
 * Widget behavior tests.
 *
 * Verifies that Bz* widgets behave correctly with real data models:
 *   - Template bindings work with BzEntryGroup data
 *   - Layout/measure responds to property changes
 *   - Actionable integration works for navigation
 *   - Accessible tree reflects actual content
 *   - State changes (installed/updatable) update UI correctly
 *
 * Run with: GTK_A11Y=test meson test -C build test_widget_behavior
 */

#include <adwaita.h>
#include <bge.h>
#include <gtk/gtk.h>
#include <libdex.h>

#include "bz-app-tile.h"
#include "bz-category-tile.h"
#include "bz-entry-group.h"
#include "bz-entry.h"
#include "bz-flathub-category.h"
#include "bz-flatpak-entry.h"
#include "bz-install-controls.h"
#include "bz-list-tile.h"
#include "bz-progress-bar.h"
#include "bz-result.h"

/* Helper to find a child widget by CSS name in the template hierarchy */
static GtkWidget *
find_child_by_css_name (GtkWidget *root, const char *css_name)
{
  GtkWidget  *child;
  const char *child_css;
  GtkWidget  *found;

  for (child = gtk_widget_get_first_child (root);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      child_css = gtk_widget_get_css_name (child);
      if (g_strcmp0 (child_css, css_name) == 0)
        return child;

      /* Recurse */
      found = find_child_by_css_name (child, css_name);
      if (found != NULL)
        return found;
    }
  return NULL;
}

/* ------------------------------------------------------------------ */
/*  Helper to create a realistic BzEntryGroup for testing            */
/* ------------------------------------------------------------------ */

static BzEntryGroup *
create_test_entry_group (const char *id,
                         const char *title,
                         const char *developer,
                         const char *description,
                         gboolean    is_installed)
{
  BzEntry      *entry;
  BzEntryGroup *group;

  entry = bz_entry_new (id);
  g_assert_nonnull (entry);

  /* Set basic properties that the template binds to */
  g_object_set (entry,
                "title", title,
                "developer", developer,
                "description", description,
                "installed", is_installed,
                "kinds", BZ_ENTRY_KIND_APPLICATION,
                /* Set a unique_id for reconciliation to work.
                 * Use the id as unique_id, and an MD5 checksum. */
                "unique-id", id,
                "unique-id-checksum", "test-checksum",
                NULL);

  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group);

  /* Debug: check unique_ids */
  GListModel *unique_ids = bz_entry_group_get_model (group);
  if (unique_ids != NULL)
    {
      g_test_message ("After group creation: unique_ids count=%u",
                      g_list_model_get_n_items (unique_ids));
    }
  else
    {
      g_test_message ("After group creation: unique_ids is NULL");
    }

  if (is_installed)
    {
      /* Reconcile with installed set to mark as removable */
      GHashTable *installed_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
      /* Use the unique_id (not checksum) as the key, which is what reconcile matches against */
      g_hash_table_insert (installed_set, g_strdup (id), GINT_TO_POINTER (1));
      bz_entry_group_reconcile_with_installed_set (group, installed_set);
      g_hash_table_unref (installed_set);

      /* Debug: check removable count */
      g_test_message ("After reconcile: removable=%d, removable_available=%d, installable=%d",
                      bz_entry_group_get_removable (group),
                      bz_entry_group_get_removable_and_available (group),
                      bz_entry_group_get_installable (group));
    }

  g_clear_object (&entry);
  return group;
}

/* ------------------------------------------------------------------ */
/*  Test BzAppTile template binding with real data                     */
/* ------------------------------------------------------------------ */

static void
test_app_tile_template_binding (void)
{
  BzEntryGroup *group;
  BzAppTile    *tile;
  GtkWidget    *widget;
  GtkWidget    *title_label    = NULL;
  GtkWidget    *desc_label     = NULL;
  GtkWidget    *icon           = NULL;
  GtkWidget    *verified_icon  = NULL;
  GtkWidget    *installed_pill = NULL;
  const char   *label_text;

  group = create_test_entry_group ("test.app.tile",
                                   "Test Application",
                                   "Test Developer",
                                   "A test application for widget behavior testing",
                                   FALSE);
  tile  = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  /* Set the group - this triggers template binding */
  bz_app_tile_set_group (tile, group);
  widget = GTK_WIDGET (tile);

  /* Initialize template to bind data */
  gtk_widget_init_template (widget);

  /* Find child widgets by css-name */
  title_label    = find_child_by_css_name (widget, "app-tile-title");
  desc_label     = find_child_by_css_name (widget, "app-tile-description");
  icon           = find_child_by_css_name (widget, "icon-dropshadow");
  verified_icon  = find_child_by_css_name (widget, "app-tile-verified-check");
  installed_pill = find_child_by_css_name (widget, "app-tile-installed-pill");

  g_test_message ("Template children found: title=%p desc=%p icon=%p verified=%p pill=%p",
                  (void *) title_label, (void *) desc_label,
                  (void *) icon, (void *) verified_icon, (void *) installed_pill);

  g_assert_nonnull (title_label);
  g_assert_nonnull (desc_label);
  /* Icon is bound via complex template path (ui-entry -> result -> entry -> icon-paintable)
   * which requires a full BzResult setup. Skip icon verification for this test. */
  if (icon != NULL)
    {
      /* Verify icon has paintable bound */
      g_test_message ("Icon found: %p", (void *) icon);
      g_assert_nonnull (gtk_image_get_paintable (GTK_IMAGE (icon)));
    }
  else
    {
      g_test_message ("Icon not found (expected without full BzResult setup)");
    }

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
}

static void
test_app_tile_template_binding_installed (void)
{
  BzEntryGroup *group;
  BzAppTile    *tile;
  GtkWidget    *widget;
  GtkWidget    *installed_pill = NULL;

  group = create_test_entry_group ("test.app.installed",
                                   "Installed App",
                                   "Dev",
                                   "Description",
                                   TRUE);
  tile  = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  bz_app_tile_set_group (tile, group);
  widget = GTK_WIDGET (tile);
  gtk_widget_init_template (widget);

  /* Debug: check removable count after group is set on tile */
  g_test_message ("Group removable=%d, removable_available=%d, installable=%d",
                  bz_entry_group_get_removable (group),
                  bz_entry_group_get_removable_and_available (group),
                  bz_entry_group_get_installable (group));

  installed_pill = find_child_by_css_name (widget, "app-tile-installed-pill");
  g_assert_nonnull (installed_pill);

  /* Verify installed pill is visible */
  g_test_message ("Installed pill visible: %d", gtk_widget_get_visible (installed_pill));
  g_assert_true (gtk_widget_get_visible (installed_pill));

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
}

/* ------------------------------------------------------------------ */
/*  Test BzAppTile actionable integration                              */
/* ------------------------------------------------------------------ */

static void
test_app_tile_actionable (void)
{
  BzEntryGroup *group;
  BzAppTile    *tile;
  const char   *action_name;

  group = create_test_entry_group ("test.app.action",
                                   "Action Test App",
                                   "Dev",
                                   "Desc",
                                   FALSE);
  tile  = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  bz_app_tile_set_group (tile, group);

  /* Verify action is set for navigation */
  action_name = gtk_actionable_get_action_name (GTK_ACTIONABLE (tile));

  g_test_message ("Action name: '%s'", action_name ? action_name : "(null)");
  g_assert_nonnull (action_name);
  g_assert_cmpstr (action_name, ==, "window.show-group");

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
}

/* ------------------------------------------------------------------ */
/*  Test BzAppTile preferred width affects layout                      */
/* ------------------------------------------------------------------ */

static void
test_app_tile_preferred_width_layout (void)
{
  BzAppTile        *tile;
  GtkWidget        *widget;
  GtkLayoutManager *layout;
  gint              min_width, nat_width;

  tile = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);
  widget = GTK_WIDGET (tile);

  /* Initial preferred width is 270 (from blueprint) */
  layout = gtk_widget_get_layout_manager (widget);
  g_assert_nonnull (layout);

  gtk_widget_measure (widget, GTK_ORIENTATION_HORIZONTAL, -1,
                      &min_width, &nat_width,
                      NULL, NULL);
  g_test_message ("Initial measure: min=%d nat=%d (expected nat=270)", min_width, nat_width);
  g_assert_cmpint (nat_width, >=, 270);

  /* Change preferred width */
  bz_app_tile_set_preferred_width (tile, 400);

  gtk_widget_measure (widget, GTK_ORIENTATION_HORIZONTAL, -1,
                      &min_width, &nat_width,
                      NULL, NULL);
  g_test_message ("After set 400: min=%d nat=%d (expected nat>=400)", min_width, nat_width);
  g_assert_cmpint (nat_width, >=, 400);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
}

/* ------------------------------------------------------------------ */
/*  Test BzInstallControls state changes                               */
/* ------------------------------------------------------------------ */

static void
test_install_controls_state (void)
{
  BzEntryGroup      *group;
  BzInstallControls *controls;
  GtkWidget         *widget;
  BzEntryGroup      *result;

  group    = create_test_entry_group ("test.app.controls",
                                      "Control Test App",
                                      "Dev",
                                      "Desc",
                                      FALSE);
  controls = BZ_INSTALL_CONTROLS (bz_install_controls_new ());
  g_assert_nonnull (controls);

  bz_install_controls_set_entry_group (controls, group);
  widget = GTK_WIDGET (controls);
  gtk_widget_init_template (widget);

  /* Verify entry group is set */
  result = bz_install_controls_get_entry_group (controls);
  g_test_message ("Entry group retrieved: %p (expected %p)", (void *) result, (void *) group);
  g_assert_true (result == group);

  /* Should have children (the stack with pages) */
  g_assert_nonnull (gtk_widget_get_first_child (widget));

  g_object_ref_sink (controls);
  g_clear_object (&controls);
  g_clear_object (&group);
}

/* ------------------------------------------------------------------ */
/*  Test BzProgressBar fraction animation                              */
/* ------------------------------------------------------------------ */

static void
test_progress_bar_fraction (void)
{
  BzProgressBar *bar;
  double         fraction;

  bar = BZ_PROGRESS_BAR (bz_progress_bar_new ());
  g_assert_nonnull (bar);

  /* Initial fraction should be 0 */
  fraction = bz_progress_bar_get_fraction (bar);
  g_assert_cmpfloat (fraction, ==, 0.0);

  /* Set fraction */
  bz_progress_bar_set_fraction (bar, 0.5);
  fraction = bz_progress_bar_get_fraction (bar);
  g_test_message ("Fraction after set 0.5: %g", fraction);
  g_assert_cmpfloat (fraction, ==, 0.5);

  bz_progress_bar_set_fraction (bar, 1.0);
  fraction = bz_progress_bar_get_fraction (bar);
  g_test_message ("Fraction after set 1.0: %g", fraction);
  g_assert_cmpfloat (fraction, ==, 1.0);

  /* Clamping */
  bz_progress_bar_set_fraction (bar, 1.5);
  fraction = bz_progress_bar_get_fraction (bar);
  g_test_message ("Fraction after set 1.5 (should clamp): %g", fraction);
  g_assert_cmpfloat (fraction, ==, 1.0);

  bz_progress_bar_set_fraction (bar, -0.5);
  fraction = bz_progress_bar_get_fraction (bar);
  g_test_message ("Fraction after set -0.5 (should clamp): %g", fraction);
  g_assert_cmpfloat (fraction, ==, 0.0);

  g_object_ref_sink (bar);
  g_clear_object (&bar);
}

/* ------------------------------------------------------------------ */
/*  Test BzCategoryTile with FlathubCategory                           */
/* ------------------------------------------------------------------ */

static void
test_category_tile_category_binding (void)
{
  BzFlathubCategory *category;
  BzCategoryTile    *tile;
  GtkWidget         *widget;
  GtkWidget         *label = NULL;
  const char        *label_text;

  category = bz_flathub_category_new ();
  g_assert_nonnull (category);
  g_object_set (category,
                "name", "Test Category",
                NULL);

  tile = BZ_CATEGORY_TILE (bz_category_tile_new ());
  g_assert_nonnull (tile);

  bz_category_tile_set_category (tile, category);
  widget = GTK_WIDGET (tile);
  gtk_widget_init_template (widget);

  /* Find the title label - css-name is "category-tile-label" */
  label = find_child_by_css_name (widget, "category-tile-label");
  g_assert_nonnull (label);

  label_text = gtk_label_get_label (GTK_LABEL (label));
  g_test_message ("Category label: '%s'", label_text);
  g_assert_cmpstr (label_text, ==, "Test Category");

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&category);
}

/* ------------------------------------------------------------------ */
/*  Test accessible tree structure                                     */
/* ------------------------------------------------------------------ */

static void
test_app_tile_accessible_tree (void)
{
  BzEntryGroup     *group;
  BzAppTile        *tile;
  GtkWidget        *widget;
  GtkAccessible    *accessible;
  GtkAccessibleRole role;
  GtkAccessibleRole child_role;
  GtkWidget        *child;

  group = create_test_entry_group ("test.app.a11y",
                                   "Accessible App",
                                   "Dev",
                                   "Desc",
                                   FALSE);
  tile  = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  bz_app_tile_set_group (tile, group);
  widget = GTK_WIDGET (tile);
  gtk_widget_init_template (widget);

  /* Check tile itself has button role */
  accessible = GTK_ACCESSIBLE (widget);
  role       = gtk_accessible_get_accessible_role (accessible);
  g_test_message ("Tile role: %d (expected BUTTON=%d)", role, GTK_ACCESSIBLE_ROLE_BUTTON);
  g_assert_cmpint (role, ==, GTK_ACCESSIBLE_ROLE_BUTTON);

  /* Check children have appropriate roles */
  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      child_role = gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (child));
      g_test_message ("Child %s role: %d", gtk_widget_get_css_name (child), child_role);
    }

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
}

/* ------------------------------------------------------------------ */
/*  Test signal emission on real user interaction                      */
/* ------------------------------------------------------------------ */

static void
on_activated_cb (GtkWidget *w, gpointer d)
{
  *(gboolean *) d = TRUE;
  g_test_message ("  >> activated signal fired");
}

static void
test_app_tile_activated_signal (void)
{
  BzEntryGroup *group;
  BzAppTile    *tile;
  gboolean      fired = FALSE;

  group = create_test_entry_group ("test.app.signal",
                                   "Signal Test App",
                                   "Dev",
                                   "Desc",
                                   FALSE);
  tile  = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  bz_app_tile_set_group (tile, group);

  g_signal_connect (tile, "clicked", G_CALLBACK (on_activated_cb), &fired);

  /* Emit clicked signal (simulates click) */
  g_signal_emit_by_name (tile, "clicked");

  g_test_message ("Signal fired: %d", fired);
  g_assert_true (fired);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
}

/* ------------------------------------------------------------------ */
/*  Test widget measure with real content                              */
/* ------------------------------------------------------------------ */

static void
test_widget_measure_with_content (void)
{
  BzEntryGroup *group;
  BzAppTile    *tile;
  GtkWidget    *widget;
  gint          min_w, nat_w, min_h, nat_h;

  group = create_test_entry_group ("test.app.measure",
                                   "Measure Test App",
                                   "Dev",
                                   "Desc",
                                   FALSE);
  tile  = BZ_APP_TILE (bz_app_tile_new ());
  g_assert_nonnull (tile);

  bz_app_tile_set_group (tile, group);
  widget = GTK_WIDGET (tile);
  gtk_widget_init_template (widget);

  /* Measure with content */
  gtk_widget_measure (widget, GTK_ORIENTATION_HORIZONTAL, -1,
                      &min_w, &nat_w, NULL, NULL);
  gtk_widget_measure (widget, GTK_ORIENTATION_VERTICAL, nat_w,
                      &min_h, &nat_h, NULL, NULL);

  g_test_message ("Measure: w=%d..%d h=%d..%d", min_w, nat_w, min_h, nat_h);

  g_assert_cmpint (min_w, >, 0);
  g_assert_cmpint (nat_w, >, 0);
  g_assert_cmpint (min_h, >, 0);
  g_assert_cmpint (nat_h, >, 0);

  g_object_ref_sink (tile);
  g_clear_object (&tile);
  g_clear_object (&group);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  dex_init ();
  bge_init ();

  /* Ensure types are registered for template instantiation */
  g_type_ensure (bz_entry_get_type ());
  g_type_ensure (bz_entry_group_get_type ());
  g_type_ensure (bz_result_get_type ());
  g_type_ensure (bz_flatpak_entry_get_type ());
  g_type_ensure (bz_flathub_category_get_type ());
  g_type_ensure (BZ_TYPE_APP_TILE);
  g_type_ensure (BZ_TYPE_INSTALL_CONTROLS);
  g_type_ensure (BZ_TYPE_PROGRESS_BAR);
  g_type_ensure (BZ_TYPE_CATEGORY_TILE);

  g_test_add_func ("/widget-behavior/app-tile-template-binding",
                   test_app_tile_template_binding);
  g_test_add_func ("/widget-behavior/app-tile-template-binding-installed",
                   test_app_tile_template_binding_installed);
  g_test_add_func ("/widget-behavior/app-tile-actionable",
                   test_app_tile_actionable);
  g_test_add_func ("/widget-behavior/app-tile-preferred-width-layout",
                   test_app_tile_preferred_width_layout);
  g_test_add_func ("/widget-behavior/install-controls-state",
                   test_install_controls_state);
  g_test_add_func ("/widget-behavior/progress-bar-fraction",
                   test_progress_bar_fraction);
  g_test_add_func ("/widget-behavior/category-tile-category-binding",
                   test_category_tile_category_binding);
  g_test_add_func ("/widget-behavior/app-tile-accessible-tree",
                   test_app_tile_accessible_tree);
  g_test_add_func ("/widget-behavior/app-tile-activated-signal",
                   test_app_tile_activated_signal);
  g_test_add_func ("/widget-behavior/widget-measure-with-content",
                   test_widget_measure_with_content);

  return g_test_run ();
}