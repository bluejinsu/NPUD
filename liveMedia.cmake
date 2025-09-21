set(LIVE_MEDIA_DEFAULT_INCLUDE_DIR "/usr/local/include/liveMedia")
set(LIVE_MEDIA_DEFAULT_LIBRARY_DIR "/usr/local/lib")

find_path(LIVE_MEDIA_INCLUDE_DIR
    NAMES liveMedia.hh
    PATHS ${LIVE_MEDIA_DEFAULT_INCLUDE_DIR}
    NO_DEFAULT_PATH)

find_library(LIVE_MEDIA_LIBRARY
    NAMES liveMedia
    PATHS ${LIVE_MEDIA_DEFAULT_LIBRARY_DIR}
    NO_DEFAULT_PATH)

if (LIVE_MEDIA_INCLUDE_DIR AND LIVE_MEDIA_LIBRARY)
    message(STATUS "Found liveMedia: ${LIVE_MEDIA_LIBRARY}")
    message(STATUS "Using liveMedia include directory: ${LIVE_MEDIA_INCLUDE_DIR}")

    set(LIVE_MEDIA_INCLUDE_DIRS 
            ${LIVE_MEDIA_INCLUDE_DIR}
            ${LIVE_MEDIA_INCLUDE_DIR}/../BasicUsageEnvironment/ 
            ${LIVE_MEDIA_INCLUDE_DIR}/../groupsock/
            ${LIVE_MEDIA_INCLUDE_DIR}/../UsageEnvironment/)

    set(LIVE_MEDIA_LIBRARIES
            ${LIVE_MEDIA_LIBRARY}
            libgroupsock.a
            libBasicUsageEnvironment.a
            libUsageEnvironment.a
            )

    include_directories(${LIVE_MEDIA_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "LiveMedia library not found.")
endif()