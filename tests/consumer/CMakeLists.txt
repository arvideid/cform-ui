# One project that finds cform-ui through the installed package alone. CI
# configures this from a build directory outside the source tree, with
# CMAKE_PREFIX_PATH pointing at an `cmake --install --prefix` output that
# names nothing back into this repository - the only proof a downstream
# project can consume the package without private includes.
cmake_minimum_required(VERSION 3.25)
project(cform_consumer LANGUAGES C)

find_package(cform-ui CONFIG REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE cform::cf_base)
