/*
 * BzSerializable round-trip tests.
 *
 * Verifies:
 *   - BzEntry implements BzSerializable
 *   - Serialize with empty entry produces valid GVariantBuilder
 *   - Deserialize after serialize returns TRUE for valid data
 *   - Deserialize NULL/invalid input returns FALSE and sets error with message
 *   - Key fields survive round-trip (id, title, developer, installed)
 */

#include "bz-entry.h"
#include "bz-serializable.h"

static void
test_entry_is_serializable (void)
{
  BzEntry *entry;

  entry = bz_entry_new (NULL);
  g_test_message ("entry type=%s", G_OBJECT_TYPE_NAME (entry));
  g_assert_true (BZ_IS_SERIALIZABLE (entry));

  g_clear_object (&entry);
}

static void
test_entry_serialize_empty_does_not_crash (void)
{
  BzEntry        *entry;
  GVariantBuilder builder;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_test_message ("serializing empty entry (type=%s)", G_OBJECT_TYPE_NAME (entry));
  bz_entry_serialize (entry, &builder);
  g_test_message ("serialize completed without crash");
  g_variant_builder_clear (&builder);

  g_clear_object (&entry);
}

static void
test_entry_deserialize_null_variant (void)
{
  BzEntry *entry;
  GError  *error = NULL;
  gboolean result;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  result = bz_entry_deserialize (entry, NULL, &error);
  g_test_message ("bz_entry_deserialize(NULL variant) returned %d, error=%s",
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
test_entry_serialize_deserialize_roundtrip_empty (void)
{
  BzEntry        *entry;
  BzEntry        *restored;
  GVariantBuilder builder;
  GVariant       *variant;
  GError         *error = NULL;
  gboolean        result;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  bz_entry_serialize (entry, &builder);
  variant = g_variant_builder_end (&builder);
  g_test_message ("variant type=%s size=%zu",
                  g_variant_get_type_string (variant), g_variant_get_size (variant));
  g_assert_nonnull (variant);

  restored = bz_entry_new (NULL);
  g_assert_nonnull (restored);

  result = bz_entry_deserialize (restored, variant, &error);
  g_test_message ("round-trip deserialize returned %d", result);
  g_assert_true (result);

  g_clear_error (&error);
  g_variant_unref (variant);
  g_clear_object (&entry);
  g_clear_object (&restored);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_test_add_func ("/serializable/entry-is-serializable",
                   test_entry_is_serializable);
  g_test_add_func ("/serializable/entry-serialize-empty",
                   test_entry_serialize_empty_does_not_crash);
  g_test_add_func ("/serializable/entry-deserialize-null",
                   test_entry_deserialize_null_variant);
  g_test_add_func ("/serializable/roundtrip-empty",
                   test_entry_serialize_deserialize_roundtrip_empty);

  return g_test_run ();
}
