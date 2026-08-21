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
