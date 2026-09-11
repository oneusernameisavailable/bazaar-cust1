/*
 * Widget/page/dialog tests for remaining param-required types.
 *
 * Covers 10 types that need simple fixtures (GListModel*, generated
 * data-objects, or basic strings):
 *   BzStatsDialog, BzTransactionListDialog, BzScreenshotPage,
 *   BzAllAppsPage, BzSearchPage, BzArticleListView,
 *   BzFeaturedCarouselView, BzArticle, BzAppsPage (basic+carousel)
 *
 * Run with: GTK_A11Y=test meson test -C build test_param_remaining
 */

#include <adwaita.h>
#include <bge.h>
#include <gtk/gtk.h>
#include <libdex.h>

#include "bz-all-apps-page.h"
#include "bz-apps-page.h"
#include "bz-article-list-view.h"
#include "bz-article.h"
#include "bz-featured-carousel-view.h"
#include "bz-screenshot-page.h"
#include "bz-search-page.h"
#include "bz-state-info.h"
#include "bz-stats-dialog.h"
#include "bz-transaction-list-dialog.h"

/* Template deps to prevent dead-stripping */
#include "bz-async-texture.h"
#include "bz-auth-state.h"
#include "bz-curated-article.h"
#include "bz-curated-articles-info.h"
#include "bz-curated-featured-carousel.h"
#include "bz-entry-group.h"
#include "bz-entry.h"
#include "bz-flathub-category.h"
#include "bz-flatpak-entry.h"
#include "bz-lozenge.h"
#include "bz-result.h"

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
/*  GListModel-based constructors                                     */
/* ------------------------------------------------------------------ */

