/** @file
 * InnoTek Portable Runtime - Assertions.
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef __iprt_assert_h__
#define __iprt_assert_h__

#include <iprt/cdefs.h>
#include <iprt/types.h>

/** @defgroup grp_rt_assert     Assert - Assertions
 * @ingroup grp_rt
 * @{
 */


/** @def AssertBreakpoint()
 * Assertion Breakpoint.
 *
 * @remark  In the gnu world we add a nop instruction after the int3 to
 *          force gdb to remain at the int3 source line.
 * @remark  The L4 kernel will try make sense of the breakpoint, thus the jmp.
 */
#ifdef RT_STRICT
# ifdef __GNUC__
#  ifndef __L4ENV__
#   define AssertBreakpoint()   do { if (RTAssertDoBreakpoint()) { __asm__ __volatile__ ("int3\n\tnop"); } } while (0)
#  else
#   define AssertBreakpoint()   do { if (RTAssertDoBreakpoint()) { __asm__ __volatile__ ("int3; jmp 1f; 1:"); } } while (0)
#  endif
# elif defined(_MSC_VER)
#  define AssertBreakpoint()    do { if (RTAssertDoBreakpoint()) { __debugbreak(); } } while (0)
# else
#  error "Unknown compiler"
# endif
#else
# define AssertBreakpoint()     do { } while (0)
#endif

/**
 * RTASSERTTYPE is the type the AssertCompile() macro redefines.
 * It has no other function and shouldn't be used.
 * Visual C++ uses this.
 */
typedef int RTASSERTTYPE[1];

/**
 * RTASSERTVAR is the type the AssertCompile() macro redefines.
 * It has no other function and shouldn't be used.
 * GCC uses this.
 */
#ifdef __GNUC__
__BEGIN_DECLS
#endif
extern int RTASSERTVAR[1];
#ifdef __GNUC__
__END_DECLS
#endif

/** @def AssertCompile
 * Asserts that a compile-time expression is true. If it's not break the build.
 * @param   expr    Expression which should be true.
 */
#ifdef __GNUC__
# define AssertCompile(expr)    extern int RTASSERTVAR[(expr) ? 1 : 0] __attribute__((unused))
#else
# define AssertCompile(expr)    typedef int RTASSERTTYPE[(expr) ? 1 : 0]
#endif

/** @def AssertCompileSize
 * Asserts a size at compile.
 * @param   type    The type.
 * @param   size    The expected type size.
 */
#define AssertCompileSize(type, size) \
    AssertCompile(sizeof(type) == (size))

/** @def AssertCompileSizeAlignment
 * Asserts a size alignment at compile.
 * @param   type    The type.
 * @param   align   The size alignment to assert.
 */
#define AssertCompileSizeAlignment(type, align) \
    AssertCompile(!(sizeof(type) & ((align) - 1)))

/** @def AssertCompileMemberAlignment
 * Asserts a member offset alignment at compile.
 * @param   type    The type.
 * @param   member  The member.
 * @param   align   The member offset alignment to assert.
 */
#if defined(__GNUC__) && defined(__cplusplus)
# define AssertCompileMemberAlignment(type, member, align) \
    AssertCompile(!(__builtin_offsetof(type, member) & ((align) - 1)))
#else
# define AssertCompileMemberAlignment(type, member, align) \
    AssertCompile(!(RT_OFFSETOF(type, member) & ((align) - 1)))
#endif 


/** @def AssertCompileMemberSize
 * Asserts a member offset alignment at compile.
 * @param   type    The type.
 * @param   member  The member.
 * @param   size    The member size to assert.
 */
#define AssertCompileMemberSize(type, member, size) \
    AssertCompile(RT_SIZEOFMEMB(type, member) == (size))

/** @def AssertCompileMemberSizeAlignment
 * Asserts a member size alignment at compile.
 * @param   type    The type.
 * @param   member  The member.
 * @param   align   The member size alignment to assert.
 */
#define AssertCompileMemberSizeAlignment(type, member, align) \
    AssertCompile(!(RT_SIZEOFMEMB(type, member) & ((align) - 1)))


/** @def Assert
 * Assert that an expression is true. If it's not hit breakpoint.
 * @param   expr    Expression which should be true.
 */
#ifdef RT_STRICT
# define Assert(expr)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertBreakpoint(); \
        } \
    } while (0)
