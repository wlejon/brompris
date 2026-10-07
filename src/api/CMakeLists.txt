add_library(brompris_api STATIC
    api.cpp
    native_mpris.cpp
)

target_include_directories(brompris_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(brompris_api PUBLIC
    brompris
    bronze_runtime_shared
)

target_compile_features(brompris_api PUBLIC cxx_std_20)

brompris_warnings(brompris_api)
