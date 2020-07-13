set(SOURCES
    buffer.c
)

add_library(${CMAKE_PROJECT_NAME} STATIC ${SOURCES})

if (MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W3 /WX)
else()
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
