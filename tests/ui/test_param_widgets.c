/*
 * Widget/page/dialog tests for types that require constructor parameters.
 *
 * Creates each Bz* type with minimum-viable parameters, verifies
 * construction, type identity, default state, property round-trips,
 * and disposal.
 *
 * Run with: GTK: GTK_A11Y=test meson test -C build test_param_widgets
 *
 * Baseline reference: tests/baselines/widget-creation-baseline.txt
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
#include "bz-state-info.h"

/* Param-required widget types */
#include "bz-addons-dialog.h"
#include "bz-age-rating-dialog.h"
#include "bz-app-size-dialog.h"
#include "bz-auth-state.h"
#include "bz-error-dialog.h"
#include "bz-favorites-page.h"
#include "bz-featured-tile.h"
#include "bz-hardware-support-dialog.h"
#include "bz-license-dialog.h"
#include "bz-login-page.h"
#include "bz-preferences-dialog.h"
#include "bz-safety-dialog.h"
#include "bz-user-data-page.h"

/* ------------------------------------------------------------------ */
/*  Diagnostics helper                                                */
/* ------------------------------------------------------------------ */

static void
print_widget_info (GtkWidget *w, const char *name)
{
  GtkAccessibleRole role;
  gint              rw, rh;

  g_test_message ("--- Diagnostics for %s ---", name);
  g_test_message ("  ptr=%p  type=%s", (void *) w, G_OBJECT_TYPE_NAME (w));
  g_test_message ("  css-name=%s", gtk_widget_get_css_name (w));
  role = gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (w));
  g_test_message ("  role=%d", (int) role);
  gtk_widget_get_size_request (w, &rw, &rh);
  g_test_message ("  visible=%d  sensitive=%d",
                  (int) gtk_widget_get_visible (w),
                  (int) gtk_widget_get_sensitive (w));
  g_test_message ("  size-req=w=%d h=%d  hexpand=%d vexpand=%d",
                  rw, rh,
                  (int) gtk_widget_get_hexpand (w),
                  (int) gtk_widget_get_vexpand (w));
  g_test_message ("--- End diagnostics for %s ---", name);
}

/* ------------------------------------------------------------------ */
/*  Tests for types needing BzEntry* only                             */
/* ------------------------------------------------------------------ */

/* bz_safety_dialog_new / bz_license_dialog_new return an AdwDialog
 * that wraps the actual BzSafetyDialog/BzLicenseDialog as child.
 * We verify both the wrapper and the inner widget. */
static GtkWidget *
dialog_get_child (GtkWidget *dialog)
{
  GtkWidget *child;

  g_object_get (dialog, "child", &child, NULL);
  g_assert_nonnull (child);
  return child;
}

static void
test_safety_dialog (void)
{
  BzEntry   *entry;
  GtkWidget *dialog;
  GtkWidget *child;
  BzEntry   *returned_entry;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  dialog = GTK_WIDGET (bz_safety_dialog_new (entry));
  g_test_message ("ADDITION BzSafetyDialog wrapper: ptr=%p", (void *) dialog);
  g_assert_nonnull (dialog);
  g_assert_true (ADW_IS_DIALOG (dialog));

  child = dialog_get_child (dialog);
  g_test_message ("ADDITION BzSafetyDialog inner: ptr=%p type=%s",
                  (void *) child, G_OBJECT_TYPE_NAME (child));
  g_assert_true (BZ_IS_SAFETY_DIALOG (child));

  /* Property round-trip: entry */
  g_object_get (child, "entry", &returned_entry, NULL);
  g_assert_nonnull (returned_entry);
  g_assert_true (returned_entry == entry);
  g_clear_object (&returned_entry);

  g_object_ref_sink (dialog);
  print_widget_info (dialog, "AdwDialog (BzSafetyDialog)");

  g_clear_object (&dialog);
  g_clear_object (&entry);
}

static void
test_license_dialog (void)
{
  BzEntry   *entry;
  GtkWidget *dialog;
  GtkWidget *child;
  BzEntry   *returned_entry;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  dialog = GTK_WIDGET (bz_license_dialog_new (entry));
  g_test_message ("ADDITION BzLicenseDialog wrapper: ptr=%p", (void *) dialog);
  g_assert_nonnull (dialog);
  g_assert_true (ADW_IS_DIALOG (dialog));

  child = dialog_get_child (dialog);
  g_test_message ("ADDITION BzLicenseDialog inner: ptr=%p type=%s",
                  (void *) child, G_OBJECT_TYPE_NAME (child));
  g_assert_true (BZ_IS_LICENSE_DIALOG (child));

  /* Property round-trip: entry */
  g_object_get (child, "entry", &returned_entry, NULL);
  g_assert_nonnull (returned_entry);
  g_assert_true (returned_entry == entry);
  g_clear_object (&returned_entry);

  g_object_ref_sink (dialog);
  print_widget_info (dialog, "AdwDialog (BzLicenseDialog)");

  g_clear_object (&dialog);
  g_clear_object (&entry);
}

