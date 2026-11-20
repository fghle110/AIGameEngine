# Warning configuration for first-party targets only. Third-party code keeps its own flags.
function(ae_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:preprocessor
            /Zc:__cplusplus
            /Zc:inline
            /utf-8
            /wd4251   # dll-interface: static runtime, not applicable
            /wd4324   # structure was padded due to alignment specifier (deliberate in math types)
            /wd4458   # declaration of 'x' hides class member (avoided in new code, kept for parity with external headers)
        )
        if(AE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wdouble-promotion
            -Wformat=2
        )
        if(AE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
