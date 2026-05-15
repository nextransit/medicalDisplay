demo_exec = executable('medical_display_demo',
    'medical_display_demo.cpp',
    dependencies: [
        ai_engine_dep,
        display_engine_dep,
        dicom_dep,
        cloud_dep,
        linux_platform_dep
    ],
    install: false
)

test('demo', demo_exec)
