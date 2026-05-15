project('medical_display_sdk', 'cpp',
    version: '1.0.0',
    default_options: [
        'warning_level=2',
        'cpp_std=c++17',
        'optimization=2'
    ]
)

# Dependencies
vulkan_dep = dependency('vulkan', required: true)
threads_dep = dependency('threads')

# Optional dependencies
libdrm_dep = dependency('libdrm', required: false)
zlib_dep = dependency('zlib', required: false)

# Subdirectories
subdir('sdk/common')
subdir('sdk/ai_engine')
subdir('sdk/display_engine')
subdir('sdk/dicom')
subdir('sdk/cloud')

# Platform-specific
if host_machine.system() == 'linux'
    subdir('sdk/platform/linux')
endif

# Examples
subdir('examples/linux')

# Tests
subdir('tests')

# Compiler options
add_project_arguments([
    '-Wall',
    '-Wextra',
    '-Wpedantic',
    '-Wno-unused-parameter'
], language: 'cpp')

# Install
install_headers('sdk/ai_engine/include/', subdir: 'ai_engine')
install_headers('sdk/display_engine/include/', subdir: 'display_engine')
install_headers('sdk/dicom/include/', subdir: 'dicom')
