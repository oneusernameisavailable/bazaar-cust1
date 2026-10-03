/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CZPaths — single source of truth for the custom build's on-disk paths.
 *
 * The label database location used to be spelled out as a literal
 * "io.github.kolunmi.Bazaar" in three separate translation units
 * (bz-full-view.c, bz-window.c, cz-custom-page.c).  That is a silent
 * data-loss hazard: renaming the app ID in one place and not the others
 * gives three different databases with no error reported anywhere.
 *
 * Everything here goes through g_get_user_data_dir(), so the resulting
 * path honours $XDG_DATA_HOME on every distribution and falls back to
 * ~/.local/share per the XDG base directory spec.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Application ID. Kept identical to the schema ID and the Flatpak app
 * ID so the database, the GSettings keys and the installed launcher all
 * agree. Change it in one commit, everywhere. */
#define CZ_APP_ID "io.github.kolunmi.Bazaar"

/* Baseline filename of the label database. */
#define CZ_DB_BASENAME "custom-labels.db"

/*
 * Returns a newly allocated absolute path to $XDG_DATA_HOME/CZ_APP_ID,
 * or NULL if the user data dir is unavailable. Free with g_free().
 */
char *
cz_user_data_dir (void);

/*
 * Returns a newly allocated absolute path to the label database.
 * Free with g_free().
 */
char *
cz_labels_db_path (void);

/*
 * Returns a newly allocated absolute path to the backup directory
 * ($XDG_DATA_HOME/CZ_APP_ID/backups). Free with g_free().
 */
char *
cz_backups_dir (void);

G_END_DECLS