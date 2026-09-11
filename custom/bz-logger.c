#include "config.h"

#include <gio/gio.h>
#include <gio/gunixoutputstream.h>
#include <json-glib/json-glib.h>

#include "bz-logger.h"

static GLogWriterOutput
bz_log_writer (GLogLevelFlags   log_level,
               const GLogField *fields,
               gsize            n_fields,
               gpointer         user_data)
{
  JsonBuilder     *builder;
  JsonNode        *root;
  JsonGenerator   *gen;
  g_autofree char *line    = NULL;
  g_autofree char *stamped = NULL;
  GOutputStream   *out;
  gsize            wrote;
  gsize            i;

  builder = json_builder_new ();
  json_builder_begin_object (builder);

  for (i = 0; i < n_fields; i++)
    {
      const char *key = fields[i].key;
      if (g_strcmp0 (key, "GLIB_DOMAIN") == 0 ||
          g_strcmp0 (key, "MESSAGE") == 0 ||
          g_strcmp0 (key, "PRIORITY") == 0 ||
          g_strcmp0 (key, "CODE_FILE") == 0 ||
          g_strcmp0 (key, "CODE_LINE") == 0 ||
          g_strcmp0 (key, "CODE_FUNC") == 0 ||
          g_strcmp0 (key, "SYSLOG_IDENTIFIER") == 0)
        {
          json_builder_set_member_name (builder, key);
          json_builder_add_string_value (builder, fields[i].value);
        }
      else if (g_strcmp0 (key, "PID") == 0)
        {
          json_builder_set_member_name (builder, key);
          json_builder_add_int_value (builder, GPOINTER_TO_INT (fields[i].value));
        }
    }

  json_builder_end_object (builder);
  root = json_builder_get_root (builder);
  gen  = json_generator_new ();
  json_generator_set_root (gen, root);
  line = json_generator_to_data (gen, NULL);
  g_object_unref (gen);
  json_node_unref (root);
  g_object_unref (builder);

  stamped = g_strdup_printf ("%s\n", line);
  out     = g_unix_output_stream_new (1, FALSE);
  g_output_stream_write_all (out, stamped,
                             strlen (stamped), &wrote, NULL, NULL);
  g_object_unref (out);

  return G_LOG_WRITER_HANDLED;
}

void
bz_logger_init (void)
{
  g_log_set_writer_func (bz_log_writer, NULL, NULL);
}

void
bz_log_json (const char    *domain,
             GLogLevelFlags log_level,
             const char    *message,
             ...)
{
  va_list          args;
  g_autofree char *fmt;

  va_start (args, message);
  fmt = g_strdup_vprintf (message, args);
  va_end (args);

  g_log (domain, log_level, "%s", fmt);
}
