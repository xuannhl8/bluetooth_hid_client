project('demo', 'cpp')

gio = dependency('gio-2.0')
glib = dependency('glib-2.0')

include_bluetooth = include_directories('bluetooth')

executable('demo', [
        'main.cpp',
    ], 
    dependencies : [
        gio, 
        glib,
    ],
    include_directories: include_bluetooth
)