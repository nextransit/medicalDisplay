# DICOM Reader CMakeLists.txt
add_library(medicaldisplay_dicom STATIC
    src/dicom_reader.cpp
    src/dicom_gsdf.c
)

add_library(MedicalDisplay::DICOM ALIAS medicaldisplay_dicom)

target_include_directories(medicaldisplay_dicom PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# zlib for compressed DICOM
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(medicaldisplay_dicom PUBLIC ZLIB::ZLIB)
endif()

install(TARGETS medicaldisplay_dicom
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/dicom
)
