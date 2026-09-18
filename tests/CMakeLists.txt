add_executable(apollo_smoke_test
    smoke_test.cpp
)

target_link_libraries(apollo_smoke_test
    PRIVATE
        apollo::common
)

target_compile_features(apollo_smoke_test PRIVATE cxx_std_20)

add_executable(apollo_ir_test
    apollo_ir_test.cpp
)

target_link_libraries(apollo_ir_test
    PRIVATE
        apollo::ir
)

target_compile_features(apollo_ir_test PRIVATE cxx_std_20)

add_executable(apollo_codegen_test
    apollo_codegen_test.cpp
)

target_link_libraries(apollo_codegen_test
    PRIVATE
        apollo::codegen
)

target_compile_features(apollo_codegen_test PRIVATE cxx_std_20)

add_executable(ir_emit_test
    ir_emit_test.cpp
)

target_link_libraries(ir_emit_test
    PRIVATE
        apollo::codegen
        apollo::ir
        apollo::common
)

target_compile_features(ir_emit_test PRIVATE cxx_std_20)

add_executable(pascal_ir_emit_test
    pascal_ir_emit_test.cpp
)

target_link_libraries(pascal_ir_emit_test
    PRIVATE
        apollo::codegen
        apollo::pascal
        apollo::ir
)

target_compile_features(pascal_ir_emit_test PRIVATE cxx_std_20)

add_executable(pascal_tbc_golden_test
    pascal_tbc_golden_test.cpp
)

target_link_libraries(pascal_tbc_golden_test
    PRIVATE
        apollo::codegen
        apollo::pascal
        apollo::ir
)

target_compile_features(pascal_tbc_golden_test PRIVATE cxx_std_20)

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

add_executable(apollo_pascal_include_test
    pascal_include_test.cpp
)

target_link_libraries(apollo_pascal_include_test
    PRIVATE
        apollo::pascal
)

target_compile_features(apollo_pascal_include_test PRIVATE cxx_std_20)
target_compile_definitions(apollo_pascal_include_test
    PRIVATE APOLLO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

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

add_executable(pascal_ir_lower_test
    pascal_ir_lower_test.cpp
)

target_link_libraries(pascal_ir_lower_test
    PRIVATE
        apollo::pascal
        apollo::ir
)

target_compile_features(pascal_ir_lower_test PRIVATE cxx_std_20)

add_test(NAME apollo_smoke_test COMMAND apollo_smoke_test)
add_test(NAME apollo_ir_test COMMAND apollo_ir_test)
add_test(NAME apollo_codegen_test COMMAND apollo_codegen_test)
add_test(NAME ir_emit_test COMMAND ir_emit_test)
add_test(NAME pascal_ir_emit_test COMMAND pascal_ir_emit_test)
add_test(
    NAME pascal_tbc_golden_test
    COMMAND pascal_tbc_golden_test ${CMAKE_SOURCE_DIR}/tests/fixtures/golden
)
add_test(NAME apollo_source_listing_test COMMAND apollo_source_listing_test)
add_test(NAME apollo_location_diagnostics_test COMMAND apollo_location_diagnostics_test)
add_test(NAME apollo_pascal_scanner_test COMMAND apollo_pascal_scanner_test)
add_test(NAME apollo_pascal_include_test COMMAND apollo_pascal_include_test)
add_test(NAME apollo_token_dump_test COMMAND apollo_token_dump_test)
add_test(NAME apollo_pascal_parser_test COMMAND apollo_pascal_parser_test)
add_test(NAME apollo_pascal_symbol_test COMMAND apollo_pascal_symbol_test)
add_test(NAME apollo_pascal_analyse_test COMMAND apollo_pascal_analyse_test)
add_test(NAME pascal_ir_lower_test COMMAND pascal_ir_lower_test)
add_test(NAME apolloc_version COMMAND $<TARGET_FILE:apolloc> --version)
set_tests_properties(apolloc_version PROPERTIES PASS_REGULAR_EXPRESSION "apolloc 0.8.0")

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
    NAME apolloc_check_error
    COMMAND $<TARGET_FILE:apolloc> --check ${CMAKE_SOURCE_DIR}/tests/fixtures/check_error.pas
)
set_tests_properties(apolloc_check_error PROPERTIES WILL_FAIL TRUE)

set(APOLLO_EXAMPLE_PROGRAMS
    hello
    count
    arith
    reals
    arrays
    subprograms
    primes
    sieve
    readsum
    ifelse
    while
    repeat
    point
    case
    include_main
    stdfuncs
)
foreach(name IN LISTS APOLLO_EXAMPLE_PROGRAMS)
    add_test(
        NAME apolloc_check_${name}
        COMMAND $<TARGET_FILE:apolloc> --check ${CMAKE_SOURCE_DIR}/examples/${name}.pas
    )
endforeach()

add_test(
    NAME apolloc_ir_hello
    COMMAND $<TARGET_FILE:apolloc> --ir ${CMAKE_SOURCE_DIR}/examples/hello.pas
)
set_tests_properties(apolloc_ir_hello PROPERTIES PASS_REGULAR_EXPRESSION "Function main")

add_test(
    NAME apolloc_ir_count
    COMMAND $<TARGET_FILE:apolloc> --ir ${CMAKE_SOURCE_DIR}/examples/count.pas
)
set_tests_properties(apolloc_ir_count PROPERTIES PASS_REGULAR_EXPRESSION "for\\.head")

add_test(
    NAME apolloc_ir_error
    COMMAND $<TARGET_FILE:apolloc> --ir ${CMAKE_SOURCE_DIR}/tests/fixtures/check_error.pas
)
set_tests_properties(apolloc_ir_error PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME apolloc_emit_hello
    COMMAND $<TARGET_FILE:apolloc> --emit ${CMAKE_SOURCE_DIR}/examples/hello.pas
)
set_tests_properties(apolloc_emit_hello PROPERTIES PASS_REGULAR_EXPRESSION "Hello, Gemini")

add_test(
    NAME apolloc_emit_count
    COMMAND $<TARGET_FILE:apolloc> --emit ${CMAKE_SOURCE_DIR}/examples/count.pas
)
set_tests_properties(apolloc_emit_count PROPERTIES PASS_REGULAR_EXPRESSION "main\\$for\\.head")

add_test(
    NAME apolloc_emit_primes
    COMMAND $<TARGET_FILE:apolloc> --emit ${CMAKE_SOURCE_DIR}/examples/primes.pas
)
set_tests_properties(apolloc_emit_primes PROPERTIES PASS_REGULAR_EXPRESSION "CALL isPrime")

add_test(
    NAME apolloc_emit_error
    COMMAND $<TARGET_FILE:apolloc> --emit ${CMAKE_SOURCE_DIR}/tests/fixtures/check_error.pas
)
set_tests_properties(apolloc_emit_error PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME apolloc_tbc_hello
    COMMAND $<TARGET_FILE:apolloc> --tbc ${CMAKE_SOURCE_DIR}/examples/hello.pas
)
set_tests_properties(apolloc_tbc_hello PROPERTIES PASS_REGULAR_EXPRESSION "Hello, Gemini")
