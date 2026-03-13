if get_option('plugins')
  cc = meson.get_compiler('c')

  if host_os == 'windows'
    shared_module('nice',
      files('nice.c', 'win32_linker.c'),
      include_directories: [
        include_directories('../../include/qemu'),
        include_directories('.'),
      ],
      link_depends: [win32_qemu_plugin_api_lib],
      link_args: ['-Lplugins', '-lqemu_plugin_api'],
      dependencies: glib,
      name_prefix: 'lib',
      install: true,
      install_dir: get_option('prefix') / 'lib' / 'qemu',
    )
  else
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
endif