#else
# define Assert(expr)     do { } while (0)
#endif


/** @def AssertReturn
 * Assert that an expression is true and returns if it isn't.
 * In RT_STRICT mode it will hit a breakpoint before returning.
 *
 * @param   expr    Expression which should be true.
 * @param   rc      What is to be presented to return.
 */
#ifdef RT_STRICT
# define AssertReturn(expr, rc) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertBreakpoint(); \
            return (rc); \
        } \
    } while (0)
#else
# define AssertReturn(expr, rc) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            return (rc); \
    } while (0)
#endif

/** @def AssertReturnVoid
 * Assert that an expression is true and returns if it isn't.
 * In RT_STRICT mode it will hit a breakpoint before returning.
 *
 * @param   expr    Expression which should be true.
 */
#ifdef RT_STRICT
# define AssertReturnVoid(expr) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertBreakpoint(); \
            return; \
        } \
    } while (0)
#else
# define AssertReturnVoid(expr) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            return; \
    } while (0)
#endif


/** @def AssertBreak
 * Assert that an expression is true and breaks if it isn't.
 * In RT_STRICT mode it will hit a breakpoint before doing break.
 *
 * @param   expr    Expression which should be true.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 */
#ifdef RT_STRICT
# define AssertBreak(expr, stmt) \
    if (RT_UNLIKELY(!(expr))) { \
        AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)
#else
# define AssertBreak(expr, stmt) \
    if (RT_UNLIKELY(!(expr))) { \
        stmt; \
        break; \
    } else do {} while (0)
#endif


/** @def AssertMsg
 * Assert that an expression is true. If it's not print message and hit breakpoint.
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 */
#ifdef RT_STRICT
# define AssertMsg(expr, a)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertMsg2 a; \
            AssertBreakpoint(); \
        } \
    } while (0)
#else
# define AssertMsg(expr, a)  do { } while (0)
#endif

/** @def AssertMsgReturn
 * Assert that an expression is true and returns if it isn't.
 * In RT_STRICT mode it will hit a breakpoint before returning.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 * @param   rc      What is to be presented to return.
 */
#ifdef RT_STRICT
# define AssertMsgReturn(expr, a, rc)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertMsg2 a; \
            AssertBreakpoint(); \
            return (rc); \
        } \
    } while (0)
#else
# define AssertMsgReturn(expr, a, rc) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            return (rc); \
    } while (0)
#endif

/** @def AssertMsgReturnVoid
 * Assert that an expression is true and returns if it isn't.
 * In RT_STRICT mode it will hit a breakpoint before returning.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 */
#ifdef RT_STRICT
# define AssertMsgReturnVoid(expr, a)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertMsg2 a; \
            AssertBreakpoint(); \
            return; \
        } \
    } while (0)
#else
# define AssertMsgReturnVoid(expr, a) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            return; \
    } while (0)
#endif


/** @def AssertMsgBreak
 * Assert that an expression is true and breaks if it isn't.
 * In RT_STRICT mode it will hit a breakpoint before doing break.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 */
#ifdef RT_STRICT
# define AssertMsgBreak(expr, a, stmt) \
    if (RT_UNLIKELY(!(expr))) { \
        AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)
#else
# define AssertMsgBreak(expr, a, stmt) \
    if (RT_UNLIKELY(!(expr))) { \
        stmt; \
        break; \
    } else do {} while (0)
#endif


/** @def AssertFailed
 * An assertion failed hit breakpoint.
 */
#ifdef RT_STRICT
# define AssertFailed()  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertBreakpoint(); \
    } while (0)
#else
# define AssertFailed()         do { } while (0)
#endif

/** @def AssertFailedReturn
 * An assertion failed, hit breakpoint (RT_STRICT mode only) and return.
 *
 * @param   rc      The rc to return.
 */
#ifdef RT_STRICT
# define AssertFailedReturn(rc)  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertBreakpoint(); \
        return (rc); \
    } while (0)
#else
# define AssertFailedReturn(rc)  \
    do { \
        return (rc); \
    } while (0)
#endif

/** @def AssertFailedReturnVoid
 * An assertion failed, hit breakpoint (RT_STRICT mode only) and return.
 */
#ifdef RT_STRICT
# define AssertFailedReturnVoid()  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertBreakpoint(); \
        return; \
    } while (0)
