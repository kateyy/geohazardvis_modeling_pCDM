set(target Modeling_pCDM)

message(STATUS "Plugin ${target}")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(sources
    plugin.cpp
    PCDMBackend.h
    PCDMBackend.cpp
    PCDMPlugin.h
    PCDMPlugin.cpp
    PCDMWidget.h
    PCDMWidget.cpp
)

set(UIs
    PCDMWidget.ui
)

qt5_wrap_ui(UI_Srcs ${UIs})

source_group_by_path_and_type(${CMAKE_CURRENT_SOURCE_DIR} ${sources} ${UIs})
source_group_by_path(${CMAKE_CURRENT_BINARY_DIR} ".*" "Generated" ${UI_Srcs} ${QRC_Srcs})
source_group_by_path(${CMAKE_CURRENT_SOURCE_DIR} ".*" "" "CMakeLists.txt")


find_package(Eigen3 REQUIRED)


add_library(${target} SHARED ${sources} ${UI_Srcs} ${QRC_Srcs})

target_include_directories(${target}
    PUBLIC
        ${GEOHAZARDVIS_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(${target}
    PUBLIC
        gui
)

configure_cxx_target(${target} PLUGIN_TARGET)


install(TARGETS ${target}
    RUNTIME DESTINATION ${INSTALL_PLUGINS_BIN}
    LIBRARY DESTINATION ${INSTALL_PLUGINS_SHARED}
    # ARCHIVE DESTINATION ${INSTALL_PLUGINS_LIB}
)
