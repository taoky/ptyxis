ptyxis_sources = [
  'gdkhsla.c',
  'main.c',
  'ptyxis-add-button-list-item.c',
  'ptyxis-add-button-list-model.c',
  'ptyxis-application.c',
  'ptyxis-client.c',
  'ptyxis-close-dialog.c',
  'ptyxis-custom-link-editor.c',
  'ptyxis-custom-link-row.c',
  'ptyxis-custom-link.c',
  'ptyxis-find-bar.c',
  'ptyxis-fullscreen-box.c',
  'ptyxis-gated-list-model.c',
  'ptyxis-inspector.c',
  'ptyxis-menu-row.c',
  'ptyxis-palette.c',
  'ptyxis-palette-preview.c',
  'ptyxis-palette-preview-color.c',
  'ptyxis-parking-lot.c',
  'ptyxis-pane.c',
  'ptyxis-preferences-list-item.c',
  'ptyxis-preferences-window.c',
  'ptyxis-profile-editor.c',
  'ptyxis-profile-row.c',
  'ptyxis-profile.c',
  'ptyxis-session.c',
  'ptyxis-settings.c',
  'ptyxis-shortcut-accel-dialog.c',
  'ptyxis-shortcut-row.c',
  'ptyxis-shortcuts.c',
  'ptyxis-shrinker.c',
  'ptyxis-tab.c',
  'ptyxis-tab-monitor.c',
  'ptyxis-terminal.c',
  'ptyxis-theme-selector.c',
  'ptyxis-title-dialog.c',
  'ptyxis-profile-dialog.c',
  'ptyxis-user-palettes.c',
  'ptyxis-util.c',
  'ptyxis-window-dressing.c',
  'ptyxis-window.c',
  'eggshell.c',
]

ptyxis_enum_headers = [
  'ptyxis-pane.h',
  'ptyxis-profile.h',
  'ptyxis-settings.h',
  'ptyxis-tab.h',
  'ptyxis-tab-monitor.h',
]

ptyxis_sources += gnome.mkenums_simple('ptyxis-enums',
            sources: ptyxis_enum_headers,
  identifier_prefix: 'Ptyxis',
      symbol_prefix: 'ptyxis',
)

ptyxis_sources += gnome.compile_resources('ptyxis-resources',
  'ptyxis.gresource.xml',
  c_name: 'ptyxis'
)

ptyxis_sources += vcs_tag(
      command: ['git', 'describe'],
     fallback: meson.project_version(),
        input: 'ptyxis-build-ident.h.in',
       output: 'ptyxis-build-ident.h',
)

schema_config = configuration_data()
schema_config.set('APP_SCHEMA_ID', app_schema_id)
schema_config.set('APP_SCHEMA_PATH', app_schema_path)
schema_config.set('APP_SCHEMA_PROFILE_ID', app_schema_profile_id)
schema_config.set('APP_SCHEMA_SHORTCUTS_ID', app_schema_shortcuts_id)
schema_config.set('APP_SCHEMA_SHORTCUTS_PATH', app_schema_shortcuts_path)
configure_file(
          input: 'org.gnome.Ptyxis.gschema.xml.in',
         output: '@0@.gschema.xml'.format(app_schema_id),
  configuration: schema_config,
    install_dir: get_option('datadir') / 'glib-2.0' / 'schemas',
)

ptyxis_deps = [
  dependency('gio-unix-2.0', version: glib_req),
  dependency('gtk4', version: gtk_req),
  dependency('libadwaita-1', version: adwaita_req),
  dependency('json-glib-1.0', version: json_glib_req),
  dependency('vte-2.91-gtk4', version: vte_req),
  cc.find_library('m', required: false),
]

if host_machine.system() == 'linux'
  ptyxis_deps += [dependency('libportal-gtk4')]
endif

ptyxis_c_args = [
  release_link_args,
  '-DG_LOG_DOMAIN="Ptyxis"',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_@0@'.format(glib_req_version.replace('.', '_')),
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_@0@'.format(glib_req_version.replace('.', '_')),
  '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_@0@'.format(gtk_req_version.replace('.', '_')),
  '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_@0@'.format(gtk_req_version.replace('.', '_')),
  '-DADW_VERSION_MIN_REQUIRED=ADW_VERSION_@0@'.format(adwaita_req_version.replace('.', '_')),
  '-DADW_VERSION_MAX_ALLOWED=ADW_VERSION_@0@'.format(adwaita_req_version.replace('.', '_')),
]

ptyxis_include_directories = [
  include_directories('.'),
  include_directories('..'),
  include_directories('../agent'),
]

executable('ptyxis', ptyxis_sources + ptyxis_agent_ipc,
         dependencies: ptyxis_deps,
              install: true,
               c_args: ptyxis_c_args,
  include_directories: ptyxis_include_directories,
)
