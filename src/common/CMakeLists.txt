add_library(apollo-common
    Version.cpp
    SourceFile.cpp
    Listing.cpp
    DiagnosticEngine.cpp
)

add_library(apollo::common ALIAS apollo-common)

target_include_directories(apollo-common
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(apollo-common PUBLIC cxx_std_20)

# Single source of truth: project() VERSION in the top-level CMakeLists.txt.
# Keep the -dev suffix until the first tagged release (v0.1.0 / Milestone 1 complete).
set(APOLLO_VERSION_STRING "${PROJECT_VERSION}-dev")
target_compile_definitions(apollo-common
    PUBLIC "APOLLO_VERSION=\"${APOLLO_VERSION_STRING}\""
)
