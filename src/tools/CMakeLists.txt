add_executable(apolloc
    apolloc.cpp
)

target_link_libraries(apolloc
    PRIVATE
        apollo::common
        apollo::pascal
        apollo::codegen
)

target_compile_features(apolloc PRIVATE cxx_std_20)
