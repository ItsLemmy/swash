swash_resources = gnome.compile_resources(
  'swash-resources',
  'swash.gresource.xml',
  source_dir: meson.current_source_dir(),
  c_name: 'swash',
)

install_data(
  'dev.lemmy.swash.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  'icons/128x128/apps/dev.lemmy.swash.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/128x128/apps'),
)

install_data(
  'icons/256x256/apps/dev.lemmy.swash.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/256x256/apps'),
)

install_data(
  'icons/512x512/apps/dev.lemmy.swash.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/512x512/apps'),
)
