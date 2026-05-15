display_engine_inc = include_directories('include')

display_engine_src = files(
    'src/display_engine_impl.cpp'
)

display_engine_dep = declare_dependency(
    include_directories: display_engine_inc,
    sources: display_engine_src,
    dependencies: [
        common_dep,
        vulkan_dep,
        threads_dep
    ]
)

# Shaders directory
shaders_dir = configure_file(
    output: 'shaders_dep.json',
    command: ['echo', '[]']
)
