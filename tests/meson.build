test_src = files('test_suite.cpp')

test_exec = executable('test_suite', test_src,
    dependencies: [
        ai_engine_dep,
        display_engine_dep,
        dicom_dep
    ]
)

test('unit_tests', test_exec)
