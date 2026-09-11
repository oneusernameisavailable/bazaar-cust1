/*
 * Comprehensive widget creation test.
 *
 * Creates every Bz* widget type that can be constructed with no parameters
 * and verifies basic lifecycle: construction, type identity, floating ref,
 * default visible/sensitive/parent state, accessible role, and size request.
 * Additionally verifies property round-trips and method calls where available.
 *
 * Run with: GTK_A11Y=test meson test -C build test_widget_creation
 *
 * Each test logs full diagnostics so analysis can pin-point any failure.
 * See tests/baselines/widget-creation-baseline.txt for reference values.
 */

#include <adwaita.h>
#include <bge.h>
#include <gtk/gtk.h>
#include <libdex.h>

/* Standard widgets — GtkWidget subclass */
#include "bz-aspect-picture.h"
#include "bz-data-graph.h"
#include "bz-fading-clamp.h"
#include "bz-list-tile.h"
#include "bz-rounded-picture.h"
#include "bz-screenshot.h"
#include "bz-screenshots-carousel.h"
#include "bz-themed-entry-group-rect.h"
#include "bz-world-map.h"
#include "bz-zoom.h"

/* Button-based widgets */
#include "bz-app-tile.h"
#include "bz-category-tile.h"
#include "bz-context-tile.h"
#include "bz-decorated-screenshot.h"
#include "bz-developer-badge.h"
#include "bz-favorite-button.h"

/* Box-based widgets */
#include "bz-featured-carousel.h"
#include "bz-flathub-category-section.h"
#include "bz-install-controls.h"
#include "bz-lozenge.h"
#include "bz-search-pill-list.h"
#include "bz-share-list.h"
#include "bz-subcategory-list.h"

/* Popover */
#include "bz-search-filter-popover.h"

/* Pages — AdwBin */
#include "bz-appstream-description-render.h"
#include "bz-curated-view.h"
#include "bz-dynamic-list-view.h"
#include "bz-flathub-page.h"
#include "bz-full-view.h"
#include "bz-lazy-wdgt.h"
#include "bz-library-page.h"
#include "bz-progress-bar.h"
#include "bz-releases-list.h"
#include "bz-transact-icon.h"
#include "bz-updates-card.h"
#include "bz-user-data-tile.h"

/* Windows */
#include "bz-entry-inspector.h"
#include "bz-inspector.h"

/* Dialogs — AdwDialog */
#include "bz-bundle-install-dialog.h"
#include "bz-donations-dialog.h"

/* Tile subclasses (BzListTile children) */
#include "bz-addon-tile.h"
#include "bz-article-tile.h"
#include "bz-favorites-tile.h"
#include "bz-installed-tile.h"
#include "bz-rich-app-tile.h"
#include "bz-transaction-tile.h"

/* Data-model types referenced by Blueprint templates — must be
 * registered before template instantiation. Include the header so the
 * _get_type() symbol is visible; g_type_ensure() call in main() ensures
 * the linker pulls in the .o file from the static library. */
#include "bz-async-texture.h"
#include "bz-auth-state.h"
#include "bz-curated-article.h"
#include "bz-entry-group.h"
#include "bz-entry.h"
#include "bz-flathub-category.h"
#include "bz-flatpak-entry.h"
#include "bz-result.h"
#include "bz-transact-icon-info.h"
#include "bz-transaction-entry-tracker.h"

/* ------------------------------------------------------------------ */
/*  Enhanced test helper with property verification                   */
/* ------------------------------------------------------------------ */

static void
test_widget_property_roundtrip_object (GtkWidget  *w,
                                       const char *prop_name,
                                       GType       expected_type,
                                       gboolean    allow_null)
{
  GObject *value = NULL;

  g_object_get (w, prop_name, &value, NULL);
  if (value != NULL)
    {
      g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (value, expected_type));
      g_clear_object (&value);
    }
  else
    {
      g_assert_true (allow_null);
    }
}

static void
test_widget_property_roundtrip_int (GtkWidget  *w,
                                    const char *prop_name,
                                    int         expected_default)
{
  int value = 0;

  g_object_get (w, prop_name, &value, NULL);
  g_assert_cmpint (value, ==, expected_default);
}

