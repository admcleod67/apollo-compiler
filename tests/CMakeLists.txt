add_executable(apollo_smoke_test
    smoke_test.cpp
)

target_link_libraries(apollo_smoke_test
    PRIVATE
        apollo::common
)

target_compile_features(apollo_smoke_test PRIVATE cxx_std_20)

add_test(NAME apollo_smoke_test COMMAND apollo_smoke_test)
add_test(NAME apolloc_version COMMAND $<TARGET_FILE:apolloc> --version)
