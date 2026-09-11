/* cz-main.c
 *
 * Copyright 2025
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "CUSTOM::MAIN"

#include "config.h"

#include <bge.h>
#include <glib/gi18n.h>
#include <libdex.h>

#include "bz-application.h"
#include "bz-logger.h"
#include "bz-window.h"
#include "cz-custom-page.h"

static GtkWidget *
find_widget_by_type (GtkWidget *widget, GType type)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
    return widget;

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *found = find_widget_by_type (child, type);
      if (found != NULL)
        return found;
    }

  return NULL;
}

static void
noncore_name_added_cb (const char *name, gpointer data)
{
  cz_custom_label_store_add_noncore_label_name (
      CZ_CUSTOM_LABEL_STORE (data), name);
}

static void
noncore_name_removed_cb (const char *name, gpointer data)
{
  cz_custom_label_store_remove_noncore_label_name (
      CZ_CUSTOM_LABEL_STORE (data), name);
}

static gboolean
on_idle_add_tab (gpointer data)
{
  GtkApplication *app = GTK_APPLICATION (data);
  GtkWidget *window = NULL;
  GtkWidget *view_stack = NULL;
  GtkWidget *custom_page = NULL;
  GList *windows = NULL;

  windows = gtk_application_get_windows (app);
  if (windows == NULL)
    return G_SOURCE_CONTINUE;  /* Retry later */

  window = GTK_WIDGET (windows->data);

  view_stack = find_widget_by_type (window, ADW_TYPE_VIEW_STACK);
  if (view_stack == NULL)
    {
      g_warning ("[Custom] AdwViewStack not found");
      goto out;
    }

  custom_page = GTK_WIDGET (cz_custom_page_new ());
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (view_stack),
      custom_page, "custom", _("Custom"), "preferences-other-symbolic");

  /* Hand the custom-label store to the window via callbacks so add/delete
   * events update the store in real time (no src/ → custom/ header dep). */
  {
    CzCustomLabelStore *store
        = cz_custom_page_get_label_store (CZ_CUSTOM_PAGE (custom_page));
    bz_window_set_custom_label_callbacks (
        BZ_WINDOW (window),
        noncore_name_added_cb,
        noncore_name_removed_cb,
        store);
  }

  return G_SOURCE_REMOVE;  /* Done, don't retry */

out:
  g_object_unref (data);
  return G_SOURCE_REMOVE;
}

static void
on_startup (GApplication *app, gpointer user_data)
{
  g_idle_add (on_idle_add_tab, g_object_ref (app));
}

int
main (int argc, char *argv[])
{
  g_autoptr (BzApplication) app = NULL;
  int result = 0;

  if (argc > 1 && g_strcmp0 (argv[1], "--version") == 0)
    {
      g_print ("%s\n", PACKAGE_VCS_VERSION);
      return 0;
    }

  dex_init ();

  bz_logger_init ();

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  bge_init ();

  app = g_object_new (
      BZ_TYPE_APPLICATION,
      "application-id", "io.github.kolunmi.Bazaar",
      "flags", G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_NON_UNIQUE,
      "resource-base-path", "/io/github/kolunmi/Bazaar",
      NULL);

  g_signal_connect (app, "startup", G_CALLBACK (on_startup), NULL);

  result = g_application_run (G_APPLICATION (app), argc, argv);

  return result;
}