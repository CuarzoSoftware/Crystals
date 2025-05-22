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

wayland_server_dep = dependency('wayland-server')
louvre_dep = dependency('Louvre')
pixman_dep = dependency('pixman-1')
kay_dep    = dependency('Kay')

executable(
    'crystals',
    sources : run_command('find', './src', '-type', 'f', '-name', '*[.cpp,.c]', check : false).stdout().strip().split('\n'),
    include_directories: ['./src'],
    dependencies : [
        wayland_server_dep,
        louvre_dep,
        pixman_dep,
        kay_dep
    ],
    install : true)
