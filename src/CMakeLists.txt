set(SOURCES
    block.c
    buffer.c
    buffer-endian.c
)

add_library(${CMAKE_PROJECT_NAME} STATIC ${SOURCES})
