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

executable(
  'swash',
  swash_sources,
  swash_resources,
  dependencies: [gtk_dep, adw_dep, m_dep],
  install: true,
)
