/* Author: MikeEviscerate <cpe.bach03@proton.me>                  */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */



/* Any functions declared here will be defined at the end of xm.c */


/* if you wanna disable my attempt at C89 static_assert, comment the #define below out */ 
#define USE__XM_STATIC_ASSERT_POLYFILL


/* few changes for allowing further optimisations on more compilers */
/* assume() for replacing asserts() when compiled for release */

#if defined(_MSC_VER) && _MSC_VER >= 1310   /* VS .NET 2003 and later */ 
    #define assume(x)  do { bool y = (x); __assume(y);} while(0)
#elif defined(__clang__)  
    #define assume(x)  do { bool y = (x); __builtin_assume(y); } while(0)
#elif defined(__GNUC__) 
    #if __GNUC__ >= 13
        #define assume(x)  do { bool y = (x); __attribute__((assume(y)));} while(0)
    #else
        #define assume(x)  do { if (!(x)) {__builtin_unreachable();} } while(0)
    #endif
#else
    #define assume(x) do {(x);} while(0)
#endif


/* C99 inline and restrict fallback */

/* lol, restrict only present for xm_envelope_lerp */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    #define INLINE inline
    #define RESTRICT restrict
#elif defined(_MSC_VER) && _MSC_VER >= 1400
    #define INLINE __inline
    #define RESTRICT __restrict
    #pragma message("RESTRICT (aka __restrict) will not propagate like C99 restrict would.")
#elif defined(__GNUC__) || defined(__clang__)
    #define INLINE __inline__
    #define RESTRICT __restrict__
#else 
    #define INLINE 
    #define RESTRICT 
#endif









/* C11 alignof fallback */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L 
    #if __STDC_VERSION__ < 202311L
        #include <stdalign.h>
    #endif
#else
    /* I don't trust this entirely, but no choice before C11 */
    #define alignof(type) offsetof(struct { char c; type a; }, a)
#endif











/* C99 math.h functions */
#include <math.h>

#if !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
    static INLINE float _XM_LOG2F(float x) {
        float y;
        y = (float)log(x);
        return y * 1.44269502162933349609375f;
    }
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    /* no need to do anything on a conforming compiler that supports C99 */
#elif defined(_MSC_VER) 
    #if _MSC_VER < 1800 
        /* They added log2f and exp2f quite late. VS 2013. */
        #define log2f(x) _XM_LOG2F(x)
        #define exp2f(x) (float)pow(2.0, x)
    #endif
    #if _MSC_VER < 1310
        /* I am assuming they added sqrtf in VS .NET 2003 */
        #define sqrtf(x) (float)sqrt(x)
    #endif
#else
    /* C89 compat code */
    #define log2f(x) _XM_LOG2F(x)
    #define exp2f(x) (float)pow(2.0, x)
    #define sqrtf(x) (float)sqrt(x)
#endif














/* This project uses static_assert at global scope, function scope and within
structs. No portable way to implement that works for all three cases. Have to
use separate definitions for each special case */

/* C11 static_assert equivalents */
#include <assert.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

    /* Implicit semi-colon at the end, DO NOT use semi-colons for this in structs */
    #define _XM_STATIC_ASSERT_STRUCT(expr, msg)     static_assert(expr, msg); 

    #define _XM_STATIC_ASSERT_GLOBAL(expr, msg)     static_assert(expr, msg) 

    /* Cannot be used within the space for variable declarations. */
    #define _XM_STATIC_ASSERT_FUNCTION(expr, msg)   static_assert(expr, msg) 



#elif defined(USE__XM_STATIC_ASSERT_POLYFILL)

    #define __XM_JOIN_4_INNER(a, b, c, d) a##b##c##d
    #define __XM_JOIN_4(a, b, c, d) __XM_JOIN_4_INNER(a, b, c, d)

    /* I am aware global and struct's case have nearly the same definition. But error messages ought to be better this way */
    
    /* Implicit semi-colon at the end, DO NOT use semi-colons for this in structs */
    #define _XM_STATIC_ASSERT_STRUCT(expr, msg) \
            struct __XM_JOIN_4(_static_assertion_failed_at_, __LINE__ , _in_ , _XM_FILE_NAME) { char __XM_JOIN_4(_, __LINE__, _ , _XM_FILE_NAME)[ (expr) ? 1 : -1 ]; };

    #define _XM_STATIC_ASSERT_GLOBAL(expr, msg) \
            struct __XM_JOIN_4(_static_assertion_failed_at_, __LINE__ , _in_ , _XM_FILE_NAME) { char __XM_JOIN_4(_, __LINE__, _ , _XM_FILE_NAME)[ (expr) ? 1 : -1 ]; }
    
    
    /* weird preprocessor bug in VC 6.0. Idk about .NET 2002 and 2003 */
    #if defined(_MSC_VER) && _MSC_VER < 1400 
        /* Cannot be used within the space for declarations. */
        #define _XM_STATIC_ASSERT_FUNCTION(expr, msg) \
            do { typedef char _static_assertion_failed_[(expr) ? 1 : -1]; } while(0)
    #else
        /* Cannot be used within the space for declarations. */
        #define _XM_STATIC_ASSERT_FUNCTION(expr, msg) \
            do { typedef char __XM_JOIN_4(_static_assertion_failed_at_, __LINE__ , _in_ , _XM_FILE_NAME)[(expr) ? 1 : -1]; } while(0)
    #endif

