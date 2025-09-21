# jsoncpp.cmake

# Set default paths based on platform
if (WIN32)
    # Windows: Assume jsoncpp is installed in ${CMAKE_SOURCE_DIR}/../jsoncpp_abi
    set(JSONCPP_DEFAULT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../jsoncpp_abi/include")
    set(JSONCPP_DEFAULT_LIBRARY_DIR "${CMAKE_SOURCE_DIR}/../jsoncpp_abi/lib64")
elseif (UNIX)
    # Linux: Assume jsoncpp is installed in /usr/local
    set(JSONCPP_DEFAULT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../jsoncpp_abi/include")
    set(JSONCPP_DEFAULT_LIBRARY_DIR "${CMAKE_SOURCE_DIR}/../jsoncpp_abi/build/lib")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Find the jsoncpp include directory
find_path(JSONCPP_INCLUDE_DIR
    NAMES json/json.h
    PATHS ${JSONCPP_DEFAULT_INCLUDE_DIR}
    NO_DEFAULT_PATH
)

# 기존 검색 우선순위를 백업
set(_original_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})

# 정적 라이브러리만 검색
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# Find the jsoncpp static library
find_library(JSONCPP_LIBRARY
    NAMES libjsoncpp.a
    PATHS ${JSONCPP_DEFAULT_LIBRARY_DIR}
    NO_DEFAULT_PATH
)

# 우선순위 복원
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_original_suffixes})

# Check if both include directory and library are found
if (JSONCPP_INCLUDE_DIR AND JSONCPP_LIBRARY)
    message(STATUS "Found jsoncpp library: ${JSONCPP_LIBRARY}")
    message(STATUS "Using jsoncpp include directory: ${JSONCPP_INCLUDE_DIR}")

    # Set variables for later usage
    set(JSONCPP_FOUND TRUE)
    set(JSONCPP_LIBRARIES ${JSONCPP_LIBRARY})
    set(JSONCPP_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIR})

    # Add include directories and libraries to the build
    include_directories(${JSONCPP_INCLUDE_DIR})
else()
    message(FATAL_ERROR "jsoncpp static library not found. Please install it in the expected directory.")
endif()
