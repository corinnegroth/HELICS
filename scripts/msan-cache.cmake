set(CMAKE_C_FLAGS "-fsanitize=memory -fno-omit-frame-pointer -g -O1" CACHE STRING "Flags used by the compiler during all build types.")
set(CMAKE_CXX_FLAGS "-fsanitize=memory -fno-omit-frame-pointer -g -O1" CACHE STRING "Flags used by the compiler during all build types.")
set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=memory" CACHE STRING "Flags used by the linker during all build types.")
set(CMAKE_MODULE_LINKER_FLAGS "-fsanitize=memory" CACHE STRING "Flags used by the linker during all build types.")