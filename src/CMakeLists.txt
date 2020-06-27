set(SOURCES
    allocator.c
    buffer.c
)

add_library(${CMAKE_PROJECT_NAME} STATIC ${SOURCES})
