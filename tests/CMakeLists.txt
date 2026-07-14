add_executable(apollo_smoke_test
    smoke_test.cpp
)

target_link_libraries(apollo_smoke_test
    PRIVATE
        apollo::common
)

target_compile_features(apollo_smoke_test PRIVATE cxx_std_20)

add_executable(apollo_source_listing_test
    source_listing_test.cpp
)

target_link_libraries(apollo_source_listing_test
    PRIVATE
        apollo::common
)

target_compile_features(apollo_source_listing_test PRIVATE cxx_std_20)

add_executable(apollo_location_diagnostics_test
    location_diagnostics_test.cpp
)

target_link_libraries(apollo_location_diagnostics_test
    PRIVATE
        apollo::common
)

target_compile_features(apollo_location_diagnostics_test PRIVATE cxx_std_20)

add_executable(apollo_pascal_scanner_test
    pascal_scanner_test.cpp
)

target_link_libraries(apollo_pascal_scanner_test
    PRIVATE
        apollo::pascal
)

target_compile_features(apollo_pascal_scanner_test PRIVATE cxx_std_20)

add_executable(apollo_token_dump_test
    token_dump_test.cpp
)

target_link_libraries(apollo_token_dump_test
    PRIVATE
        apollo::pascal
)

target_compile_features(apollo_token_dump_test PRIVATE cxx_std_20)

add_executable(apollo_pascal_parser_test
    pascal_parser_test.cpp
)

target_link_libraries(apollo_pascal_parser_test
    PRIVATE
        apollo::pascal
)

target_compile_features(apollo_pascal_parser_test PRIVATE cxx_std_20)

add_executable(apollo_pascal_symbol_test
    pascal_symbol_test.cpp
)

target_link_libraries(apollo_pascal_symbol_test
    PRIVATE
        apollo::pascal
)

target_compile_features(apollo_pascal_symbol_test PRIVATE cxx_std_20)

add_executable(apollo_pascal_analyse_test
    pascal_analyse_test.cpp
)

target_link_libraries(apollo_pascal_analyse_test
    PRIVATE
        apollo::pascal
)

target_compile_features(apollo_pascal_analyse_test PRIVATE cxx_std_20)

add_test(NAME apollo_smoke_test COMMAND apollo_smoke_test)
add_test(NAME apollo_source_listing_test COMMAND apollo_source_listing_test)
add_test(NAME apollo_location_diagnostics_test COMMAND apollo_location_diagnostics_test)
add_test(NAME apollo_pascal_scanner_test COMMAND apollo_pascal_scanner_test)
add_test(NAME apollo_token_dump_test COMMAND apollo_token_dump_test)
add_test(NAME apollo_pascal_parser_test COMMAND apollo_pascal_parser_test)
add_test(NAME apollo_pascal_symbol_test COMMAND apollo_pascal_symbol_test)
add_test(NAME apollo_pascal_analyse_test COMMAND apollo_pascal_analyse_test)
add_test(NAME apolloc_version COMMAND $<TARGET_FILE:apolloc> --version)
set_tests_properties(apolloc_version PROPERTIES PASS_REGULAR_EXPRESSION "apolloc 0.3.0")

add_test(
    NAME apolloc_list_fixture
    COMMAND $<TARGET_FILE:apolloc> --list ${CMAKE_SOURCE_DIR}/tests/fixtures/list_sample.pas
)
add_test(
    NAME apolloc_list_missing
    COMMAND $<TARGET_FILE:apolloc> --list ${CMAKE_SOURCE_DIR}/tests/fixtures/does-not-exist.pas
)
set_tests_properties(apolloc_list_missing PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME apolloc_tokens_hello
    COMMAND $<TARGET_FILE:apolloc> --tokens ${CMAKE_SOURCE_DIR}/tests/fixtures/scan_hello.pas
)
set_tests_properties(apolloc_tokens_hello PROPERTIES PASS_REGULAR_EXPRESSION "KeywordProgram")

add_test(
    NAME apolloc_tokens_error
    COMMAND $<TARGET_FILE:apolloc> --tokens ${CMAKE_SOURCE_DIR}/tests/fixtures/tokens_error.pas
)
set_tests_properties(apolloc_tokens_error PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME apolloc_ast_hello
    COMMAND $<TARGET_FILE:apolloc> --ast ${CMAKE_SOURCE_DIR}/examples/hello.pas
)
set_tests_properties(apolloc_ast_hello PROPERTIES PASS_REGULAR_EXPRESSION "Program Hello")

add_test(
    NAME apolloc_ast_error
    COMMAND $<TARGET_FILE:apolloc> --ast ${CMAKE_SOURCE_DIR}/tests/fixtures/ast_error.pas
)
set_tests_properties(apolloc_ast_error PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME apolloc_check_hello
    COMMAND $<TARGET_FILE:apolloc> --check ${CMAKE_SOURCE_DIR}/examples/hello.pas
)

add_test(
    NAME apolloc_check_count
    COMMAND $<TARGET_FILE:apolloc> --check ${CMAKE_SOURCE_DIR}/examples/count.pas
)

add_test(
    NAME apolloc_check_error
    COMMAND $<TARGET_FILE:apolloc> --check ${CMAKE_SOURCE_DIR}/tests/fixtures/check_error.pas
)
set_tests_properties(apolloc_check_error PROPERTIES WILL_FAIL TRUE)
