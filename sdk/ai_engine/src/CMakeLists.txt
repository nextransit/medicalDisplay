# AI Engine Platform-specific implementations
add_library(medicaldisplay_ai_platform STATIC)

# Base implementation
target_sources(medicaldisplay_ai_platform PRIVATE
    ai_engine_impl.cpp
)

# Platform-specific
if(LINUX)
    # Linux uses the base implementation
elseif(ANDROID)
    target_sources(medicaldisplay_ai_platform PRIVATE
        ai_engine_android.cpp
    )
endif()

target_link_libraries(medicaldisplay_ai_platform PUBLIC
    medicaldisplay_common
    medicaldisplay_simd
    Threads::Threads
)