static void
test_stats_dialog (void)
{
  GListStore *model;
  GListStore *country_model;
  GListModel *got_model;
  GListModel *got_country;
  gint        got_downloads;
  GtkWidget  *w;

  model = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (model);
  country_model = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (country_model);

  w = GTK_WIDGET (bz_stats_dialog_new (
      G_LIST_MODEL (model),
      G_LIST_MODEL (country_model),
      42));
  g_test_message ("ADDITION BzStatsDialog: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_STATS_DIALOG (w));
  g_assert_true (GTK_IS_WIDGET (w));

  g_object_ref_sink (w);
  print_widget_info (w, "BzStatsDialog");

  g_object_get (w, "model", &got_model, NULL);
  g_assert_true (got_model == G_LIST_MODEL (model));
  g_clear_object (&got_model);

  g_object_get (w, "country-model", &got_country, NULL);
  g_assert_true (got_country == G_LIST_MODEL (country_model));
  g_clear_object (&got_country);

  g_object_get (w, "total-downloads", &got_downloads, NULL);
  g_assert_cmpint (got_downloads, ==, 42);

  g_object_set (w, "total-downloads", 99, NULL);
  g_object_get (w, "total-downloads", &got_downloads, NULL);
  g_assert_cmpint (got_downloads, ==, 99);

  g_clear_object (&w);
  g_clear_object (&model);
  g_clear_object (&country_model);
}

static void
test_transaction_list_dialog (void)
{
  GListStore *entries;
  GtkWidget  *w;

  entries = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (entries);

  w = GTK_WIDGET (bz_transaction_list_dialog_new (
      G_LIST_MODEL (entries),
      "Test Heading",
      "Test body with %s",
      "Test body no apps %u",
      "Secondary %u",
      "Cancel",
      "Confirm"));
  g_test_message ("ADDITION BzTransactionListDialog: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_TRANSACTION_LIST_DIALOG (w));
  g_assert_true (GTK_IS_WIDGET (w));
  g_assert_false (bz_transaction_list_dialog_was_confirmed (
      BZ_TRANSACTION_LIST_DIALOG (w)));

  g_object_ref_sink (w);
  print_widget_info (w, "BzTransactionListDialog");

  g_clear_object (&w);
  g_clear_object (&entries);
}

static void
test_screenshot_page (void)
{
  GListStore     *screenshots;
  GListStore     *captions;
  GtkWidget      *source_widget;
  GtkWidget      *w;
  GFile          *f;
  BzAsyncTexture *tex;

  screenshots = g_list_store_new (BZ_TYPE_ASYNC_TEXTURE);
  g_assert_nonnull (screenshots);
  captions = g_list_store_new (GTK_TYPE_STRING_OBJECT);
  g_assert_nonnull (captions);

  f = g_file_new_for_path ("/tmp/dummy.png");
  g_assert_nonnull (f);
  tex = bz_async_texture_new_lazy (f, NULL);
  g_assert_nonnull (tex);
  g_list_store_append (screenshots, tex);
  g_clear_object (&tex);
  g_clear_object (&f);

  source_widget = g_object_new (GTK_TYPE_BOX, NULL);
  g_assert_nonnull (source_widget);

  w = GTK_WIDGET (bz_screenshot_page_new (
      G_LIST_MODEL (screenshots),
      G_LIST_MODEL (captions),
      0,
      source_widget));

  /* source_widget is not parented by the page — sink it before disposal */
  if (g_object_is_floating (source_widget))
    g_object_ref_sink (source_widget);
  else
    g_object_ref (source_widget);

  g_test_message ("ADDITION BzScreenshotPage: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_SCREENSHOT_PAGE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  {
    const char *caption;
    gboolean    zoomed;

    caption = bz_screenshot_page_get_current_caption (
        BZ_SCREENSHOT_PAGE (w));
    g_assert_nonnull (caption);

    g_object_get (w, "is-zoomed", &zoomed, NULL);
    g_assert_false (zoomed);
  }

  g_object_ref_sink (w);
  print_widget_info (w, "BzScreenshotPage");

  g_clear_object (&w);
  g_clear_object (&source_widget);
  g_clear_object (&captions);
  g_clear_object (&screenshots);
}

static void
test_all_apps_page (void)
{
  GListStore *applications;
  GtkWidget  *w;

  applications = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (applications);
  g_object_ref (applications); /* extra ref survives page's ownership */

  {
    const char *title;

    w = GTK_WIDGET (bz_all_apps_page_new (
        "All Apps",
        G_LIST_MODEL (applications)));
    g_test_message ("ADDITION BzAllAppsPage: ptr=%p", (void *) w);
    g_assert_nonnull (w);
    g_assert_true (BZ_IS_ALL_APPS_PAGE (w));
    g_assert_true (GTK_IS_WIDGET (w));

    title = adw_navigation_page_get_title (
        ADW_NAVIGATION_PAGE (w));
    g_assert_cmpstr (title, ==, "All Apps");
  }

  g_object_ref_sink (w);
  print_widget_info (w, "BzAllAppsPage");

  g_clear_object (&w);
  g_clear_object (&applications);
}

static void
test_search_page (void)
{
  BzStateInfo *state;
  GtkWidget   *w;

  state = bz_state_info_new ();
  g_assert_nonnull (state);

  {
    BzStateInfo  *got_state;
    BzEntryGroup *sel;
    gboolean      remove;

    w = GTK_WIDGET (bz_search_page_new (
        state,
        "initial query"));
    g_test_message ("ADDITION BzSearchPage: ptr=%p", (void *) w);
    g_assert_nonnull (w);
    g_assert_true (BZ_IS_SEARCH_PAGE (w));
    g_assert_true (GTK_IS_WIDGET (w));

    got_state = bz_search_page_get_state (BZ_SEARCH_PAGE (w));
    g_assert_true (got_state == state);

    bz_search_page_set_text (BZ_SEARCH_PAGE (w), "new text");
    g_assert_cmpstr (bz_search_page_get_text (
                         BZ_SEARCH_PAGE (w)),
                     ==, "new text");

    sel = bz_search_page_get_selected (
        BZ_SEARCH_PAGE (w), &remove);
    g_assert_null (sel);
  }

  g_object_ref_sink (w);
  print_widget_info (w, "BzSearchPage");

  g_clear_object (&w);
  g_clear_object (&state);
}

/* ------------------------------------------------------------------ */
/*  Tests using generated data-object types (_new(void))              */
/* ------------------------------------------------------------------ */

static void
test_article_list_view (void)
{
  BzCuratedArticlesInfo *articles;
  BzCuratedArticlesInfo *got;
  GtkWidget             *w;

  articles = bz_curated_articles_info_new ();
  g_assert_nonnull (articles);

  w = GTK_WIDGET (bz_article_list_view_new (articles));
  g_test_message ("ADDITION BzArticleListView: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_ARTICLE_LIST_VIEW (w));
  g_assert_true (GTK_IS_WIDGET (w));

  got = bz_article_list_view_get_articles (
      BZ_ARTICLE_LIST_VIEW (w));
  g_assert_true (got == articles);

  g_object_ref_sink (w);
  print_widget_info (w, "BzArticleListView");

  g_clear_object (&w);
  g_clear_object (&articles);
}

static void
test_featured_carousel_view (void)
{
  BzCuratedFeaturedCarousel *carousel;
  BzCuratedFeaturedCarousel *got;
  GtkWidget                 *w;

  carousel = bz_curated_featured_carousel_new ();
  g_assert_nonnull (carousel);

  w = GTK_WIDGET (bz_featured_carousel_view_new (carousel));
  g_test_message ("ADDITION BzFeaturedCarouselView: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_FEATURED_CAROUSEL_VIEW (w));
  g_assert_true (GTK_IS_WIDGET (w));

  got = bz_featured_carousel_view_get_carousel (
      BZ_FEATURED_CAROUSEL_VIEW (w));
  g_assert_true (got == carousel);

  g_object_ref_sink (w);
  print_widget_info (w, "BzFeaturedCarouselView");

  g_clear_object (&w);
  g_clear_object (&carousel);
}

static void
test_article (void)
{
  BzCuratedArticle *article;
  BzCuratedArticle *got;
  GtkWidget        *w;

  article = bz_curated_article_new ();
  g_assert_nonnull (article);

  w = GTK_WIDGET (bz_article_new (article));
  g_test_message ("ADDITION BzArticle: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_ARTICLE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  g_object_get (w, "article", &got, NULL);
  g_assert_true (got == article);

  g_object_ref_sink (w);
  print_widget_info (w, "BzArticle");

  g_clear_object (&w);
  g_clear_object (&article);
}

/* ------------------------------------------------------------------ */
/*  BzAppsPage (basic + carousel)                                     */
/* ------------------------------------------------------------------ */

static void
test_apps_page (void)
{
  GListStore *applications;
  GListStore *extra_apps;
  GListModel *got_extra;
  char       *got_subtitle;
  GtkWidget  *w;

  applications = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (applications);
  extra_apps = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (extra_apps);

  w = GTK_WIDGET (bz_apps_page_new (
      "Apps",
      G_LIST_MODEL (applications)));
  g_test_message ("ADDITION BzAppsPage: ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_APPS_PAGE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  /* title is a CONSTRUCT_ONLY property — verify via widget API */
  g_assert_cmpstr (adw_navigation_page_get_title (
                       ADW_NAVIGATION_PAGE (w)),
                   ==, "Apps");

  /* all-applications round-trip (not CONSTRUCT_ONLY) */
  g_object_set (w, "all-applications", extra_apps, NULL);
  g_object_get (w, "all-applications", &got_extra, NULL);
  g_assert_true (got_extra == G_LIST_MODEL (extra_apps));

  /* page-subtitle round-trip */
  bz_apps_page_set_subtitle (BZ_APPS_PAGE (w), "subtitle");
  g_object_get (w, "page-subtitle", &got_subtitle, NULL);
  g_assert_cmpstr (got_subtitle, ==, "subtitle");
  g_free (got_subtitle);

  g_object_ref_sink (w);
  print_widget_info (w, "BzAppsPage");

  g_clear_object (&w);
  g_clear_object (&applications);
  g_clear_object (&extra_apps);
}

static void
test_apps_page_with_carousel (void)
{
  GListStore *applications;
  GListStore *carousel;
  GListModel *got_carousel;
  GtkWidget  *w;

  applications = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (applications);
  carousel = g_list_store_new (G_TYPE_OBJECT);
  g_assert_nonnull (carousel);

  w = GTK_WIDGET (bz_apps_page_new_with_carousel (
      "Apps",
      G_LIST_MODEL (applications),
      G_LIST_MODEL (carousel)));
  g_test_message ("ADDITION BzAppsPage(carousel): ptr=%p", (void *) w);
  g_assert_nonnull (w);
  g_assert_true (BZ_IS_APPS_PAGE (w));
  g_assert_true (GTK_IS_WIDGET (w));

  g_object_get (w, "carousel-applications", &got_carousel, NULL);
  g_assert_true (got_carousel == G_LIST_MODEL (carousel));

  g_object_ref_sink (w);
  print_widget_info (w, "BzAppsPage(carousel)");

  g_clear_object (&w);
  g_clear_object (&carousel);
  g_clear_object (&applications);
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

  /* Template dependency types — ensure they survive dead-stripping */
  g_type_ensure (bz_entry_get_type ());
  g_type_ensure (bz_entry_group_get_type ());
  g_type_ensure (bz_flatpak_entry_get_type ());
  g_type_ensure (bz_auth_state_get_type ());
  g_type_ensure (bz_result_get_type ());
  g_type_ensure (bz_lozenge_get_type ());
  g_type_ensure (bz_async_texture_get_type ());
  g_type_ensure (bz_curated_articles_info_get_type ());
  g_type_ensure (bz_curated_featured_carousel_get_type ());
  g_type_ensure (bz_curated_article_get_type ());
  g_type_ensure (bz_flathub_category_get_type ());

  /* GListModel-based constructors */
  g_test_add_func ("/param-remaining/stats-dialog", test_stats_dialog);
  g_test_add_func ("/param-remaining/transaction-list-dialog",
                   test_transaction_list_dialog);
  g_test_add_func ("/param-remaining/screenshot-page", test_screenshot_page);
  g_test_add_func ("/param-remaining/all-apps-page", test_all_apps_page);
  g_test_add_func ("/param-remaining/search-page", test_search_page);

  /* Generated data-object constructors */
  g_test_add_func ("/param-remaining/article-list-view",
                   test_article_list_view);
  g_test_add_func ("/param-remaining/featured-carousel-view",
                   test_featured_carousel_view);
  g_test_add_func ("/param-remaining/article", test_article);

  /* BzAppsPage variants */
  g_test_add_func ("/param-remaining/apps-page", test_apps_page);
  g_test_add_func ("/param-remaining/apps-page-carousel",
                   test_apps_page_with_carousel);

  return g_test_run ();
}