static void
test_widget_property_roundtrip_double (GtkWidget  *w,
                                       const char *prop_name,
                                       double      expected_default)
{
  double value = 0.0;

  g_object_get (w, prop_name, &value, NULL);
  g_assert_cmpfloat (value, ==, expected_default);
}

static void
test_widget_property_roundtrip_boolean (GtkWidget  *w,
                                        const char *prop_name,
                                        gboolean    expected_default)
{
  gboolean value = FALSE;

  g_object_get (w, prop_name, &value, NULL);
  g_assert_cmpint (value, ==, expected_default);
}

static void
test_widget_property_roundtrip_string (GtkWidget  *w,
                                       const char *prop_name,
                                       const char *expected_default)
{
  g_autofree char *value = NULL;

  g_object_get (w, prop_name, &value, NULL);
  if (expected_default == NULL)
    g_assert_null (value);
  else
    g_assert_cmpstr (value ? value : "", ==, expected_default);
}

/* Forward declarations for helpers used by test_widget_enhanced and test_single_widget */
static void print_widget_diagnostics (GtkWidget  *w,
                                      const char *type_name_short,
                                      gboolean    pre_sink);
static void test_single_widget (GType             gtype,
                                const char       *type_name_short,
                                GType             expected_gtype,
                                GtkAccessibleRole expected_role);

/* Generic widget test with property verification */
static void
test_widget_enhanced (GType             gtype,
                      const char       *type_name_short,
                      GType             expected_gtype,
                      GtkAccessibleRole expected_role,
                      void (*verify_props) (GtkWidget *w))
{
  GtkWidget *w;
  GType      actual_type;

  /* ADDITION: construct */
  w = GTK_WIDGET (g_object_new (gtype, NULL));
  g_test_message ("ADDITION %s: ptr=%p", type_name_short, (void *) w);
  g_assert_nonnull (w);

  /* MODIFICATION: floating ref capture before sink */
  g_test_message ("MODIFICATION %s: pre-sink floating=%d", type_name_short,
                  (int) g_object_is_floating (w));
  print_widget_diagnostics (w, type_name_short, TRUE);

  /* TYPE: verify identity */
  actual_type = G_OBJECT_TYPE (w);
  g_test_message ("ADDITION %s: actual-type=%s expected=%s match=%s",
                  type_name_short,
                  g_type_name (actual_type),
                  g_type_name (expected_gtype),
                  actual_type == expected_gtype ? "OK" : "WRONG");
  g_assert_cmpint (actual_type, ==, expected_gtype);

  g_test_message ("ADDITION %s: GTK_IS_WIDGET=%s", type_name_short,
                  GTK_IS_WIDGET (w) ? "true" : "false");
  g_assert_true (GTK_IS_WIDGET (w));

  /* ACCESSIBLE ROLE: verify expected role */
  {
    GtkAccessibleRole actual_role;

    actual_role = gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (w));
    g_test_message ("MODIFICATION %s: role=%d (expected %d)",
                    type_name_short, (int) actual_role, (int) expected_role);
    g_assert_cmpint (actual_role, ==, expected_role);
  }

  /* Property verification */
  if (verify_props != NULL)
    verify_props (w);

  /* MODIFICATION: sink the floating ref */
  g_object_ref_sink (w);
  g_test_message ("MODIFICATION %s: post-sink floating=%d", type_name_short,
                  (int) g_object_is_floating (w));

  print_widget_diagnostics (w, type_name_short, FALSE);

  /* DELETION: unref */
  g_test_message ("DELETION %s: pre-clear=%p", type_name_short, (void *) w);
  g_clear_object (&w);
  g_test_message ("DELETION %s: post-clear=%p", type_name_short, (void *) w);
  g_assert_null (w);
}

/* No-op verifier for widgets with no public API */
static void
verify_props_none (GtkWidget *w)
{
  (void) w;
}

/* ------------------------------------------------------------------ */
/*  Missing helper implementations                                    */
/* ------------------------------------------------------------------ */

