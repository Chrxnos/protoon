cmake_minimum_required(VERSION 3.15)
project(Protoon VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Protoon executable
add_executable(Protoon
    main.cpp
)

# Link Windows libraries
target_link_libraries(Protoon
    ntdll
)

# Compiler flags
if(MSVC)
    target_compile_options(Protoon PRIVATE /W4)
else()
    target_compile_options(Protoon PRIVATE -Wall -Wextra)
endif()

# Set output directory
set_target_properties(Protoon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
