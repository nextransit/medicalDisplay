dicom_inc = include_directories('include')

dicom_src = files(
    'src/dicom_reader.cpp'
)

dicom_dep = declare_dependency(
    include_directories: dicom_inc,
    sources: dicom_src,
    dependencies: [
        common_dep,
        zlib_dep
    ]
)
