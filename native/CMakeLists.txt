# The manifest is shared by native and Emscripten builds; paths are project-relative.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/sources.txt" PBD_SOURCES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sources.txt)

# Platform bindings remain in the manifest, but the native core has no Emscripten dependency.
list(FILTER PBD_SOURCES EXCLUDE REGEX "/bindings/")
list(TRANSFORM PBD_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")

if(MSVC)
  # 이 디렉터리의 코어와 검사 대상은 예외를 사용하지 않습니다. 기본 /EHsc와의 중복을 제거합니다.
  string(REGEX REPLACE "(^|[ \t])[-/]EHsc([ \t]|$)" "\\1" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

add_library(pbd_core STATIC ${PBD_SOURCES})
target_compile_definitions(pbd_core PUBLIC PARALLEL_EXECUTOR_ENABLE_THREADS=1)
find_package(Threads REQUIRED)
target_link_libraries(pbd_core PUBLIC Threads::Threads)
target_include_directories(pbd_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(pbd_core PUBLIC cxx_std_20)
set_target_properties(pbd_core PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(pbd_core PRIVATE /W4 /WX /utf-8 /permissive-)
  target_compile_options(pbd_core PUBLIC /EHs-c-)
  target_compile_definitions(pbd_core PUBLIC _HAS_EXCEPTIONS=0)
else()
  target_compile_options(pbd_core PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
  target_compile_options(pbd_core PUBLIC -fno-exceptions)
endif()

if(BUILD_TESTING)
  add_executable(pbd_native_tests "${PROJECT_SOURCE_DIR}/tests/native/test_physics.cpp")
  add_executable(pbd_refactoring_tests "${PROJECT_SOURCE_DIR}/tests/native/test_refactoring.cpp")
  add_executable(pbd_parallel_tests "${PROJECT_SOURCE_DIR}/tests/native/test_parallel.cpp")
  add_executable(pbd_no_exception_tests "${PROJECT_SOURCE_DIR}/tests/native/test_no_exceptions.cpp")

  foreach(PBD_TEST IN ITEMS pbd_native_tests pbd_refactoring_tests pbd_parallel_tests pbd_no_exception_tests)
    target_link_libraries(${PBD_TEST} PRIVATE pbd_core)

    # Preserve the documented debugger executable paths after add_subdirectory.
    set_target_properties(${PBD_TEST} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>")

    if(MSVC)
      target_compile_options(${PBD_TEST} PRIVATE /W4 /WX /utf-8 /permissive-)
    else()
      target_compile_options(${PBD_TEST} PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
    endif()

    add_test(NAME ${PBD_TEST} COMMAND ${PBD_TEST})
    set_tests_properties(${PBD_TEST} PROPERTIES TIMEOUT 120)
  endforeach()
endif()
