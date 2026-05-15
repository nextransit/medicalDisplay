linux_platform_inc = include_directories('include')

linux_platform_src = files(
    'src/drm_display.cpp',
    'src/vulkan_renderer.cpp'
)

linux_platform_dep = declare_dependency(
    include_directories: linux_platform_inc,
    sources: linux_platform_src,
    dependencies: [
        common_dep,
        display_engine_dep,
        vulkan_dep,
        libdrm_dep
    ]
)
