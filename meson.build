project('ptyxis', 'c',
          version: '50.2',
    meson_version: '>= 1.0.0',
  default_options: [ 'warning_level=2', 'werror=false', 'c_std=gnu11', ],
)

cc = meson.get_compiler('c')

i18n = import('i18n')
gnome = import('gnome')

app_schema_id = get_option('gschema-id')
app_schema_path = get_option('gschema-path')

app_is_generic = get_option('generic') != 'ptyxis'
app_is_builder = get_option('generic') == 'builder'
app_id = get_option('app-id')

if get_option('development')
  if app_id == ''
    app_id = 'org.gnome.Ptyxis.Devel'
  endif
  if app_schema_path == ''
    app_schema_path = '/org/gnome/Ptyxis/Devel/'
  endif
else
  if app_id == ''
    app_id = 'org.gnome.Ptyxis'
  endif
  if app_schema_path == ''
    app_schema_path = '/org/gnome/Ptyxis/'
  endif
endif

if app_schema_id == ''
  if get_option('development')
    app_schema_id = 'org.gnome.Ptyxis.Devel'
  else
    app_schema_id = 'org.gnome.Ptyxis'
  endif
endif

app_schema_profile_id = '@0@.Profile'.format(app_schema_id)
app_schema_shortcuts_id = '@0@.Shortcuts'.format(app_schema_id)
app_schema_shortcuts_path = '@0@Shortcuts/'.format(app_schema_path)

glib_req_version = '2.80'
gtk_req_version = '4.14'
adwaita_req_version = '1.8'
json_glib_req_version = '1.6'
vte_req_version = '0.79'

glib_req = '>= @0@'.format(glib_req_version)
gtk_req = '>= @0@'.format(gtk_req_version)
adwaita_req = '>= @0@'.format(adwaita_req_version)
json_glib_req = '>= @0@'.format(json_glib_req_version)
vte_req = '>= @0@'.format(vte_req_version)

# Various optimizations for non-debug builds including disabling of
# cast checks, asserts, and additional link options.
release_link_args = []
test_link_args = [
  '-Wl,-z,defs',   # Detect and reject underlinking
  '-Wl,-z,now',    # Disable lazy binding
  '-Wl,-z,relro',  # Read-only segments after relocation
]
if not get_option('buildtype').startswith('debug')
  add_global_arguments([
    '-DG_DISABLE_ASSERT',
    '-DG_DISABLE_CAST_CHECKS',
  ], language: 'c')
  test_link_args += ['-Wl,-Bsymbolic', '-fno-plt']
endif

config_h = configuration_data()
config_h.set_quoted('APP_ID', app_id)
config_h.set_quoted('APP_SCHEMA_ID', app_schema_id)
config_h.set_quoted('APP_SCHEMA_PATH', app_schema_path)
config_h.set_quoted('APP_SCHEMA_PROFILE_ID', app_schema_profile_id)
config_h.set_quoted('APP_SCHEMA_SHORTCUTS_ID', app_schema_shortcuts_id)
config_h.set_quoted('APP_SCHEMA_SHORTCUTS_PATH', app_schema_shortcuts_path)
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', 'ptyxis')
config_h.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))
config_h.set_quoted('BINDIR', get_option('prefix') / get_option('bindir'))
config_h.set_quoted('PKGDATADIR', get_option('prefix') / get_option('datadir') / 'ptyxis')
# LIBEXECDIR must be expanded in config.h
config_h.set_quoted('LIBEXECDIR', get_option('prefix') / get_option('libexecdir'))
config_h.set_quoted('PACKAGE_WEBSITE', 'https://gitlab.gnome.org/chergert/ptyxis')
config_h.set_quoted('PACKAGE_ICON_NAME', app_id)
config_h.set_quoted('PACKAGE_NAME', 'Ptyxis')
config_h.set10('DEVELOPMENT_BUILD', get_option('development'))
config_h.set10('APP_IS_GENERIC', app_is_generic)
config_h.set10('APP_IS_BUILDER', app_is_builder)

summary('Application ID', app_id)
summary('GSchema ID Prefix', app_schema_id)
summary('GSchema Path Prefix', app_schema_path)
summary('Development Banner', get_option('development'))
summary('Generic', app_is_generic)

check_functions = [
  ['HAVE_GRANTPT', 'grantpt'],
  ['HAVE_POSIX_OPENPT', 'posix_openpt'],
  ['HAVE_PTSNAME', 'ptsname'],
  ['HAVE_PTSNAME_R', 'ptsname_r'],
  ['HAVE_UNLOCKPT', 'unlockpt'],
]
foreach func: check_functions
  config_h.set(func[0], cc.has_function(func[1]))
endforeach

project_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdeclaration-after-statement',
  '-Werror=address',
  '-Werror=array-bounds',
  '-Werror=empty-body',
  '-Werror=implicit',
  '-Werror=implicit-function-declaration',
  '-Werror=incompatible-pointer-types',
  '-Werror=init-self',
  '-Werror=int-conversion',
  '-Werror=int-to-pointer-cast',
  '-Werror=main',
  '-Werror=misleading-indentation',
  '-Werror=missing-braces',
  '-Werror=missing-include-dirs',
  '-Werror=nonnull',
  '-Werror=overflow',
  '-Werror=parenthesis',
  '-Werror=pointer-arith',
  '-Werror=pointer-to-int-cast',
  '-Werror=redundant-decls',
  '-Werror=return-type',
  '-Werror=sequence-point',
  '-Werror=shadow',
  '-Werror=strict-prototypes',
  '-Werror=trigraphs',
  '-Werror=undef',
  '-Werror=write-strings',
  '-Wformat-nonliteral',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Wlogical-op',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-cast-function-type',
  '-Wno-dangling-pointer',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused',
  '-fno-strict-aliasing',
  ['-Werror=format-security', '-Werror=format=2'],
]
if cc.has_multi_arguments('-Wmissing-declarations')
  project_c_args += '-Wmissing-declarations'
endif
if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif
foreach arg: test_c_args
  if cc.has_multi_arguments(arg)
    project_c_args += arg
  endif
endforeach
add_project_arguments(project_c_args, language: 'c')

foreach link_arg: test_link_args
  if cc.has_link_argument(link_arg)
    release_link_args += link_arg
  endif
endforeach

subdir('data')
subdir('agent')
subdir('src')
subdir('po')
subdir('man')
subdir('testsuite')

configure_file(output: 'config.h', configuration: config_h)

gnome.post_install(
     glib_compile_schemas: true,
    gtk_update_icon_cache: true,
  update_desktop_database: true,
)
