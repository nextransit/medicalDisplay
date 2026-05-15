ai_engine_inc = include_directories('include')

ai_engine_src = files(
    'src/ai_engine_impl.cpp'
)

ai_engine_dep = declare_dependency(
    include_directories: ai_engine_inc,
    sources: ai_engine_src,
    dependencies: [
        common_dep,
        threads_dep
    ]
)
