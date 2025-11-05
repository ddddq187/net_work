add_executable (test_util
  test_util.cc
  ${PROJECT_SOURCE_DIR}/libsponge/util/util.cc
)

target_include_directories(test_util PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/libsponge
  ${PROJECT_SOURCE_DIR}/libsponge/util
)


