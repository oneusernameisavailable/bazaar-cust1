unit_test_sources = [
  'test_entry_cache_manager.c',
  'test_gobject_lifecycle.c',
  'test_flatpak_transaction.c',
  'test_search_engine.c',
  'test_appstream_parser.c',
  'test_data_graph.c',
  'test_serializable.c',
  'test-bz-label-store.c',
]

foreach source : unit_test_sources
  test_name = source.split('.')[0]
  test_exe = executable(test_name,
    source,
    dependencies: bz_dep,
  )
  test(test_name, test_exe,
    protocol: 'exitcode',
    env: ['G_TEST_OPTION_DEFAULTS=1', 'GSETTINGS_BACKEND=memory', 'G_MESSAGES_DEBUG=all'],
  )
endforeach