static void
test_age_rating_dialog (void)
{
  BzEntry   *entry;
  GtkWidget *w;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  w = GTK_WIDGET (bz_age_rating_dialog_new (entry));
  g_test_message ("ADDITION BzAgeRatingDialog: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_AGE_RATING_DIALOG (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* entry property is CONSTRUCT_ONLY | WRITABLE - not readable */

  g_object_ref_sink (w);
  print_widget_info (w, "BzAgeRatingDialog");

  g_clear_object (&w);
  g_clear_object (&entry);
}

static void
test_hardware_support_dialog (void)
{
  BzEntry   *entry;
  GtkWidget *w;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  w = GTK_WIDGET (bz_hardware_support_dialog_new (entry));
  g_test_message ("ADDITION BzHardwareSupportDialog: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_HARDWARE_SUPPORT_DIALOG (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* entry property is CONSTRUCT_ONLY | WRITABLE - not readable */

  g_object_ref_sink (w);
  print_widget_info (w, "BzHardwareSupportDialog");

  g_clear_object (&w);
  g_clear_object (&entry);
}

/* ------------------------------------------------------------------ */
/*  Tests for types needing BzEntryGroup*                             */
/* ------------------------------------------------------------------ */

/* bz_app_size_dialog_new returns an AdwDialog that wraps the actual
 * BzAppSizeDialog (AdwBin) as child. Its blueprint uses $is_app_id()
 * which needs g_application_get_default(). We register a temporary app
 * and run this test LAST to avoid contaminating other tests. */
static void
test_app_size_dialog (void)
{
  GApplication *app;
  BzEntry      *entry;
  BzEntryGroup *group;
  GtkWidget    *dialog;
  GtkWidget    *child;
  BzEntryGroup *returned_group;

  app = g_object_new (ADW_TYPE_APPLICATION,
                      "application-id", "org.test.param-widgets",
                      NULL);
  g_assert_nonnull (app);
  g_application_register (app, NULL, NULL);

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);
  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group);

  dialog = GTK_WIDGET (bz_app_size_dialog_new (group));
  g_test_message ("ADDITION BzAppSizeDialog wrapper: ptr=%p", (void *) dialog);
  g_assert_nonnull (dialog);
  g_assert_true (ADW_IS_DIALOG (dialog));

  child = dialog_get_child (dialog);
  g_test_message ("ADDITION BzAppSizeDialog inner: ptr=%p type=%s",
                  (void *) child, G_OBJECT_TYPE_NAME (child));
  g_assert_true (BZ_IS_APP_SIZE_DIALOG (child));

  /* Property round-trip: group */
  g_object_get (child, "group", &returned_group, NULL);
  g_assert_nonnull (returned_group);
  g_assert_true (returned_group == group);
  g_clear_object (&returned_group);

  g_object_ref_sink (dialog);
  print_widget_info (dialog, "AdwDialog (BzAppSizeDialog)");

  g_clear_object (&dialog);
  g_clear_object (&group);
  g_clear_object (&entry);
  g_clear_object (&app);
}

static void
test_addons_dialog (void)
{
  BzEntry      *entry;
  BzEntryGroup *group;
  GtkWidget    *w;
  GListModel   *addon_groups;
  BzEntryGroup *selected_group;
  BzResult     *selected_ui_entry;
  BzResult     *parent_ui_entry;
  BzStateInfo  *state;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);
  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group);

  w = GTK_WIDGET (bz_addons_dialog_new (group));
  g_test_message ("ADDITION BzAddonsDialog: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_ADDONS_DIALOG (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* Property round-trips - addon-groups is set asynchronously so may be NULL initially */
  g_object_get (w,
                "addon-groups", &addon_groups,
                "selected-group", &selected_group,
                "selected-ui-entry", &selected_ui_entry,
                "parent-ui-entry", &parent_ui_entry,
                "state", &state,
                NULL);

  /* addon_groups may be NULL initially if entry has no addons */
  if (addon_groups != NULL)
    g_clear_object (&addon_groups);

  g_assert_nonnull (parent_ui_entry);
  g_assert_nonnull (state);
  g_assert_null (selected_group);    /* initially NULL */
  g_assert_null (selected_ui_entry); /* initially NULL */

  g_clear_object (&selected_group);
  g_clear_object (&selected_ui_entry);
  g_clear_object (&parent_ui_entry);
  g_clear_object (&state);

  g_object_ref_sink (w);
  print_widget_info (w, "BzAddonsDialog");

  g_clear_object (&w);
  g_clear_object (&group);
  g_clear_object (&entry);
}