#else
# define AssertFailedReturnVoid()  \
    do { \
        return; \
    } while (0)
#endif


/** @def AssertFailedBreak
 * An assertion failed, hit breakpoint (RT_STRICT mode only), execute
 * the given statement and break.
 *
 * @param   stmt    Statement to execute before break.
 */
#ifdef RT_STRICT
# define AssertFailedBreak(stmt) \
    if (1) { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)
#else
# define AssertFailedBreak(stmt) \
    if (1) { \
        stmt; \
        break; \
    } else do {} while (0)
#endif


/** @def AssertMsgFailed
 * An assertion failed print a message and a hit breakpoint.
 *
 * @param   a   printf argument list (in parenthesis).
 */
#ifdef RT_STRICT
# define AssertMsgFailed(a)  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertBreakpoint(); \
    } while (0)
#else
# define AssertMsgFailed(a)     do { } while (0)
#endif

/** @def AssertMsgFailedReturn
 * An assertion failed, hit breakpoint with message (RT_STRICT mode only) and return.
 *
 * @param   a       printf argument list (in parenthesis).
 * @param   rc      What is to be presented to return.
 */
#ifdef RT_STRICT
# define AssertMsgFailedReturn(a, rc)  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertBreakpoint(); \
        return (rc); \
    } while (0)
#else
# define AssertMsgFailedReturn(a, rc)  \
    do { \
        return (rc); \
    } while (0)
#endif

/** @def AssertMsgFailedReturnVoid
 * An assertion failed, hit breakpoint with message (RT_STRICT mode only) and return.
 *
 * @param   a       printf argument list (in parenthesis).
 */
#ifdef RT_STRICT
# define AssertMsgFailedReturnVoid(a)  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertBreakpoint(); \
        return; \
    } while (0)
#else
# define AssertMsgFailedReturnVoid(a)  \
    do { \
        return; \
    } while (0)
#endif


/** @def AssertMsgFailedBreak
 * An assertion failed, hit breakpoint (RT_STRICT mode only), execute
 * the given statement and break.
 *
 * @param   a       printf argument list (in parenthesis).
 * @param   stmt    Statement to execute before break.
 */
#ifdef RT_STRICT
# define AssertMsgFailedBreak(a, stmt) \
    if (1) { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)
#else
# define AssertMsgFailedBreak(a, stmt) \
    if (1) { \
        stmt; \
        break; \
    } else do {} while (0)
#endif


/** @def AssertReleaseBreakpoint()
 * Assertion Breakpoint.
 *
 * @remark  In the gnu world we add a nop instruction after the int3 to
 *          force gdb to remain at the int3 source line.
 * @remark  The L4 kernel will try make sense of the breakpoint, thus the jmp.
 */
#ifdef __GNUC__
# ifndef __L4ENV__
#  define AssertReleaseBreakpoint()   do { __asm__ __volatile__ ("int3\n\tnop"); } while (0)
# else
#  define AssertReleaseBreakpoint()   do { __asm__ __volatile__ ("int3; jmp 1f; 1:"); } while (0)
# endif
#elif defined(_MSC_VER)
# define AssertReleaseBreakpoint()      __debugbreak()
#else
# error "Unknown compiler"
#endif


/** @def AssertRelease
 * Assert that an expression is true. If it's not hit a breakpoint.
 *
 * @param   expr    Expression which should be true.
 */
#define AssertRelease(expr)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertReleaseBreakpoint(); \
        } \
    } while (0)

/** @def AssertReleaseReturn
 * Assert that an expression is true, hit a breakpoing and return if it isn't.
 *
 * @param   expr    Expression which should be true.
 * @param   rc      What is to be presented to return.
 */
#define AssertReleaseReturn(expr, rc)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertReleaseBreakpoint(); \
            return (rc); \
        } \
    } while (0)

/** @def AssertReleaseReturnVoid
 * Assert that an expression is true, hit a breakpoing and return if it isn't.
 *
 * @param   expr    Expression which should be true.
 */
#define AssertReleaseReturnVoid(expr)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertReleaseBreakpoint(); \
            return; \
        } \
    } while (0)


/** @def AssertReleaseBreak
 * Assert that an expression is true, hit a breakpoing and break if it isn't.
 *
 * @param   expr    Expression which should be true.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 */
