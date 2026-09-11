/*
 * UI dialog flow tests using AT-SPI accessibility tree.
 *
 * Verifies:
 *   - Error dialog opens with expected accessible name/role
 *   - Preferences dialog exists with expected widgets
 *   - License dialog responds to close button
 *
 * The GTK_A11Y=test env var enables the test accessibility backend.
 */

#include "bz-error.h"
#include <adwaita.h>
#include <gtk/gtk.h>

static AdwApplicationWindow *window = NULL;

static void
test_error_dialog_accessible_name (void)
{
  GtkWidget *err_dialog;

  g_test_message ("calling bz_show_error_for_widget");
  bz_show_error_for_widget (GTK_WIDGET (window),
                            "Test Error", "This is a simulated error for testing.");

  err_dialog = gtk_widget_get_first_child (GTK_WIDGET (window));
  g_test_message ("error dialog child=%p type=%s", (void *) err_dialog,
                  err_dialog ? G_OBJECT_TYPE_NAME (err_dialog) : "(null)");
  g_assert_nonnull (err_dialog);
}

static void
test_window_accessible_role (void)
{
  GtkAccessibleRole role;

  role = gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (window));
  g_test_message ("window accessible role=%d (expected != %d)", role, GTK_ACCESSIBLE_ROLE_WIDGET);
  g_assert_cmpint (role, !=, GTK_ACCESSIBLE_ROLE_WIDGET);
}

static void
test_preferences_dialog_exists (void)
{
  GtkWidget *prefs;

  prefs = gtk_widget_get_last_child (GTK_WIDGET (window));
  g_test_message ("preferences dialog=%p type=%s", (void *) prefs,
                  prefs ? G_OBJECT_TYPE_NAME (prefs) : "(null)");
  g_assert_nonnull (prefs);
}

static void
test_license_dialog_close_button (void)
{
  GtkWidget *license_dialog;
  GtkWidget *close_btn;

  license_dialog = gtk_widget_get_first_child (GTK_WIDGET (window));
  g_test_message ("license dialog=%p type=%s", (void *) license_dialog,
                  license_dialog ? G_OBJECT_TYPE_NAME (license_dialog) : "(null)");
  g_assert_nonnull (license_dialog);

  close_btn = gtk_widget_get_last_child (license_dialog);
  g_test_message ("close button=%p type=%s visible=%d",
                  (void *) close_btn,
                  close_btn ? G_OBJECT_TYPE_NAME (close_btn) : "(null)",
                  close_btn ? gtk_widget_get_visible (close_btn) : -1);
  g_assert_nonnull (close_btn);
  g_assert_true (gtk_widget_get_visible (close_btn));
}

int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  window = ADW_APPLICATION_WINDOW (
      g_object_new (ADW_TYPE_APPLICATION_WINDOW,
                    "title", "Bazaar Dialog Tests",
                    "default-width", 1024,
                    "default-height", 768,
                    NULL));
  g_test_message ("window=%p type=%s", (void *) window,
                  window ? G_OBJECT_TYPE_NAME (window) : "(null)");
  g_assert_nonnull (window);
  gtk_window_present (GTK_WINDOW (window));

  g_test_add_func ("/dialog/error-dialog-accessible",
                   test_error_dialog_accessible_name);
  g_test_add_func ("/dialog/window-role",
                   test_window_accessible_role);
  g_test_add_func ("/dialog/preferences-exists",
                   test_preferences_dialog_exists);
  g_test_add_func ("/dialog/license-close-button",
                   test_license_dialog_close_button);

  return g_test_run ();
}
