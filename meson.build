project('demo', 'cpp')

gio = dependency('gio-2.0')
glib = dependency('glib-2.0')

bluetooth_dep = declare_dependency(
  include_directories : include_directories('bluetooth'),
  link_args : ['/workdir/documents/bthidhub/bluetooth/libbluetooth.so']
)

executable('demo', [
        'main.cpp',
    ], 
    dependencies : [
        bluetooth_dep,
        gio, 
        glib,
    ],
)