#define AssertReleaseBreak(expr, stmt)  \
    if (RT_UNLIKELY(!(expr))) { \
        AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertReleaseBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)



/** @def AssertReleaseMsg
 * Assert that an expression is true, print the message and hit a breakpoint if it isn't.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 */
#define AssertReleaseMsg(expr, a)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertMsg2 a; \
            AssertReleaseBreakpoint(); \
        } \
    } while (0)

/** @def AssertReleaseMsgReturn
 * Assert that an expression is true, print the message and hit a breakpoint and return if it isn't.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 * @param   rc      What is to be presented to return.
 */
#define AssertReleaseMsgReturn(expr, a, rc)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertMsg2 a; \
            AssertReleaseBreakpoint(); \
            return (rc); \
        } \
    } while (0)

/** @def AssertReleaseMsgReturn
 * Assert that an expression is true, print the message and hit a breakpoint and return if it isn't.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 */
#define AssertReleaseMsgReturnVoid(expr, a)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
        { \
            AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertMsg2 a; \
            AssertReleaseBreakpoint(); \
            return; \
        } \
    } while (0)


/** @def AssertReleaseMsgBreak
 * Assert that an expression is true, print the message and hit a breakpoing and break if it isn't.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 */
#define AssertReleaseMsgBreak(expr, a, stmt)  \
    if (RT_UNLIKELY(!(expr))) { \
        AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertReleaseBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)


/** @def AssertReleaseFailed
 * An assertion failed, hit a breakpoint.
 */
#define AssertReleaseFailed()  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertReleaseBreakpoint(); \
    } while (0)

/** @def AssertReleaseFailedReturn
 * An assertion failed, hit a breakpoint and return.
 *
 * @param   rc      What is to be presented to return.
 */
#define AssertReleaseFailedReturn(rc)  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertReleaseBreakpoint(); \
        return (rc); \
    } while (0)

/** @def AssertReleaseFailedReturn
 * An assertion failed, hit a breakpoint and return.
 */
#define AssertReleaseFailedReturnVoid()  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertReleaseBreakpoint(); \
        return; \
    } while (0)


/** @def AssertReleaseFailedBreak
 * An assertion failed, hit a breakpoint and break.
 *
 * @param   stmt    Statement to execute before break.
 */
#define AssertReleaseFailedBreak(stmt)  \
    if (1) { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertReleaseBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)


/** @def AssertReleaseMsgFailed
 * An assertion failed, print a message and hit a breakpoint.
 *
 * @param   a   printf argument list (in parenthesis).
 */
#define AssertReleaseMsgFailed(a)  \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertReleaseBreakpoint(); \
    } while (0)

/** @def AssertReleaseMsgFailedReturn
 * An assertion failed, print a message, hit a breakpoint and return.
 *
 * @param   a   printf argument list (in parenthesis).
 * @param   rc      What is to be presented to return.
 */
#define AssertReleaseMsgFailedReturn(a, rc) \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertReleaseBreakpoint(); \
        return (rc); \
    } while (0)

/** @def AssertReleaseMsgFailedReturnVoid
 * An assertion failed, print a message, hit a breakpoint and return.
 *
 * @param   a   printf argument list (in parenthesis).
 */
#define AssertReleaseMsgFailedReturnVoid(a) \
    do { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertReleaseBreakpoint(); \
        return; \
    } while (0)


/** @def AssertReleaseMsgFailedBreak
 * An assertion failed, print a message, hit a breakpoint and break.
 *
 * @param   a   printf argument list (in parenthesis).
 * @param   stmt    Statement to execute before break.
 */
#define AssertReleaseMsgFailedBreak(a, stmt) \
    if (1) { \
        AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
        AssertMsg2 a; \
        AssertReleaseBreakpoint(); \
        stmt; \
        break; \
    } else do {} while (0)



/** @def AssertFatal
 * Assert that an expression is true. If it's not hit a breakpoint (for ever).
 *
 * @param   expr    Expression which should be true.
 */
#define AssertFatal(expr)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            for (;;) \
            { \
                AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
                AssertReleaseBreakpoint(); \
            } \
    } while (0)

