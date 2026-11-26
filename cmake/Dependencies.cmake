# Dependencies must already exist locally under tools/vendor. Configuration never downloads files.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(tools/vendor/SDL3-3.4.20 EXCLUDE_FROM_ALL)
add_library(mtv-json INTERFACE)
target_include_directories(mtv-json INTERFACE tools/vendor/json/single_include)
