# Set the directory for la_ddc
set(LA_DDC_DIR "${CMAKE_SOURCE_DIR}/../la_ddc")
set(LA_DDC_INCLUDE_DIR "${LA_DDC_DIR}/include")
set(LA_DDC_LIB_DIR "${LA_DDC_DIR}/build")

# Find the la_ddc shared library (.so)
find_library(LA_DDC_LIB shared
    NAMES la_ddc
    PATHS ${LA_DDC_LIB_DIR}
    NO_DEFAULT_PATH
)

# Check if the library is found
if (LA_DDC_LIB)
    message(STATUS "Found la_ddc library: ${LA_DDC_LIB}")
else()
    message(FATAL_ERROR "la_ddc library not found in ${LA_DDC_LIB_DIR}. Please ensure the library is built.")
endif()

# Add include directories
include_directories(${LA_DDC_INCLUDE_DIR})

link_directories(${LA_DDC_LIB_DIR})

# To use this library in a target, use target_link_libraries in your CMakeLists.txt
set(LA_DDC_LIBRARIES ${LA_DDC_LIB})
