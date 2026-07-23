/* Author: MikeEviscerate <cpe.bach03@proton.me>                  */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#pragma once

/* other compilers will complain if #error isn't a valid preprocessor extension anyway */
#if !defined(_MSC_VER)
    #error "Use it only with msvc."
#endif
/* definining stdint stuff IF there's not an existing polyfill in use */
#if !defined(INT32_MAX)
    #define UINT8_MAX  0xFF
    #define UINT16_MAX 0xFFFF
    #define UINT32_MAX 0xFFFFFFFF
    #define UINT64_MAX 0xFFFFFFFFFFFFFFFF

    #define INT8_MIN  -128
    #define INT16_MIN -32768
    #define INT32_MIN -2147483648
    #define INT64_MIN -9223372036854775808

    #define INT8_MAX  127
    #define INT16_MAX 32767
    #define INT32_MAX 2147483647
    #define INT64_MAX 9223372036854775807



    typedef signed    __int8       int8_t;
    typedef unsigned  __int8      uint8_t;
    typedef signed   __int16      int16_t;
    typedef unsigned __int16     uint16_t;
    typedef signed   __int32      int32_t;
    typedef unsigned __int32     uint32_t;
    typedef signed   __int64      int64_t;
    typedef unsigned __int64     uint64_t;

    #if !defined(UINTPTR_MAX) 
        #if defined(_WIN64)
            #define UINTPTR_MAX UINT64_MAX
            typedef int64_t intptr_t;
            typedef uint64_t uintptr_t;
        #elif defined(_WIN32)
            #define UINTPTR_MAX UINT32_MAX
            typedef int32_t intptr_t;
            typedef uint32_t uintptr_t;
        #endif
    #endif
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#elif defined(__cplusplus)
#elif defined(__bool_true_false_are_defined) && __bool_true_false_are_defined == 1
#else
    #define __bool_true_false_are_defined 1
    typedef uint8_t bool;
    #define true  1
    #define false 0
#endif
