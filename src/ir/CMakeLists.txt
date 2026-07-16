add_library(apollo-ir
    Ir.cpp
    IrDump.cpp
)

add_library(apollo::ir ALIAS apollo-ir)

target_include_directories(apollo-ir
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(apollo-ir PUBLIC cxx_std_20)
