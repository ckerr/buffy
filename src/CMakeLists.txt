set(SOURCES
    block.c
    buffer.c
)

add_library(${CMAKE_PROJECT_NAME} STATIC ${SOURCES})
