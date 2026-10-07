# Strict warnings for Tidefield's own targets (not third-party code).
function(tf_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference)
    endif()
endfunction()
