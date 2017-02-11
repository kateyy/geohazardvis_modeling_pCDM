set(test_target ${target}-test)
message(STATUS "Test ${test_target}")

set(sources
    main.cpp
    PCDMBackend_test.cpp
)

source_group_by_path_and_type(${CMAKE_CURRENT_SOURCE_DIR} ${sources})
# source_group_by_path(${CMAKE_CURRENT_BINARY_DIR} ".*" "Generated" ...)
source_group_by_path(${CMAKE_CURRENT_SOURCE_DIR} ".*" "" "CMakeLists.txt")

add_executable(${test_target} ${sources})

target_include_directories(${test_target} 
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(${test_target} SYSTEM
    PRIVATE
        ${GTEST_INCLUDE_DIR}
)
target_link_libraries(${test_target}
    PUBLIC
        ${staticTarget}
        ${GTEST_LIBRARIES}
)

configure_cxx_target(${test_target} IDE_FOLDER ${IDE_TESTS_FOLDER})
setupProjectUserConfig(${test_target} GTEST_TARGET)