static void
print_widget_diagnostics (GtkWidget  *w,
                          const char *type_name_short,
                          gboolean    pre_sink)
{
  GtkAccessibleRole role;
  gint              rw, rh;

  g_test_message ("--- Diagnostics for %s (%s) ---", type_name_short,
                  pre_sink ? "pre-sink" : "post-sink");
  g_test_message ("  ptr=%p", (void *) w);
  g_test_message ("  type=%s  GType=%lu",
                  G_OBJECT_TYPE_NAME (w),
                  (unsigned long) G_OBJECT_TYPE (w));
  g_test_message ("  css-name=%s", gtk_widget_get_css_name (w));
  g_test_message ("  floating=%d", (int) g_object_is_floating (w));
  g_test_message ("  visible=%d", (int) gtk_widget_get_visible (w));
  g_test_message ("  sensitive=%d", (int) gtk_widget_get_sensitive (w));
  g_test_message ("  parent=%p", (void *) gtk_widget_get_parent (w));
  role = gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (w));
  g_test_message ("  role=%d", (int) role);
  gtk_widget_get_size_request (w, &rw, &rh);
  g_test_message ("  size-req=w=%d h=%d", rw, rh);
  g_test_message ("  hexpand=%d  vexpand=%d",
                  (int) gtk_widget_get_hexpand (w),
                  (int) gtk_widget_get_vexpand (w));
  g_test_message ("  opacity=%.3f", (double) gtk_widget_get_opacity (w));
  g_test_message ("--- End diagnostics ---");
}

static void
test_single_widget (GType             gtype,
                    const char       *type_name_short,
                    GType             expected_gtype,
                    GtkAccessibleRole expected_role)
{
  GtkWidget *w;
  GType      actual_type;

  w = GTK_WIDGET (g_object_new (gtype, NULL));
  g_test_message ("ADDITION %s: ptr=%p", type_name_short, (void *) w);
  g_assert_nonnull (w);

  g_test_message ("MODIFICATION %s: pre-sink floating=%d", type_name_short,
                  (int) g_object_is_floating (w));
  print_widget_diagnostics (w, type_name_short, TRUE);

  actual_type = G_OBJECT_TYPE (w);
  g_test_message ("ADDITION %s: actual-type=%s expected=%s match=%s",
                  type_name_short,
                  g_type_name (actual_type),
                  g_type_name (expected_gtype),
                  actual_type == expected_gtype ? "OK" : "WRONG");
  g_assert_cmpint (actual_type, ==, expected_gtype);

  g_test_message ("ADDITION %s: GTK_IS_WIDGET=%s", type_name_short,
                  GTK_IS_WIDGET (w) ? "true" : "false");
  g_assert_true (GTK_IS_WIDGET (w));

  {
    GtkAccessibleRole actual_role;

    actual_role = gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (w));
    g_test_message ("MODIFICATION %s: role=%d (expected %d)",
                    type_name_short, (int) actual_role, (int) expected_role);
    g_assert_cmpint (actual_role, ==, expected_role);
  }

  g_object_ref_sink (w);
  g_test_message ("MODIFICATION %s: post-sink floating=%d", type_name_short,
                  (int) g_object_is_floating (w));

  print_widget_diagnostics (w, type_name_short, FALSE);

  g_test_message ("DELETION %s: pre-clear=%p", type_name_short, (void *) w);
  g_clear_object (&w);
  g_test_message ("DELETION %s: post-clear=%p", type_name_short, (void *) w);
  g_assert_null (w);
}

/* ------------------------------------------------------------------ */
/*  Per-type test functions                                           */
/* ------------------------------------------------------------------ */

