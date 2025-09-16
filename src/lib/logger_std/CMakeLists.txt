add_library(logger_std STATIC
    logger_std.cpp
    logger_std.h
)

if(ENABLE_DEBUG_TIMINGS)
    target_compile_definitions(logger_std PRIVATE ENABLE_DEBUG_TIMINGS=1)
endif()