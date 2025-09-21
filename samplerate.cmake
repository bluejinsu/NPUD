# curl.cmake

# Set default paths based on platform
if (WIN32)
    # Windows: Assume curl is installed in ${CMAKE_SOURCE_DIR}/../curl
    set(SAMPLERATE_DEFAULT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../curl/include")
    set(SAMPLERATE_DEFAULT_LIBRARY_DIR "${CMAKE_SOURCE_DIR}/../curl/lib")
elseif (UNIX)
    # Linux: Assume curl is installed in /usr/
    set(SAMPLERATE_DEFAULT_INCLUDE_DIR "/usr/local/include")
    set(SAMPLERATE_DEFAULT_LIBRARY_DIR "/usr/local/lib64")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Find the curl include directory
find_path(SAMPLERATE_INCLUDE_DIR
    NAMES samplerate.h
    PATHS ${SAMPLERATE_DEFAULT_INCLUDE_DIR}
    NO_DEFAULT_PATH
)

# Find the curl library
find_library(SAMPLERATE_LIBRARY
    NAMES samplerate
    PATHS ${SAMPLERATE_DEFAULT_LIBRARY_DIR}
    NO_DEFAULT_PATH
)

# Check if both include directory and library are found
if (SAMPLERATE_INCLUDE_DIR AND SAMPLERATE_LIBRARY)
    message(STATUS "Found samplerate: ${SAMPLERATE_LIBRARY}")
    message(STATUS "Using samplerate include directory: ${SAMPLERATE_INCLUDE_DIR}")

    # Set variables for later usage
    set(SAMPLERATE_FOUND TRUE)
    set(SAMPLERATE_LIBRARIES ${SAMPLERATE_LIBRARY})
    set(SAMPLERATE_INCLUDE_DIRS ${SAMPLERATE_INCLUDE_DIR})

    # Add include directories and libraries to the build
    include_directories(${SAMPLERATE_INCLUDE_DIR})
else()
    message(FATAL_ERROR "Samplerate library not found. Please install it in the expected directory.")
endif()