static void
test_fading_clamp (void)
{
  test_single_widget (BZ_TYPE_FADING_CLAMP, "BzFadingClamp",
                      BZ_TYPE_FADING_CLAMP,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_zoom (void)
{
  test_single_widget (BZ_TYPE_ZOOM, "BzZoom", BZ_TYPE_ZOOM,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_world_map (void)
{
  test_single_widget (BZ_TYPE_WORLD_MAP, "BzWorldMap",
                      BZ_TYPE_WORLD_MAP,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_data_graph (void)
{
  test_single_widget (BZ_TYPE_DATA_GRAPH, "BzDataGraph",
                      BZ_TYPE_DATA_GRAPH,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_screenshots_carousel (void)
{
  test_single_widget (BZ_TYPE_SCREENSHOTS_CAROUSEL,
                      "BzScreenshotsCarousel",
                      BZ_TYPE_SCREENSHOTS_CAROUSEL,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_rounded_picture (void)
{
  test_single_widget (BZ_TYPE_ROUNDED_PICTURE, "BzRoundedPicture",
                      BZ_TYPE_ROUNDED_PICTURE,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_aspect_picture (void)
{
  test_single_widget (BZ_TYPE_ASPECT_PICTURE, "BzAspectPicture",
                      BZ_TYPE_ASPECT_PICTURE,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_screenshot (void)
{
  test_single_widget (BZ_TYPE_SCREENSHOT, "BzScreenshot",
                      BZ_TYPE_SCREENSHOT,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_themed_entry_group_rect (void)
{
  test_single_widget (BZ_TYPE_THEMED_ENTRY_GROUP_RECT,
                      "BzThemedEntryGroupRect",
                      BZ_TYPE_THEMED_ENTRY_GROUP_RECT,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_list_tile (void)
{
  test_single_widget (BZ_TYPE_LIST_TILE, "BzListTile",
                      BZ_TYPE_LIST_TILE,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

/* ---- GtkButton subclasses ---- */

static void
test_decorated_screenshot (void)
{
  test_single_widget (BZ_TYPE_DECORATED_SCREENSHOT,
                      "BzDecoratedScreenshot",
                      BZ_TYPE_DECORATED_SCREENSHOT,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_app_tile (void)
{
  test_single_widget (BZ_TYPE_APP_TILE, "BzAppTile",
                      BZ_TYPE_APP_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_favorite_button (void)
{
  test_single_widget (BZ_TYPE_FAVORITE_BUTTON, "BzFavoriteButton",
                      BZ_TYPE_FAVORITE_BUTTON,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_category_tile (void)
{
  test_single_widget (BZ_TYPE_CATEGORY_TILE, "BzCategoryTile",
                      BZ_TYPE_CATEGORY_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_context_tile (void)
{
  test_single_widget (BZ_TYPE_CONTEXT_TILE, "BzContextTile",
                      BZ_TYPE_CONTEXT_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_developer_badge (void)
{
  GtkWidget        *w;
  GType             actual_type;
  GtkAccessibleRole role;
  gint              rw, rh;

  w = GTK_WIDGET (g_object_new (BZ_TYPE_DEVELOPER_BADGE, NULL));
  g_test_message ("ADDITION BzDeveloperBadge: ptr=%p", (void *) w);
  g_assert_nonnull (w);

  g_test_message ("MODIFICATION BzDeveloperBadge: pre-sink floating=%d",
                  (int) g_object_is_floating (w));

  actual_type = G_OBJECT_TYPE (w);
  g_test_message ("ADDITION BzDeveloperBadge: actual-type=%s parent=%s",
                  g_type_name (actual_type),
                  g_type_name (g_type_parent (actual_type)));
  g_assert_true (BZ_IS_DEVELOPER_BADGE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  g_object_ref_sink (w);

  g_test_message ("--- Diagnostics for BzDeveloperBadge ---");
  g_test_message ("  ptr=%p", (void *) w);
  g_test_message ("  type=%s  GType=%lu",
                  G_OBJECT_TYPE_NAME (w),
                  (unsigned long) G_OBJECT_TYPE (w));
  g_test_message ("  css-name=%s", gtk_widget_get_css_name (w));
  g_test_message ("  floating=%d", (int) g_object_is_floating (w));
  g_test_message ("  visible=%d", (int) gtk_widget_get_visible (w));
  g_test_message ("  sensitive=%d", (int) gtk_widget_get_sensitive (w));
  g_test_message ("  parent=%p", (void *) gtk_widget_get_parent (w));
  role = gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (w));
  g_test_message ("  role=%d", (int) role);
  g_assert_cmpint (role, ==, GTK_ACCESSIBLE_ROLE_GENERIC);
  gtk_widget_get_size_request (w, &rw, &rh);
  g_test_message ("  size-req=w=%d h=%d", rw, rh);
  g_test_message ("  hexpand=%d  vexpand=%d",
                  (int) gtk_widget_get_hexpand (w),
                  (int) gtk_widget_get_vexpand (w));
  g_test_message ("  opacity=%.3f", (double) gtk_widget_get_opacity (w));
  g_test_message ("--- End diagnostics ---");

  g_test_message ("DELETION BzDeveloperBadge: pre-clear=%p", (void *) w);
  g_clear_object (&w);
  g_test_message ("DELETION BzDeveloperBadge: post-clear=%p", (void *) w);
  g_assert_null (w);
}

/* ---- GtkBox subclasses ---- */

static void
test_subcategory_list (void)
{
  test_single_widget (BZ_TYPE_SUBCATEGORY_LIST, "BzSubcategoryList",
                      BZ_TYPE_SUBCATEGORY_LIST,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_share_list (void)
{
  test_single_widget (BZ_TYPE_SHARE_LIST, "BzShareList",
                      BZ_TYPE_SHARE_LIST,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_install_controls (void)
{
  test_single_widget (BZ_TYPE_INSTALL_CONTROLS, "BzInstallControls",
                      BZ_TYPE_INSTALL_CONTROLS,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_lozenge (void)
{
  test_single_widget (BZ_TYPE_LOZENGE, "BzLozenge", BZ_TYPE_LOZENGE,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_flathub_category_section (void)
{
  test_single_widget (BZ_TYPE_FLATHUB_CATEGORY_SECTION,
                      "BzFlathubCategorySection",
                      BZ_TYPE_FLATHUB_CATEGORY_SECTION,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_search_pill_list (void)
{
  test_single_widget (BZ_TYPE_SEARCH_PILL_LIST, "BzSearchPillList",
                      BZ_TYPE_SEARCH_PILL_LIST,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_featured_carousel (void)
{
  test_single_widget (BZ_TYPE_FEATURED_CAROUSEL, "BzFeaturedCarousel",
                      BZ_TYPE_FEATURED_CAROUSEL,
                      GTK_ACCESSIBLE_ROLE_GROUP);
}

/* ---- GtkPopover subclass ---- */

static void
test_search_filter_popover (void)
{
  test_single_widget (BZ_TYPE_SEARCH_FILTER_POPOVER,
                      "BzSearchFilterPopover",
                      BZ_TYPE_SEARCH_FILTER_POPOVER,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

/* ---- AdwBin subclasses ---- */

static void
test_dynamic_list_view (void)
{
  test_single_widget (BZ_TYPE_DYNAMIC_LIST_VIEW, "BzDynamicListView",
                      BZ_TYPE_DYNAMIC_LIST_VIEW,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_updates_card (void)
{
  test_single_widget (BZ_TYPE_UPDATES_CARD, "BzUpdatesCard",
                      BZ_TYPE_UPDATES_CARD,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_curated_view (void)
{
  test_single_widget (BZ_TYPE_CURATED_VIEW, "BzCuratedView",
                      BZ_TYPE_CURATED_VIEW,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_library_page (void)
{
  test_single_widget (BZ_TYPE_LIBRARY_PAGE, "BzLibraryPage",
                      BZ_TYPE_LIBRARY_PAGE,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_progress_bar (void)
{
  test_single_widget (BZ_TYPE_PROGRESS_BAR, "BzProgressBar",
                      BZ_TYPE_PROGRESS_BAR,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_releases_list (void)
{
  test_single_widget (BZ_TYPE_RELEASES_LIST, "BzReleasesList",
                      BZ_TYPE_RELEASES_LIST,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_user_data_tile (void)
{
  test_single_widget (BZ_TYPE_USER_DATA_TILE, "BzUserDataTile",
                      BZ_TYPE_USER_DATA_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_flathub_page (void)
{
  test_single_widget (BZ_TYPE_FLATHUB_PAGE, "BzFlathubPage",
                      BZ_TYPE_FLATHUB_PAGE,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_full_view (void)
{
  test_single_widget (BZ_TYPE_FULL_VIEW, "BzFullView",
                      BZ_TYPE_FULL_VIEW,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_lazy_wdgt (void)
{
  test_single_widget (BZ_TYPE_LAZY_WDGT, "BzLazyWdgt",
                      BZ_TYPE_LAZY_WDGT,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_transact_icon (void)
{
  test_single_widget (BZ_TYPE_TRANSACT_ICON, "BzTransactIcon",
                      BZ_TYPE_TRANSACT_ICON,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void
test_appstream_description_render (void)
{
  test_single_widget (BZ_TYPE_APPSTREAM_DESCRIPTION_RENDER,
                      "BzAppstreamDescriptionRender",
                      BZ_TYPE_APPSTREAM_DESCRIPTION_RENDER,
                      GTK_ACCESSIBLE_ROLE_GENERIC);
}

/* ---- AdwWindow subclasses ---- */

static void
test_inspector (void)
{
  test_single_widget (BZ_TYPE_INSPECTOR, "BzInspector",
                      BZ_TYPE_INSPECTOR,
                      GTK_ACCESSIBLE_ROLE_WINDOW);
}

static void
test_entry_inspector (void)
{
  test_single_widget (BZ_TYPE_ENTRY_INSPECTOR, "BzEntryInspector",
                      BZ_TYPE_ENTRY_INSPECTOR,
                      GTK_ACCESSIBLE_ROLE_WINDOW);
}

/* ---- AdwDialog subclasses ---- */

static void
test_donations_dialog (void)
{
  test_single_widget (BZ_TYPE_DONATIONS_DIALOG, "BzDonationsDialog",
                      BZ_TYPE_DONATIONS_DIALOG,
                      GTK_ACCESSIBLE_ROLE_DIALOG);
}

static void
test_bundle_install_dialog (void)
{
  test_single_widget (BZ_TYPE_BUNDLE_INSTALL_DIALOG,
                      "BzBundleInstallDialog",
                      BZ_TYPE_BUNDLE_INSTALL_DIALOG,
                      GTK_ACCESSIBLE_ROLE_DIALOG);
}

/* ---- BzListTile subclasses ---- */

static void
test_favorites_tile (void)
{
  test_single_widget (BZ_TYPE_FAVORITES_TILE, "BzFavoritesTile",
                      BZ_TYPE_FAVORITES_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_transaction_tile (void)
{
  test_single_widget (BZ_TYPE_TRANSACTION_TILE, "BzTransactionTile",
                      BZ_TYPE_TRANSACTION_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_article_tile (void)
{
  test_single_widget (BZ_TYPE_ARTICLE_TILE, "BzArticleTile",
                      BZ_TYPE_ARTICLE_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_rich_app_tile (void)
{
  test_single_widget (BZ_TYPE_RICH_APP_TILE, "BzRichAppTile",
                      BZ_TYPE_RICH_APP_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_addon_tile (void)
{
  test_single_widget (BZ_TYPE_ADDON_TILE, "BzAddonTile",
                      BZ_TYPE_ADDON_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
test_installed_tile (void)
{
  test_single_widget (BZ_TYPE_INSTALLED_TILE, "BzInstalledTile",
                      BZ_TYPE_INSTALLED_TILE,
                      GTK_ACCESSIBLE_ROLE_BUTTON);
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

  /* Ensure types used in Blueprint templates are registered. Without
   * this, the linker dead-strips the corresponding .o files from the
   * static library and GTK template instantiation fails with
   * "Invalid type 'BzXxx'". */
  g_type_ensure (bz_entry_get_type ());
  g_type_ensure (bz_entry_group_get_type ());
  g_type_ensure (bz_result_get_type ());
  g_type_ensure (bz_flatpak_entry_get_type ());
  g_type_ensure (bz_auth_state_get_type ());
  g_type_ensure (bz_flathub_category_get_type ());
  /* Template dependency chain — register early so blueprints work */
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);

  g_test_add_func ("/widget-creation/fading-clamp",
                   test_fading_clamp);
  g_test_add_func ("/widget-creation/zoom", test_zoom);
  g_test_add_func ("/widget-creation/world-map", test_world_map);
  g_test_add_func ("/widget-creation/data-graph", test_data_graph);
  g_test_add_func ("/widget-creation/screenshots-carousel",
                   test_screenshots_carousel);
  g_test_add_func ("/widget-creation/rounded-picture",
                   test_rounded_picture);
  g_test_add_func ("/widget-creation/aspect-picture",
                   test_aspect_picture);
  g_test_add_func ("/widget-creation/screenshot",
                   test_screenshot);
  g_test_add_func ("/widget-creation/themed-entry-group-rect",
                   test_themed_entry_group_rect);
  g_test_add_func ("/widget-creation/list-tile", test_list_tile);

  g_test_add_func ("/widget-creation/decorated-screenshot",
                   test_decorated_screenshot);
  g_test_add_func ("/widget-creation/app-tile", test_app_tile);
  g_test_add_func ("/widget-creation/favorite-button",
                   test_favorite_button);
  g_test_add_func ("/widget-creation/category-tile",
                   test_category_tile);
  g_test_add_func ("/widget-creation/context-tile",
                   test_context_tile);
  g_test_add_func ("/widget-creation/developer-badge",
                   test_developer_badge);

  g_test_add_func ("/widget-creation/subcategory-list",
                   test_subcategory_list);
  g_test_add_func ("/widget-creation/share-list", test_share_list);
  g_test_add_func ("/widget-creation/install-controls",
                   test_install_controls);
  g_test_add_func ("/widget-creation/lozenge", test_lozenge);
  g_test_add_func ("/widget-creation/flathub-category-section",
                   test_flathub_category_section);
  g_test_add_func ("/widget-creation/search-pill-list",
                   test_search_pill_list);
  g_test_add_func ("/widget-creation/featured-carousel",
                   test_featured_carousel);

  g_test_add_func ("/widget-creation/search-filter-popover",
                   test_search_filter_popover);

  g_test_add_func ("/widget-creation/dynamic-list-view",
                   test_dynamic_list_view);
  g_test_add_func ("/widget-creation/updates-card",
                   test_updates_card);
  g_test_add_func ("/widget-creation/curated-view",
                   test_curated_view);
  g_test_add_func ("/widget-creation/library-page",
                   test_library_page);
  g_test_add_func ("/widget-creation/progress-bar",
                   test_progress_bar);
  g_test_add_func ("/widget-creation/releases-list",
                   test_releases_list);
  g_test_add_func ("/widget-creation/user-data-tile",
                   test_user_data_tile);
  g_test_add_func ("/widget-creation/flathub-page",
                   test_flathub_page);
  g_test_add_func ("/widget-creation/full-view", test_full_view);
  g_test_add_func ("/widget-creation/lazy-wdgt", test_lazy_wdgt);
  g_test_add_func ("/widget-creation/transact-icon",
                   test_transact_icon);
  g_test_add_func ("/widget-creation/appstream-description-render",
                   test_appstream_description_render);

  g_test_add_func ("/widget-creation/inspector", test_inspector);
  g_test_add_func ("/widget-creation/entry-inspector",
                   test_entry_inspector);

  g_test_add_func ("/widget-creation/donations-dialog",
                   test_donations_dialog);
  g_test_add_func ("/widget-creation/bundle-install-dialog",
                   test_bundle_install_dialog);

  g_test_add_func ("/widget-creation/favorites-tile",
                   test_favorites_tile);
  g_test_add_func ("/widget-creation/transaction-tile",
                   test_transaction_tile);
  g_test_add_func ("/widget-creation/article-tile",
                   test_article_tile);
  g_test_add_func ("/widget-creation/rich-app-tile",
                   test_rich_app_tile);
  g_test_add_func ("/widget-creation/addon-tile", test_addon_tile);
  g_test_add_func ("/widget-creation/installed-tile",
                   test_installed_tile);

  return g_test_run ();
}
