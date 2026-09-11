/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CzAppReport — app + label CSV report export.
 *
 * Generates a comma-separated-value report covering every entry group in
 * the state: name, app id, matching custom-tab categories, core label,
 * custom labels and the four per-app review notes (aesthetics / usability /
 * features / issues).  RFC-4180 quoting is used.  Reports are written to
 * <db_dir>/reports/bazaar-app-report-<timestamp>.csv and pruned to a fixed
 * retention count, mirroring the backup retention in BzLabelStore.
 */

#ifndef CZ_APP_REPORT_H
#define CZ_APP_REPORT_H

#include <glib.h>

#include "bz-label-store.h"
#include "bz-state-info.h"

G_BEGIN_DECLS

char *
cz_app_report_suggest_path (const char *db_path);

void
cz_app_report_prune (const char *reports_dir,
                     guint       max);

gboolean
cz_app_report_generate (BzLabelStore *store,
                        BzStateInfo  *state,
                        const char   *out_path,
                        GError      **error);

G_END_DECLS

#endif /* CZ_APP_REPORT_H */