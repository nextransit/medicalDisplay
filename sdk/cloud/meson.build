cloud_inc = include_directories('include')

cloud_src = files(
    'src/cloud_agent_impl.cpp'
)

cloud_dep = declare_dependency(
    include_directories: cloud_inc,
    sources: cloud_src,
    dependencies: [
        common_dep,
        threads_dep
    ]
)
