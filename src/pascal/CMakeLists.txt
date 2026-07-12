add_library(apollo-pascal
    scanner/Scanner.cpp
    TokenDump.cpp
)

add_library(apollo::pascal ALIAS apollo-pascal)

target_include_directories(apollo-pascal
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(apollo-pascal
    PUBLIC
        apollo::common
)

target_compile_features(apollo-pascal PUBLIC cxx_std_20)
