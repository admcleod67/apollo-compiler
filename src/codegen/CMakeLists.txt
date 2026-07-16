add_library(apollo-codegen
    TbcWriter.cpp
    Emit.cpp
)

add_library(apollo::codegen ALIAS apollo-codegen)

target_include_directories(apollo-codegen
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(apollo-codegen
    PUBLIC
        apollo::common
        apollo::ir
)

target_compile_features(apollo-codegen PUBLIC cxx_std_20)
