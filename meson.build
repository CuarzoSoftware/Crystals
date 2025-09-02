project(
    'Crystals',
    'c', 'cpp',
    version : '0.1.0',
    meson_version: '>= 0.60.0',
    default_options: [
        'warning_level=2',
        'cpp_std=c++20',
    ]
)

wayland_server_dep  = dependency('wayland-server')
libinput_dep        = dependency('libinput')
cz_louvre_dep       = dependency('cz-louvre')
cz_core_dep         = dependency('cz-core')
cz_skia_dep         = dependency('cz-skia')
cz_ream_dep         = dependency('cz-ream')
cz_kay_dep          = dependency('cz-kay')

executable(
    'crystals',
    sources : run_command('find', './src', '-type', 'f', '-name', '*[.cpp,.c]', check : false).stdout().strip().split('\n'),
    include_directories: ['./src'],
    dependencies : [
        wayland_server_dep,
        libinput_dep,
        cz_louvre_dep,
        cz_core_dep,
        cz_skia_dep,
        cz_ream_dep,
        cz_kay_dep
    ],
    install : true)
