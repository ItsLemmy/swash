swash_conf = configuration_data()
swash_conf.set_quoted('SWASH_VERSION', meson.project_version())

swash_sources = files(
  'main.c',
  'canvas-input.c',
  'canvas-layout.c',
  'canvas-render.c',
  'canvas-state.c',
  'document.c',
  'document.h',
  'export.c',
  'export.h',
  'ocr.c',
  'ocr.h',
  'render.c',
  'render.h',
  'stroke.c',
  'stroke.h',
  'types.h',
  'window.c',
  'window.h',
  'window-private.h',
)

configure_file(
  input: 'swash-config.h.in',
  output: 'swash-config.h',
  configuration: swash_conf,
)

swash_resources = gnome.compile_resources(
  'swash-resources',
  'swash.gresource.xml',
  source_dir: 'ui',
  c_name: 'swash',
)

executable(
  'swash',
  swash_sources,
  swash_resources,
  dependencies: [gtk_dep, adw_dep, m_dep],
  install: true,
)

install_data(
  'dev.lemmy.swash.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  'ui/icons/icon.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/128x128/apps'),
  rename: 'dev.lemmy.swash.png',
)

install_data(
  'ui/icons/icon-256.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/256x256/apps'),
  rename: 'dev.lemmy.swash.png',
)

install_data(
  'ui/icons/icon-512.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/512x512/apps'),
  rename: 'dev.lemmy.swash.png',
)
