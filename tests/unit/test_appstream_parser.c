/*
 * AppStream parser tests.
 *
 * Verifies:
 *   - populate_entry with NULL component returns FALSE and sets BZ_APPSTREAM_ERROR
 *   - bz_appstream_parser_entry_from_metainfo with NULL file returns NULL and sets error
 *   - populate_entry with NULL entry returns FALSE and sets error
 *   - Error messages are non-empty and descriptive
 */

#include "bz-appstream-parser.h"
#include "bz-entry.h"

static void
test_populate_entry_null_component (void)
{
  BzEntry *entry;
  GError  *error = NULL;
  gboolean result;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  result = bz_appstream_parser_populate_entry (entry, NULL,
                                               "/tmp", "flathub", NULL, NULL, NULL, 0, &error);
  g_test_message ("populate_entry(NULL component) returned %d, error=%s",
                  result, error ? error->message : "(null)");
  g_assert_false (result);
  g_assert_nonnull (error);
  g_test_message ("error domain=%s code=%d msg=%s",
                  g_quark_to_string (error->domain), error->code, error->message);
  g_assert_cmpstr (error->message, !=, "");

  g_clear_error (&error);
  g_clear_object (&entry);
}

static void
test_entry_from_metainfo_null_file (void)
{
  BzEntry *entry;
  GError  *error = NULL;

  entry = bz_appstream_parser_entry_from_metainfo (NULL, NULL, &error);
  g_test_message ("entry_from_metainfo(NULL file) returned %p, error=%s",
                  (void *) entry, error ? error->message : "(null)");
  g_assert_null (entry);
  g_assert_nonnull (error);
  g_test_message ("error domain=%s code=%d msg=%s",
                  g_quark_to_string (error->domain), error->code, error->message);
  g_assert_cmpstr (error->message, !=, "");

  g_clear_error (&error);
}

static void
test_populate_entry_null_entry (void)
{
  GError  *error = NULL;
  gboolean result;

  result = bz_appstream_parser_populate_entry (NULL, NULL,
                                               "/tmp", "flathub", NULL, NULL, NULL, 0, &error);
  g_test_message ("populate_entry(NULL entry) returned %d, error=%s",
                  result, error ? error->message : "(null)");
  g_assert_false (result);
  g_assert_nonnull (error);
  g_test_message ("error domain=%s code=%d msg=%s",
                  g_quark_to_string (error->domain), error->code, error->message);
  g_assert_cmpstr (error->message, !=, "");

  g_clear_error (&error);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_test_add_func ("/appstream-parser/populate-null-component",
                   test_populate_entry_null_component);
  g_test_add_func ("/appstream-parser/from-metainfo-null-file",
                   test_entry_from_metainfo_null_file);
  g_test_add_func ("/appstream-parser/populate-null-entry",
                   test_populate_entry_null_entry);

  return g_test_run ();
}
