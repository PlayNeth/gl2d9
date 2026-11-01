project('gl2d9', 'cpp',
  version : '1.0.0',
  default_options : ['cpp_std=c++17', 'warning_level=2', 'buildtype=release', 'b_ndebug=if-release'])

cpp = meson.get_compiler('cpp')
if host_machine.system() != 'windows' or host_machine.cpu_family() != 'x86'
  error('gl2d9 targets 32-bit Windows; use --cross-file build-win32.txt')
endif

add_project_arguments('-Wno-unused-parameter', '-Wno-missing-field-initializers', '-msse2', '-mfpmath=sse', language : 'cpp')

win_deps = []
foreach lib : ['d3d9', 'gdi32', 'user32']
  win_deps += cpp.find_library(lib)
endforeach

static_link = ['-static', '-static-libgcc', '-static-libstdc++']

# What the three DLLs share: the emulated Voodoo and its renderer.
core = static_library('gl2d9core',
  'src/main.cpp', 'src/util.cpp', 'src/window.cpp', 'src/state.cpp', 'src/texmem.cpp', 'src/shader.cpp',
    'src/render.cpp', 'src/lfb.cpp', 'src/core.cpp', 'src/gu.cpp',
  dependencies : win_deps)

# Glide 2.1 (Voodoo Graphics).
shared_library('glide', 'src/api2.cpp',
  name_prefix : '',
  cpp_args : ['-DGLIDE21'],
  vs_module_defs : 'src/glide.def',
  link_whole : core,
  dependencies : win_deps,
  link_args : static_link + ['-s'],
  install : true)

# Glide 2.x (Voodoo 2).
shared_library('glide2x', 'src/api2.cpp',
  name_prefix : '',
  vs_module_defs : 'src/glide2x.def',
  link_whole : core,
  dependencies : win_deps,
  link_args : static_link + ['-s'],
  install : true)

# Glide 3.x.
shared_library('glide3x', 'src/api3.cpp',
  name_prefix : '',
  vs_module_defs : 'src/glide3x.def',
  link_whole : core,
  dependencies : win_deps,
  link_args : static_link + ['-s'],
  install : true)
