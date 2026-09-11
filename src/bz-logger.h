#pragma once

#include <glib.h>

G_BEGIN_DECLS

void
bz_logger_init (void);

void
bz_log_json (const char    *domain,
             GLogLevelFlags log_level,
             const char    *message,
             ...) G_GNUC_PRINTF (3, 4);

G_END_DECLS
