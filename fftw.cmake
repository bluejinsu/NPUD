# ffw.cmake

# Set default paths based on platform
if (WIN32)
    # Windows: Assume FFTW is installed in ${CMAKE_SOURCE_DIR}/../fftw64
    set(FFTW_DEFAULT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../fftw64/include")
    set(FFTW_DEFAULT_LIBRARY_DIR "${CMAKE_SOURCE_DIR}/../fftw64/lib")
elseif (UNIX)
    # Linux: Assume FFTW is installed in /usr/local
    set(FFTW_DEFAULT_INCLUDE_DIR "/usr/local/include")
    set(FFTW_DEFAULT_LIBRARY_DIR "/usr/local/lib")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Find the FFTW include directory
find_path(FFTW_INCLUDE_DIR
    NAMES fftw3.h
    PATHS ${FFTW_DEFAULT_INCLUDE_DIR}
    NO_DEFAULT_PATH
)

# Find the FFTW library
find_library(FFTW_LIBRARY
    NAMES fftw3
    PATHS ${FFTW_DEFAULT_LIBRARY_DIR}
    NO_DEFAULT_PATH
)

# Check if both include directory and library are found
if (FFTW_INCLUDE_DIR AND FFTW_LIBRARY)
    message(STATUS "Found FFTW: ${FFTW_LIBRARY}")
    message(STATUS "Using FFTW include directory: ${FFTW_INCLUDE_DIR}")

    # Set variables for later usage
    set(FFTW_FOUND TRUE)
    set(FFTW_LIBRARIES ${FFTW_LIBRARY})
    set(FFTW_INCLUDE_DIRS ${FFTW_INCLUDE_DIR})

    # Add include directories and libraries to the build
    include_directories(${FFTW_INCLUDE_DIR})
else()
    message(FATAL_ERROR "FFTW library not found. Please install it in the expected directory.")
endif()