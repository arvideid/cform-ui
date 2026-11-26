# One project that finds cform-ui through the installed package alone. CI
# configures this from a build directory outside the source tree, with
# CMAKE_PREFIX_PATH pointing at an `cmake --install --prefix` output that
# names nothing back into this repository - the only proof a downstream
# project can consume the package without private includes.
#
# CFORM_CONSUMER_VERSION is the version the caller is asking for. Named, the
# package file has to agree with it under the compatibility rule the library
# ships, and the program below compares the number the package stated with the
# number the library answers - so a package that installs a header, a library
# and a config file that disagree is a failed build rather than a bug report.
cmake_minimum_required(VERSION 3.25)
project(cform_consumer LANGUAGES C)

if(CFORM_CONSUMER_VERSION)
    find_package(cform-ui ${CFORM_CONSUMER_VERSION} CONFIG REQUIRED EXACT)
else()
    find_package(cform-ui CONFIG REQUIRED)
endif()

message(STATUS "found cform-ui ${cform-ui_VERSION}, backend ABI ${CFORM_UI_BACKEND_ABI_VERSION}")

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE cform::cf_base)

if(CFORM_CONSUMER_VERSION)
    target_compile_definitions(consumer PRIVATE
        CFORM_UI_VERSION_AT_COMPILE="${cform-ui_VERSION}")
endif()
