# curl.cmake

# Set default paths based on platform
if (WIN32)
    # Windows: Assume curl is installed in ${CMAKE_SOURCE_DIR}/../curl
    set(CURL_DEFAULT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../curl/include")
    set(CURL_DEFAULT_LIBRARY_DIR "${CMAKE_SOURCE_DIR}/../curl/lib")
elseif (UNIX)
    # Linux: Assume curl is installed in /usr/
    set(CURL_DEFAULT_INCLUDE_DIR "/usr/local/include")
    set(CURL_DEFAULT_LIBRARY_DIR "/usr/local/lib64")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Find the curl include directory
find_path(CURL_INCLUDE_DIR
    NAMES curl/curl.h
    PATHS ${CURL_DEFAULT_INCLUDE_DIR}
    NO_DEFAULT_PATH
)

# Find the curl library
find_library(CURL_LIBRARY
    NAMES curl
    PATHS ${CURL_DEFAULT_LIBRARY_DIR}
    NO_DEFAULT_PATH
)

# Check if both include directory and library are found
if (CURL_INCLUDE_DIR AND CURL_LIBRARY)
    message(STATUS "Found curl: ${CURL_LIBRARY}")
    message(STATUS "Using curl include directory: ${CURL_INCLUDE_DIR}")

    # Set variables for later usage
    set(CURL_FOUND TRUE)
    set(CURL_LIBRARIES ${CURL_LIBRARY})
    set(CURL_INCLUDE_DIRS ${CURL_INCLUDE_DIR})

    # Add include directories and libraries to the build
    include_directories(${CURL_INCLUDE_DIR})
else()
    message(FATAL_ERROR "Curl library not found. Please install it in the expected directory.")
endif()