#else 
    /* Implicit semi-colon at the end, DO NOT use semi-colons for this in structs */
    #define _XM_STATIC_ASSERT_STRUCT(expr, msg) 

    #define _XM_STATIC_ASSERT_GLOBAL(expr, msg) 

    /* Cannot be used within the space for declarations. */
    #define _XM_STATIC_ASSERT_FUNCTION(expr, msg) 
#endif















/*C23 checked integer arithmetic */

/* for pre C23 compat */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#include <stdckdint.h>

#define _xm_ckd_add_u8_uu _xm_ckd_add
#define _xm_ckd_sub_u8_uu _xm_ckd_sub
#define _xm_ckd_add_u16_ui _xm_ckd_add
#define _xm_ckd_add_u16_uu _xm_ckd_add
#define _xm_ckd_add_u32_uu _xm_ckd_add
#define _xm_ckd_sub_u32_uu _xm_ckd_sub
#define _xm_ckd_mul_u32_uu _xm_ckd_mul

#else
    /*I know it's bad to do it this way, but I linker error otherwise*/
static INLINE bool _xm_ckd_add_u8_uu(uint8_t* result, uint8_t x, uint8_t y) {
    uint16_t res = x;
    res += y;
    if (res > UINT8_MAX) {
        *result = (uint8_t)(res & UINT8_MAX);
        return true;
    }
    *result = (uint8_t)res;
    return false;
}

static INLINE bool _xm_ckd_sub_u8_uu(uint8_t* result, uint8_t x, uint8_t y) {
    int16_t res = x;
    res -= y;
    if (res < 0) {
        *result = (uint8_t)(res & UINT8_MAX);
        return true;
    }
    *result = (uint8_t)res;
    return false;
}

static INLINE bool _xm_ckd_add_u16_uu(uint16_t* result, uint16_t x, uint16_t y) {
    uint32_t res = x;
    res += y;
    if (res > UINT16_MAX) {
        *result = (uint16_t)(res & UINT16_MAX);
        return true;
    }
    *result = (uint16_t)res;
    return false;
}

static INLINE bool _xm_ckd_add_u16_ui(uint16_t* result, uint16_t x, int16_t y) {
    int32_t res = x;
    res += y;
    if (res > UINT16_MAX) {
        *result = (uint16_t)(res & UINT16_MAX);
        return true;
    }
    *result = (uint16_t)res;
    return false;
}

static INLINE bool _xm_ckd_add_u32_uu(uint32_t* result, uint32_t x, uint32_t y) {
    uint64_t res = x;
    res += y;
    if (res > UINT32_MAX) {
        *result = (uint32_t)(res & UINT32_MAX);
        return true;
    }
    *result = (uint32_t)res;
    return false;
}

static INLINE bool _xm_ckd_sub_u32_uu(uint32_t* result, uint32_t x, uint32_t y) {
    int64_t res = x;
    res -= y;
    if (res < 0) {
        *result = (uint32_t)(res & UINT32_MAX);
        return true;
    }
    *result = (uint32_t)res;
    return false;
}

static INLINE bool _xm_ckd_mul_u32_uu(uint32_t* result, uint32_t x, uint32_t y) {
    uint64_t res = x;
    res *= y;
    if (res > UINT32_MAX) {
        *result = (uint32_t)(res & UINT32_MAX);
        return true;
    }
    *result = (uint32_t)res;
    return false;
}
#endif








/* C23 bit manipulation */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
    /* use the compiler's implementation instead of my potentially crap code */
#include <stdbit.h>

#define _xm_count_ones stdc_count_ones
#else
static INLINE uint8_t _xm_count_ones(uint16_t x) {
    uint8_t i = 0;
    while (x) {
        i += x & 1;
        x = x >> 1;
    }
    return i;
}
#endif



