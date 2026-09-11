ui_test_sources = [
  'test_window_widgets.c',
  'test_dialog_flow.c',
  'test_widget_creation.c',
  'test_param_widgets.c',
  'test_param_remaining.c',
  'test_properties.c',
  'test_widget_behavior.c',
]

foreach source : ui_test_sources
  test_name = source.split('.')[0]
  test_exe = executable(test_name,
    source,
    dependencies: bz_dep,
  )
  test(test_name, test_exe,
    protocol: 'exitcode',
    env: ['G_TEST_OPTION_DEFAULTS=1', 'GSETTINGS_BACKEND=memory', 'GTK_A11Y=test'],
    timeout: 60,
  )
endforeach

test_app_launch_exe = executable('test_app_launch', 'test_app_launch.c', dependencies: bz_dep)
# Disabled due to sanitizer interference with GResource registration.
# test('test_app_launch', test_app_launch_exe,
#   args: [bazaar_custom_exe, bazaar_exe],
#   protocol: 'exitcode',
#   env: ['G_TEST_OPTION_DEFAULTS=1', 'GSETTINGS_BACKEND=memory', 'GTK_A11Y=test'],
#   timeout: 120,
# )
