test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

testsuite_c_args = [
  '-DG_LOG_DOMAIN="ptyxis"',
  '-DDISABLE_AGENT_ENTRY_POINT',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

testsuite = {
  'test-podman' : {},
}

testsuite_deps = [
  ptyxis_agent_deps,
]

foreach test, params: testsuite
  test_exe = executable(test,
    ['@0@.c'.format(test)] + ptyxis_agent_sources,
                 c_args: testsuite_c_args,
           dependencies: testsuite_deps,
    include_directories: [include_directories('..'), include_directories('.'), include_directories('../agent')],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach

split_node_test = executable('test-split-node',
  ['test-split-node.c', '../src/ptyxis-split-node.c'],
  c_args: testsuite_c_args,
  dependencies: dependency('gobject-2.0'),
  include_directories: [include_directories('..'), include_directories('../src')],
)
test('test-split-node', split_node_test, env: test_env)

terminal_picker_test = executable('test-terminal-picker',
  ['test-terminal-picker.c', '../src/ptyxis-terminal-picker-match.c'],
  c_args: testsuite_c_args,
  dependencies: dependency('glib-2.0'),
  include_directories: [include_directories('../src')],
)
test('test-terminal-picker', terminal_picker_test, env: test_env)

terminal_copy_test = executable('test-terminal-copy',
  ['test-terminal-copy.c', '../src/ptyxis-terminal-copy.c'],
  c_args: testsuite_c_args,
  dependencies: dependency('glib-2.0'),
  include_directories: [include_directories('..'), include_directories('../src')],
)
test('test-terminal-copy', terminal_copy_test, env: test_env)

quake_service_test = executable('test-quake-service',
  ['test-quake-service.c', '../src/ptyxis-quake-service.c'],
  c_args: testsuite_c_args,
  dependencies: [dependency('gtk4'), dependency('libportal-gtk4')],
  include_directories: [include_directories('..'), include_directories('../src')],
)
test('test-quake-service', quake_service_test,
  args: [join_paths(meson.project_build_root(), 'data', '@0@.QuakeDaemon.desktop'.format(app_id))],
  env: test_env,
)
