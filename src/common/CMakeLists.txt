add_library(apollo-common
    Version.cpp
    SourceFile.cpp
    Listing.cpp
)

add_library(apollo::common ALIAS apollo-common)

target_include_directories(apollo-common
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(apollo-common PUBLIC cxx_std_20)
