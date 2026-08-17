cmake_minimum_required(VERSION 3.10)
project(meow-meow)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell CMake where to find your headers
include_directories(include)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

# Include LLVM headers
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Your source files
set(SOURCES
    src/main.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/AST.cpp
    src/CodeGen.cpp
    src/Library.cpp
)

# Create executable
add_executable(main.exe ${SOURCES})

# --- LINK LLVM LIBRARIES EXPLICITLY ---


# Link system libraries on Windows
execute_process(COMMAND llvm-config --libs all
    OUTPUT_VARIABLE LLVM_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE)
target_link_libraries(main.exe ${LLVM_LIBS})




if(WIN32)
    target_link_libraries(main.exe
        ole32 shell32 z zstd ntdll uuid
    )
endif()