/** @def AssertFatalMsg
 * Assert that an expression is true, print the message and hit a breakpoint (for ever) if it isn't.
 *
 * @param   expr    Expression which should be true.
 * @param   a       printf argument list (in parenthesis).
 */
#define AssertFatalMsg(expr, a)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            for (;;) \
            { \
                AssertMsg1(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
                AssertMsg2 a; \
                AssertReleaseBreakpoint(); \
            } \
    } while (0)

/** @def AssertFatalFailed
 * An assertion failed, hit a breakpoint (for ever).
 */
#define AssertFatalFailed()  \
    do { \
        for (;;) \
        { \
            AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertReleaseBreakpoint(); \
        } \
    } while (0)

/** @def AssertFatalMsgFailed
 * An assertion failed, print a message and hit a breakpoint (for ever).
 *
 * @param   a   printf argument list (in parenthesis).
 */
#define AssertFatalMsgFailed(a)  \
    do { \
        for (;;) \
        { \
            AssertMsg1((const char *)0, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            AssertMsg2 a; \
            AssertReleaseBreakpoint(); \
        } \
    } while (0)


/** @def AssertRC
 * Asserts a iprt status code successful.
 *
 * On failure it will print info about the rc and hit a breakpoint.
 *
 * @param   rc  iprt status code.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertRC(rc)            AssertMsgRC(rc, ("%Vra\n", (rc)))

/** @def AssertRCReturn
 * Asserts a iprt status code successful, bitch (RT_STRICT mode only) and return if it isn't.
 *
 * @param   rc      iprt status code.
 * @param   rcRet   What is to be presented to return.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertRCReturn(rc, rcRet)   AssertMsgRCReturn(rc, ("%Vra\n", (rc)), rcRet)

/** @def AssertRCBreak
 * Asserts a iprt status code successful, bitch (RT_STRICT mode only) and break if it isn't.
 *
 * @param   rc      iprt status code.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertRCBreak(rc, stmt)   AssertMsgRCBreak(rc, ("%Vra\n", (rc)), stmt)

/** @def AssertMsgRC
 * Asserts a iprt status code successful.
 *
 * It prints a custom message and hits a breakpoint on FAILURE.
 *
 * @param   rc      iprt status code.
 * @param   msg     printf argument list (in parenthesis).
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertMsgRC(rc, msg) \
    do { AssertMsg(RT_SUCCESS(rc), msg); NOREF(rc); } while (0)

/** @def AssertMsgRCReturn
 * Asserts a iprt status code successful and if it's not return the specified status code.
 *
 * If RT_STRICT is defined the message will be printed and a breakpoint hit before it returns
 *
 * @param   rc      iprt status code.
 * @param   msg     printf argument list (in parenthesis).
 * @param   rcRet   What is to be presented to return.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertMsgRCReturn(rc, msg, rcRet) \
    do { AssertMsgReturn(RT_SUCCESS(rc), msg, rcRet); NOREF(rc); } while (0)

/** @def AssertMsgRCBreak
 * Asserts a iprt status code successful and break if it's not.
 *
 * If RT_STRICT is defined the message will be printed and a breakpoint hit before it returns
 *
 * @param   rc      iprt status code.
 * @param   msg     printf argument list (in parenthesis).
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertMsgRCBreak(rc, msg, stmt) \
    do { AssertMsgBreak(RT_SUCCESS(rc), msg, stmt); NOREF(rc); } while (0)

/** @def AssertRCSuccess
 * Asserts an iprt status code equals VINF_SUCCESS.
 *
 * On failure it will print info about the rc and hit a breakpoint.
 *
 * @param   rc  iprt status code.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertRCSuccess(rc)                 AssertMsg((rc) == VINF_SUCCESS, ("%Vra\n", (rc)))

/** @def AssertRCSuccessReturn
 * Asserts that an iprt status code equals VINF_SUCCESS, bitch (RT_STRICT mode only) and return if it isn't.
 *
 * @param   rc      iprt status code.
 * @param   rcRet   What is to be presented to return.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertRCSuccessReturn(rc, rcRet)    AssertMsgReturn((rc) == VINF_SUCCESS, ("%Vra\n", (rc)), rcRet)

/** @def AssertRCSuccessBreak
 * Asserts that an iprt status code equals VINF_SUCCESS, bitch (RT_STRICT mode only) and break if it isn't.
 *
 * @param   rc      iprt status code.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 * @remark  rc is references multiple times. In release mode is NOREF()'ed.
 */
#define AssertRCSuccessBreak(rc, stmt)    AssertMsgBreak((rc) == VINF_SUCCESS, ("%Vra\n", (rc)), stmt)


/** @def AssertReleaseRC
 * Asserts a iprt status code successful.
 *
 * On failure information about the error will be printed and a breakpoint hit.
 *
 * @param   rc  iprt status code.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseRC(rc)             AssertReleaseMsgRC(rc, ("%Vra\n", (rc)))

/** @def AssertReleaseRCReturn
 * Asserts a iprt status code successful, returning if it isn't.
 *
 * On failure information about the error will be printed, a breakpoint hit
 * and finally returning from the function if the breakpoint is somehow ignored.
 *
 * @param   rc      iprt status code.
 * @param   rcRet   What is to be presented to return.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseRCReturn(rc, rcRet) AssertReleaseMsgRCReturn(rc, ("%Vra\n", (rc)), rcRet)

/** @def AssertReleaseRCBreak
 * Asserts a iprt status code successful, break if it isn't.
 *
 * On failure information about the error will be printed, a breakpoint hit
 * and finally the break statement will be issued if the breakpoint is somehow ignored.
 *
 * @param   rc      iprt status code.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseRCBreak(rc, stmt) AssertReleaseMsgRCBreak(rc, ("%Vra\n", (rc)), stmt)

/** @def AssertReleaseMsgRC
 * Asserts a iprt status code successful.
 *
 * On failure a custom message is printed and a breakpoint is hit.
 *
 * @param   rc      iprt status code.
 * @param   msg     printf argument list (in parenthesis).
 * @remark  rc is references multiple times.
 */
#define AssertReleaseMsgRC(rc, msg)    AssertReleaseMsg(RT_SUCCESS(rc), msg)

/** @def AssertReleaseMsgRCReturn
 * Asserts a iprt status code successful.
 *
 * On failure a custom message is printed, a breakpoint is hit, and finally
 * returning from the function if the breakpoint is showhow ignored.
 *
 * @param   rc      iprt status code.
 * @param   msg     printf argument list (in parenthesis).
 * @param   rcRet   What is to be presented to return.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseMsgRCReturn(rc, msg, rcRet)    AssertReleaseMsgReturn(RT_SUCCESS(rc), msg, rcRet)

/** @def AssertReleaseMsgRCBreak
 * Asserts a iprt status code successful.
 *
 * On failure a custom message is printed, a breakpoint is hit, and finally
 * the brean statement is issued if the breakpoint is showhow ignored.
 *
 * @param   rc      iprt status code.
 * @param   msg     printf argument list (in parenthesis).
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseMsgRCBreak(rc, msg, stmt)    AssertReleaseMsgBreak(RT_SUCCESS(rc), msg, stmt)

/** @def AssertReleaseRCSuccess
 * Asserts that an iprt status code equals VINF_SUCCESS.
 *
 * On failure information about the error will be printed and a breakpoint hit.
 *
 * @param   rc  iprt status code.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseRCSuccess(rc)                  AssertReleaseMsg((rc) == VINF_SUCCESS, ("%Vra\n", (rc)))

/** @def AssertReleaseRCSuccessReturn
 * Asserts that an iprt status code equals VINF_SUCCESS.
 *
 * On failure information about the error will be printed, a breakpoint hit
 * and finally returning from the function if the breakpoint is somehow ignored.
 *
 * @param   rc      iprt status code.
 * @param   rcRet   What is to be presented to return.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseRCSuccessReturn(rc, rcRet)     AssertReleaseMsgReturn((rc) == VINF_SUCCESS, ("%Vra\n", (rc)), rcRet)

/** @def AssertReleaseRCSuccessBreak
 * Asserts that an iprt status code equals VINF_SUCCESS.
 *
 * On failure information about the error will be printed, a breakpoint hit
 * and finally the break statement will be issued if the breakpoint is somehow ignored.
 *
 * @param   rc      iprt status code.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 * @remark  rc is references multiple times.
 */
#define AssertReleaseRCSuccessBreak(rc, stmt)     AssertReleaseMsgBreak((rc) == VINF_SUCCESS, ("%Vra\n", (rc)), stmt)


/** @def AssertFatalRC
 * Asserts a iprt status code successful.
 *
 * On failure information about the error will be printed and a breakpoint hit.
 *
 * @param   rc  iprt status code.
 * @remark  rc is references multiple times.
 */
#define AssertFatalRC(rc)           AssertFatalMsgRC(rc, ("%Vra\n", (rc)))

/** @def AssertReleaseMsgRC
 * Asserts a iprt status code successful.
 *
 * On failure a custom message is printed and a breakpoint is hit.
 *
 * @param   rc      iprt status code.
 * @param   msg     printf argument list (in parenthesis).
 * @remark  rc is references multiple times.
 */
#define AssertFatalMsgRC(rc, msg)   AssertFatalMsg(RT_SUCCESS(rc), msg)

/** @def AssertFatalRCSuccess
 * Asserts that an iprt status code equals VINF_SUCCESS.
 *
 * On failure information about the error will be printed and a breakpoint hit.
 *
 * @param   rc  iprt status code.
 * @remark  rc is references multiple times.
 */
#define AssertFatalRCSuccess(rc)    AssertFatalMsg((rc) == VINF_SUCCESS, ("%Vra\n", (rc)))


/** @def AssertPtr
 * Asserts that a pointer is valid.
 *
 * @param   pv      The pointer.
 */
#define AssertPtr(pv)               AssertMsg(VALID_PTR(pv), ("%p\n", (pv)))

/** @def AssertPtrReturn
 * Asserts that a pointer is valid.
 *
 * @param   pv      The pointer.
 * @param   rcRet   What is to be presented to return.
 */
#define AssertPtrReturn(pv, rcRet)  AssertMsgReturn(VALID_PTR(pv), ("%p\n", (pv)), rcRet)

/** @def AssertPtrBreak
 * Asserts that a pointer is valid.
 *
 * @param   pv      The pointer.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 */
#define AssertPtrBreak(pv, stmt)  AssertMsgBreak(VALID_PTR(pv), ("%p\n", (pv)), stmt)

/** @def AssertPtrNull
 * Asserts that a pointer is valid or NULL.
 *
 * @param   pv      The pointer.
 */
#define AssertPtrNull(pv)               AssertMsg(VALID_PTR(pv) || (pv) == NULL, ("%p\n", (pv)))

/** @def AssertPtrNullReturn
 * Asserts that a pointer is valid or NULL.
 *
 * @param   pv      The pointer.
 * @param   rcRet   What is to be presented to return.
 */
#define AssertPtrNullReturn(pv, rcRet)  AssertMsgReturn(VALID_PTR(pv) || (pv) == NULL, ("%p\n", (pv)), rcRet)

/** @def AssertPtrNullBreak
 * Asserts that a pointer is valid or NULL.
 *
 * @param   pv      The pointer.
 * @param   stmt    Statement to execute before break in case of a failed assertion.
 */
#define AssertPtrNullBreak(pv, stmt)  AssertMsgBreak(VALID_PTR(pv) || (pv) == NULL, ("%p\n", (pv)), stmt)


__BEGIN_DECLS

/**
 * The 1st part of an assert message.
 *
 * @param   pszExpr     Expression. Can be NULL.
 * @param   uLine       Location line number.
 * @param   pszFile     Location file name.
 * @param   pszFunction Location function name.
 * @remark  This API exists in HC Ring-3 and GC.
 */
RTDECL(void)    AssertMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction);

/**
 * The 2nd (optional) part of an assert message.
 * @param   pszFormat   Printf like format string.
 * @param   ...         Arguments to that string.
 * @remark  This API exists in HC Ring-3 and GC.
 */
RTDECL(void)    AssertMsg2(const char *pszFormat, ...);

/**
 * Overridable function that decides whether assertions executes the breakpoint or not. 
 * 
 * The generic implementation will return true.
 * 
 * @returns true if the breakpoint should be hit, false if it should be ignored.
 * @remark  The RTDECL() makes this a bit difficult to override on windows. Sorry.
 */
RTDECL(bool)    RTAssertDoBreakpoint(void);


/** The last assert message, 1st part. */
extern RTDATADECL(char) g_szRTAssertMsg1[1024];
/** The last assert message, 2nd part. */
extern RTDATADECL(char) g_szRTAssertMsg2[2048];

__END_DECLS

/** @} */

#endif

