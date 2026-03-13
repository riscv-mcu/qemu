if get_option('plugins')
  cc = meson.get_compiler('c')
  dl = cc.find_library('dl', required: false)

  shared_module('nice',
    files('nice.c'),
    include_directories: [
      include_directories('../../include/qemu'),
      include_directories('.'),
    ],
    dependencies: [glib, dl],
    name_prefix: 'lib',
    install: true,
    install_dir: get_option('prefix') / 'lib' / 'qemu',
  )
endif
