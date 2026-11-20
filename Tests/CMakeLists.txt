# ---------------------------------------------------------------------------
# Aegis test suite. Everything is a real unit/integration test: no manual
# checklists. CTest drives the executables.
# ---------------------------------------------------------------------------
message(STATUS "Aegis: configuring tests")

file(GLOB_RECURSE AE_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

foreach(_excluded IN LISTS AE_EXCLUDE_MODULES)
    list(FILTER AE_TEST_SOURCES EXCLUDE REGEX "/Tests/${_excluded}/")
endforeach()

add_executable(AegisTests ${AE_TEST_SOURCES})
set_target_properties(AegisTests PROPERTIES FOLDER "Tests")
target_include_directories(AegisTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(AegisTests PRIVATE Aegis doctest::doctest)
target_compile_definitions(AegisTests PRIVATE DOCTEST_CONFIG_NO_MULTITHREADING=1)
ae_copy_shaders(AegisTests)

ae_apply_warnings(AegisTests)

add_test(NAME AegisTests COMMAND AegisTests --no-version --order-by=file)
set_tests_properties(AegisTests PROPERTIES TIMEOUT 300 WORKING_DIRECTORY "$<TARGET_FILE_DIR:AegisTests>")

# Test assets (created by the tests at run time) live inside the build tree.
add_custom_command(TARGET AegisTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AegisTests>/TestAssets")