static void
test_featured_tile (void)
{
  BzEntry      *entry;
  BzEntryGroup *group;
  GtkWidget    *w;
  BzEntryGroup *returned_group;
  GdkPaintable *first_screenshot;
  gboolean      has_screenshot;
  gboolean      narrow;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);
  group = bz_entry_group_new_for_single_entry (entry);
  g_assert_nonnull (group);

  w = GTK_WIDGET (bz_featured_tile_new (group));
  g_test_message ("ADDITION BzFeaturedTile: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_FEATURED_TILE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* Property round-trips */
  g_object_get (w,
                "group", &returned_group,
                "first-screenshot", &first_screenshot,
                "has-screenshot", &has_screenshot,
                "narrow", &narrow,
                NULL);

  g_assert_nonnull (returned_group);
  g_assert_true (returned_group == group);
  g_clear_object (&returned_group);

  g_assert_null (first_screenshot); /* initially NULL */
  g_assert_false (has_screenshot);  /* initially FALSE */
  g_assert_false (narrow);          /* initially FALSE */

  /* Method call: set_group/get_group round-trip */
  bz_featured_tile_set_group (BZ_FEATURED_TILE (w), NULL);
  g_object_get (w, "group", &returned_group, NULL);
  g_assert_null (returned_group);

  bz_featured_tile_set_group (BZ_FEATURED_TILE (w), group);
  g_object_get (w, "group", &returned_group, NULL);
  g_assert_nonnull (returned_group);
  g_assert_true (returned_group == group);
  g_clear_object (&returned_group);

  g_object_ref_sink (w);
  print_widget_info (w, "BzFeaturedTile");

  g_clear_object (&w);
  g_clear_object (&group);
  g_clear_object (&entry);
}

/* ------------------------------------------------------------------ */
/*  Tests for types needing BzStateInfo*                              */
/* ------------------------------------------------------------------ */

