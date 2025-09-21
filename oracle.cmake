# oracle.cmake

set(ORACLE_DIR "${CMAKE_SOURCE_DIR}/../instantclient_19_25/")
set(ORACLE_INCLUDE_DIR "${ORACLE_DIR}/sdk/include")
set(ORACLE_LIBRARY_DIR "${ORACLE_DIR}/")

# Find OCCI include directory
find_path(ORACLE_INCLUDE
    NAMES occi.h
    PATHS ${ORACLE_INCLUDE_DIR}
    NO_DEFAULT_PATH
)

# Find OCCI library
find_library(OCCI_LIBRARY
    NAMES occi
    PATHS ${ORACLE_LIBRARY_DIR}
    NO_DEFAULT_PATH
)

# Find Oracle client library
find_library(CLNTSH_LIBRARY
    NAMES clntsh
    PATHS ${ORACLE_LIBRARY_DIR}
    NO_DEFAULT_PATH
)

# Check if all required components are found
if (ORACLE_INCLUDE AND OCCI_LIBRARY AND CLNTSH_LIBRARY)
    message(STATUS "Found Oracle OCCI: ${OCCI_LIBRARY}")
    message(STATUS "Found Oracle Client: ${CLNTSH_LIBRARY}")
    message(STATUS "Using Oracle include directory: ${ORACLE_INCLUDE}")

    # Set variables for later usage
    set(ORACLE_FOUND TRUE)
    set(ORACLE_LIBRARIES ${OCCI_LIBRARY} ${CLNTSH_LIBRARY})
    set(ORACLE_INCLUDE_DIRS ${ORACLE_INCLUDE})

    # Add include directories
    include_directories(${ORACLE_INCLUDE_DIR})
else()
    message(FATAL_ERROR "Oracle libraries not found. Please ensure they are installed and available.")
endif()
