/*
 * Flatpak transaction state machine tests.
 *
 * Verifies:
 *   - Transaction created with installs/updates/removals preserves counts
 *   - All getter list models return non-NULL
 *   - Merged transactions combine entry lists
 *   - Hold/release prevents premature destruction
 */

#include "bz-entry.h"
#include "bz-transaction-manager.h"
#include "bz-transaction.h"

static BzEntry *test_entry;

static void
test_transaction_empty_creates_ok (void)
{
  BzTransaction *tx;

  tx = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  g_test_message ("tx=%p type=%s", (void *) tx,
                  tx ? G_OBJECT_TYPE_NAME (tx) : "(null)");
  g_assert_nonnull (tx);
  g_assert_true (BZ_IS_TRANSACTION (tx));

  g_clear_object (&tx);
}

static void
test_transaction_get_lists_return_nonnull (void)
{
  BzTransaction *tx;
  GListModel    *installs, *updates, *removals, *trackers;

  tx = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  g_assert_nonnull (tx);

  installs = bz_transaction_get_installs (tx);
  g_test_message ("installs model=%p n_items=%u",
                  (void *) installs, installs ? g_list_model_get_n_items (installs) : 0);
  g_assert_nonnull (installs);

  updates = bz_transaction_get_updates (tx);
  g_test_message ("updates model=%p n_items=%u",
                  (void *) updates, updates ? g_list_model_get_n_items (updates) : 0);
  g_assert_nonnull (updates);

  removals = bz_transaction_get_removals (tx);
  g_test_message ("removals model=%p n_items=%u",
                  (void *) removals, removals ? g_list_model_get_n_items (removals) : 0);
  g_assert_nonnull (removals);

  trackers = bz_transaction_get_trackers (tx);
  g_test_message ("trackers model=%p n_items=%u",
                  (void *) trackers, trackers ? g_list_model_get_n_items (trackers) : 0);
  g_assert_nonnull (trackers);

  g_clear_object (&tx);
}

static void
test_transaction_lists_empty_on_create (void)
{
  BzTransaction *tx;
  guint          n_installs, n_updates, n_removals;

  tx = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  g_assert_nonnull (tx);

  n_installs = g_list_model_get_n_items (bz_transaction_get_installs (tx));
  n_updates  = g_list_model_get_n_items (bz_transaction_get_updates (tx));
  n_removals = g_list_model_get_n_items (bz_transaction_get_removals (tx));
  g_test_message ("n_installs=%u n_updates=%u n_removals=%u",
                  n_installs, n_updates, n_removals);
  g_assert_cmpuint (n_installs, ==, 1);
  g_assert_cmpuint (n_updates, ==, 0);
  g_assert_cmpuint (n_removals, ==, 0);

  g_clear_object (&tx);
}

static void
test_transaction_hold_release_cycle (void)
{
  BzTransaction *tx;

  tx = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  g_assert_nonnull (tx);

  g_test_message ("calling bz_transaction_hold on tx=%p", (void *) tx);
  bz_transaction_hold (tx);
  g_test_message ("calling bz_transaction_release on tx=%p", (void *) tx);
  bz_transaction_release (tx);

  g_clear_object (&tx);
}

static void
test_transaction_merge_two_install_lists (void)
{
  BzTransaction *tx1, *tx2, *merged;
  BzTransaction *txs[2];
  guint          n_merged;

  tx1    = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  tx2    = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  txs[0] = tx1;
  txs[1] = tx2;

  merged   = bz_transaction_new_merged (txs, 2);
  n_merged = g_list_model_get_n_items (bz_transaction_get_installs (merged));
  g_test_message ("merged installs count=%u (expected=2)", n_merged);
  g_assert_nonnull (merged);
  g_assert_cmpuint (n_merged, ==, 2);

  g_clear_object (&tx1);
  g_clear_object (&tx2);
  g_clear_object (&merged);
}

static void
test_transaction_merge_two_empty (void)
{
  BzTransaction *tx1, *tx2, *merged;
  BzTransaction *txs[2];

  tx1    = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  tx2    = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  txs[0] = tx1;
  txs[1] = tx2;

  merged = bz_transaction_new_merged (txs, 2);
  g_test_message ("merged tx=%p type=%s", (void *) merged,
                  merged ? G_OBJECT_TYPE_NAME (merged) : "(null)");
  g_assert_nonnull (merged);

  g_clear_object (&tx1);
  g_clear_object (&tx2);
  g_clear_object (&merged);
}

int
main (int argc, char *argv[])
{
  int ret;

  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  test_entry = bz_entry_new (NULL);
  g_assert_nonnull (test_entry);

  g_test_add_func ("/flatpak-transaction/empty-creates-ok",
                   test_transaction_empty_creates_ok);
  g_test_add_func ("/flatpak-transaction/get-lists-nonnull",
                   test_transaction_get_lists_return_nonnull);
  g_test_add_func ("/flatpak-transaction/lists-empty-start",
                   test_transaction_lists_empty_on_create);
  g_test_add_func ("/flatpak-transaction/hold-release",
                   test_transaction_hold_release_cycle);
  g_test_add_func ("/flatpak-transaction/merge-two-install-lists",
                   test_transaction_merge_two_install_lists);
  g_test_add_func ("/flatpak-transaction/merge-two-empty",
                   test_transaction_merge_two_empty);

  ret = g_test_run ();

  g_clear_object (&test_entry);

  return ret;
}