static void
test_preferences_dialog (void)
{
  BzStateInfo *state;
  GtkWidget   *w;
  BzStateInfo *returned_state;

  state = bz_state_info_new ();
  g_assert_nonnull (state);

  w = GTK_WIDGET (bz_preferences_dialog_new (state));
  g_test_message ("ADDITION BzPreferencesDialog: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_PREFERENCES_DIALOG (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* Property round-trip: state (READABLE) */
  g_object_get (w, "state", &returned_state, NULL);
  g_assert_nonnull (returned_state);
  g_assert_true (returned_state == state);
  g_clear_object (&returned_state);

  g_object_ref_sink (w);
  print_widget_info (w, "BzPreferencesDialog");

  g_clear_object (&w);
  g_clear_object (&state);
}

static void
test_favorites_page (void)
{
  BzStateInfo *state;
  GtkWidget   *w;
  BzStateInfo *returned_state;
  GListModel  *model;
  GListModel  *favorites;
  gboolean     show_sidebar;

  state = bz_state_info_new ();
  g_assert_nonnull (state);

  w = GTK_WIDGET (bz_favorites_page_new (state));
  g_test_message ("ADDITION BzFavoritesPage: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_FAVORITES_PAGE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* Property round-trips */
  g_object_get (w,
                "state", &returned_state,
                "model", &model,
                "favorites", &favorites,
                "show-sidebar", &show_sidebar,
                NULL);

  g_assert_nonnull (returned_state);
  g_assert_true (returned_state == state);
  g_clear_object (&returned_state);

  g_assert_null (model);         /* initially NULL until async fetch completes */
  g_assert_null (favorites);     /* initially NULL until async fetch completes */
  g_assert_false (show_sidebar); /* default FALSE */

  g_clear_object (&model);
  g_clear_object (&favorites);

  /* Property setter: show-sidebar */
  g_object_set (w, "show-sidebar", TRUE, NULL);
  g_object_get (w, "show-sidebar", &show_sidebar, NULL);
  g_assert_true (show_sidebar);

  g_object_set (w, "show-sidebar", FALSE, NULL);
  g_object_get (w, "show-sidebar", &show_sidebar, NULL);
  g_assert_false (show_sidebar);

  g_object_ref_sink (w);
  print_widget_info (w, "BzFavoritesPage");

  g_clear_object (&w);
  g_clear_object (&state);
}

static void
test_user_data_page (void)
{
  BzStateInfo *state;
  GtkWidget   *w;
  BzStateInfo *returned_state;
  GListModel  *model;

  state = bz_state_info_new ();
  g_assert_nonnull (state);

  w = GTK_WIDGET (bz_user_data_page_new (state));
  g_test_message ("ADDITION BzUserDataPage: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_USER_DATA_PAGE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* Property round-trips */
  g_object_get (w,
                "state", &returned_state,
                "model", &model,
                NULL);

  g_assert_nonnull (returned_state);
  g_assert_true (returned_state == state);
  g_clear_object (&returned_state);

  g_assert_null (model); /* initially NULL until async fetch completes */
  g_clear_object (&model);

  g_object_ref_sink (w);
  print_widget_info (w, "BzUserDataPage");

  g_clear_object (&w);
  g_clear_object (&state);
}

/* ------------------------------------------------------------------ */
/*  Tests for types needing string params                             */
/* ------------------------------------------------------------------ */

static void
test_error_dialog (void)
{
  GtkWidget *w;

  w = GTK_WIDGET (bz_error_dialog_new ("Test Title", "Test message body"));
  g_test_message ("ADDITION BzErrorDialog: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_ERROR_DIALOG (w));
  g_assert_true (GTK_IS_WIDGET (w));

  g_object_ref_sink (w);
  print_widget_info (w, "BzErrorDialog");

  g_clear_object (&w);
}

/* ------------------------------------------------------------------ */
/*  Tests for types needing BzAuthState*                              */
/* ------------------------------------------------------------------ */

static void
test_login_page (void)
{
  BzAuthState *auth;
  GtkWidget   *w;
  BzAuthState *returned_auth;

  auth = g_object_new (BZ_TYPE_AUTH_STATE, NULL);
  g_assert_nonnull (auth);

  w = GTK_WIDGET (bz_login_page_new (auth));
  g_test_message ("ADDITION BzLoginPage: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_LOGIN_PAGE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* Property round-trip: auth-state (READWRITE, CONSTRUCT_ONLY) */
  g_object_get (w, "auth-state", &returned_auth, NULL);
  g_assert_nonnull (returned_auth);
  g_assert_true (returned_auth == auth);
  g_clear_object (&returned_auth);

  g_object_ref_sink (w);
  print_widget_info (w, "BzLoginPage");

  g_clear_object (&w);
  g_clear_object (&auth);
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
  g_type_ensure (bz_auth_state_get_type ());
  g_type_ensure (bz_result_get_type ());
  g_type_ensure (bz_lozenge_get_type ());
  g_type_ensure (bz_async_texture_get_type ());

  /* BzEntry-based constructors */
  g_test_add_func ("/param-widgets/safety-dialog", test_safety_dialog);
  g_test_add_func ("/param-widgets/license-dialog", test_license_dialog);
  g_test_add_func ("/param-widgets/age-rating-dialog", test_age_rating_dialog);
  g_test_add_func ("/param-widgets/hardware-support-dialog",
                   test_hardware_support_dialog);

  /* BzEntryGroup-based constructors */
  g_test_add_func ("/param-widgets/addons-dialog", test_addons_dialog);
  g_test_add_func ("/param-widgets/featured-tile", test_featured_tile);

  /* BzStateInfo-based constructors */
  g_test_add_func ("/param-widgets/preferences-dialog",
                   test_preferences_dialog);
  g_test_add_func ("/param-widgets/favorites-page", test_favorites_page);
  g_test_add_func ("/param-widgets/user-data-page", test_user_data_page);

  /* String-based constructors */
  g_test_add_func ("/param-widgets/error-dialog", test_error_dialog);

  /* AuthState-based constructors */
  g_test_add_func ("/param-widgets/login-page", test_login_page);

  /* BzAppSizeDialog runs LAST — it registers a GApplication which
   * would contaminate g_application_get_default() for other tests. */
  g_test_add_func ("/param-widgets/app-size-dialog", test_app_size_dialog);

  return g_test_run ();
}
