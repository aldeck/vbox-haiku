/** @file
 *
 * MS COM / XPCOM Abstraction Layer:
 * Smart string classes declaration
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

#ifndef __VBox_com_string_h__
#define __VBox_com_string_h__

#if !defined(__WIN__)
#include <nsMemory.h>
#endif

#include "VBox/com/defs.h"
#include "VBox/com/assert.h"

#include <iprt/string.h>
#include <iprt/alloc.h>

namespace com
{

class Utf8Str;

/**
 *  Helper class that represents the |BSTR| type and hides platform-siecific
 *  implementation details.
 *
 *  @note
 *  This class follows the common ownership transfer rule, Regarding to passing
 *  strings as method parameters, this means that the instance data is always
 *  owned by the caller.
 */
class Bstr
{
public:

    typedef BSTR String;
    typedef const BSTR ConstString;

    Bstr () : bstr (NULL) {}

    Bstr (const Bstr &that) : bstr (NULL) { raw_copy (bstr, that.bstr); }
    Bstr (const BSTR that) : bstr (NULL) { raw_copy (bstr, that); }
    Bstr (const wchar_t *that) : bstr (NULL)
    {
        AssertCompile (sizeof (wchar_t) == sizeof (OLECHAR));
        raw_copy (bstr, (const BSTR) that);
    }

    Bstr (const Utf8Str &that);
    Bstr (const char *that);

    /** Shortcut that calls #alloc(aSize) right after object creation. */
    Bstr (size_t aSize) : bstr (NULL) { alloc (aSize); }

    ~Bstr () { setNull(); }

    Bstr &operator = (const Bstr &that) { safe_assign (that.bstr); return *this; }
    Bstr &operator = (const BSTR that) { safe_assign (that); return *this; }

    Bstr &operator = (const Utf8Str &that);
    Bstr &operator = (const char *that);

    Bstr &setNull()
    {
        if (bstr)
        {
            ::SysFreeString (bstr);
            bstr = NULL;
        }
        return *this;
    }

    Bstr &setNullIfEmpty()
    {
        if (bstr && *bstr == 0)
        {
            ::SysFreeString (bstr);
            bstr = NULL;
        }
        return *this;
    }

    /**
     *  Allocates memory for a string capable to store \a aSize - 1 characters
     *  plus the terminating zero character. If \a aSize is zero, or if a
     *  memory allocation error occurs, this object will become null.
     */
    Bstr &alloc (size_t aSize)
    {
        setNull();
        if (aSize)
        {
            AssertCompile (sizeof (unsigned int) >= sizeof (aSize));
            unsigned int size = (unsigned int) aSize;
            bstr = ::SysAllocStringLen (NULL, size - 1);
            if (bstr)
                bstr [0] = 0;
        }
        return *this;
    }

    int compare (const BSTR str) const
    {
        return ::RTStrUcs2Cmp ((PRTUCS2) bstr, (PRTUCS2) str);
    }

    bool operator == (const Bstr &that) const { return !compare (that.bstr); }
    bool operator != (const Bstr &that) const { return !!compare (that.bstr); }
    bool operator == (const BSTR that) const { return !compare (that); }
    bool operator != (const wchar_t *that) const
    {
        AssertCompile (sizeof (wchar_t) == sizeof (OLECHAR));
        return !!compare ((const BSTR) that);
    }
    bool operator == (const wchar_t *that) const
    {
        AssertCompile (sizeof (wchar_t) == sizeof (OLECHAR));
        return !compare ((const BSTR) that);
    }
    bool operator != (const BSTR that) const { return !!compare (that); }
    bool operator < (const Bstr &that) const { return compare (that.bstr) < 0; }
    bool operator < (const BSTR that) const { return compare (that) < 0; }
    bool operator < (const wchar_t *that) const
    {
        AssertCompile (sizeof (wchar_t) == sizeof (OLECHAR));
        return compare ((const BSTR) that) < 0;
    }

    int compareIgnoreCase (const BSTR str) const
    {
        return ::RTUtf16LocaleICmp (bstr, str);
    }

    bool isNull() const { return bstr == NULL; }
    operator bool() const { return !isNull(); }

    bool isEmpty() const { return isNull() || *bstr == 0; }

    size_t length() const { return isNull() ? 0 : ::RTStrUcs2Len ((PRTUCS2) bstr); }

    /** Intended to to pass instances as |BSTR| input parameters to methods. */
    operator const BSTR () const { return bstr; }

    /** The same as operator const BSTR(), but for situations where the compiler
        cannot typecast implicitly (for example, in printf() argument list). */
    const BSTR raw() const { return bstr; }

    /**
     *  Returns a non-const raw pointer that allows to modify the string directly.
     *  @warning
     *      Be sure not to modify data beyond the allocated memory! The
     *      guaranteed size of the allocated memory is at least #length()
     *      bytes after creation and after every assignment operation.
     */
    BSTR mutableRaw() { return bstr; }

    /**
     *  Intended to assign instances to |BSTR| out parameters from within the
     *  interface method. Transfers the ownership of the duplicated string to
     *  the caller.
     */
    const Bstr &cloneTo (BSTR *pstr) const
    {
        if (pstr)
        {
            *pstr = NULL;
            raw_copy (*pstr, bstr);
        }
        return *this;
    }

    /**
     *  Intended to assign instances to |char *| out parameters from within the
     *  interface method. Transfers the ownership of the duplicated string to
     *  the caller.
     */
    const Bstr &cloneTo (char **pstr) const;

    /**
     *  Intended to pass instances as |BSTR| out parameters to methods.
     *  Takes the ownership of the returned data.
     */
    BSTR *asOutParam() { setNull(); return &bstr; }

private:

    void safe_assign (const BSTR str)
    {
        if (bstr != str)
        {
            setNull();
            raw_copy (bstr, str);
        }
    }

    inline static void raw_copy (BSTR &ls, const BSTR rs)
    {
        if (rs)
            ls = ::SysAllocString ((const OLECHAR *) rs);
    }

    inline static void raw_copy (BSTR &ls, const char *rs)
    {
        if (rs)
        {
            PRTUCS2 s = NULL;
            ::RTStrUtf8ToUcs2 (&s, rs);
            raw_copy (ls, (BSTR) s);
            ::RTStrUcs2Free (s);
        }
    }

    BSTR bstr;

    friend class Utf8Str; // to access our raw_copy()
};

// symmetric compare operators
inline bool operator== (const BSTR l, const Bstr &r) { return r.operator== (l); }
inline bool operator!= (const BSTR l, const Bstr &r) { return r.operator!= (l); }

////////////////////////////////////////////////////////////////////////////////

/**
 *  Helper class that represents UTF8 (|char *|) strings. Useful in
 *  conjunction with Bstr to simplify conversions beetween UCS2 (|BSTR|)
 *  and UTF8.
 */
class Utf8Str
{
public:

    typedef char *String;
    typedef const char *ConstString;

    Utf8Str () : str (NULL) {}

    Utf8Str (const Utf8Str &that) : str (NULL) { raw_copy (str, that.str); }
    Utf8Str (const char *that) : str (NULL) { raw_copy (str, that); }

    Utf8Str (const Bstr &that) : str (NULL) { raw_copy (str, that); }
    Utf8Str (const BSTR that) : str (NULL) { raw_copy (str, that); }

    /** Shortcut that calls #alloc(aSize) right after object creation. */
    Utf8Str (size_t aSize) : str (NULL) { alloc(aSize); }

    virtual ~Utf8Str () { setNull(); }

    Utf8Str &operator = (const Utf8Str &that) { safe_assign (that.str); return *this; }
    Utf8Str &operator = (const char *that) { safe_assign (that); return *this; }

    Utf8Str &operator = (const Bstr &that)
    {
        setNull();
        raw_copy (str, that);
        return *this;
    }
    Utf8Str &operator = (const BSTR that)
    {
        setNull();
        raw_copy (str, that);
        return *this;
    }

    Utf8Str &setNull()
    {
        if (str)
        {
#if defined (__WIN__)
            ::RTStrFree (str);
#else
            nsMemory::Free (str);
#endif
            str = NULL;
        }
        return *this;
    }

    Utf8Str &setNullIfEmpty()
    {
        if (str && *str == 0)
        {
#if defined (__WIN__)
            ::RTStrFree (str);
#else
            nsMemory::Free (str);
#endif
            str = NULL;
        }
        return *this;
    }

    /**
     *  Allocates memory for a string capable to store \a aSize - 1 characters
     *  plus the terminating zero character. If \a aSize is zero, or if a
     *  memory allocation error occurs, this object will become null.
     */
    Utf8Str &alloc (size_t aSize)
    {
        setNull();
        if (aSize)
        {
#if defined (__WIN__)
            str = (char *) ::RTMemTmpAlloc (aSize);
#else
            str = (char *) nsMemory::Alloc (aSize);
#endif
            if (str)
                str [0] = 0;
        }
        return *this;
    }

    int compare (const char *s) const
    {
        return str == s ? 0 : ::strcmp (str, s);
    }

    bool operator == (const Utf8Str &that) const { return !compare (that.str); }
    bool operator != (const Utf8Str &that) const { return !!compare (that.str); }
    bool operator == (const char *that) const { return !compare (that); }
    bool operator != (const char *that) const { return !!compare (that); }
    bool operator < (const Utf8Str &that) const { return compare (that.str) < 0; }
    bool operator < (const char *that) const { return compare (that) < 0; }

    bool isNull() const { return str == NULL; }
    operator bool() const { return !isNull(); }

    bool isEmpty() const { return isNull() || *str == 0; }

    size_t length() const { return isNull() ? 0 : ::strlen (str); }

    /** Intended to to pass instances as input (|char *|) parameters to methods. */
    operator const char *() const { return str; }

    /** The same as operator const char *(), but for situations where the compiler
        cannot typecast implicitly (for example, in printf() argument list). */
    const char *raw() const { return str; }

    /**
     *  Returns a non-const raw pointer that allows to modify the string directly.
     *  @warning
     *      Be sure not to modify data beyond the allocated memory! The
     *      guaranteed size of the allocated memory is at least #length()
     *      bytes after creation and after every assignment operation.
     */
    char *mutableRaw() { return str; }

    /**
     *  Intended to assign instances to |char *| out parameters from within the
     *  interface method. Transfers the ownership of the duplicated string to the
     *  caller.
     */
    const Utf8Str &cloneTo (char **pstr) const
    {
        if (pstr)
        {
            *pstr = NULL;
            raw_copy (*pstr, str);
        }
        return *this;
    }

    /**
     *  Intended to assign instances to |BSTR| out parameters from within the
     *  interface method. Transfers the ownership of the duplicated string to the
     *  caller.
     */
    const Utf8Str &cloneTo (BSTR *pstr) const
    {
        if (pstr)
        {
            *pstr = NULL;
            Bstr::raw_copy (*pstr, str);
        }
        return *this;
    }

    /**
     *  Intended to pass instances as out (|char **|) parameters to methods.
     *  Takes the ownership of the returned data.
     */
    char **asOutParam() { setNull(); return &str; }

private:

    void safe_assign (const char *s)
    {
        if (str != s)
        {
            setNull();
            raw_copy (str, s);
        }
    }

    inline static void raw_copy (char *&ls, const char *rs)
    {
        if (rs)
#if defined (__WIN__)
            ::RTStrDupEx (&ls, rs);
#else
            ls = (char *) nsMemory::Clone (rs, strlen (rs) + 1);
#endif
    }

    inline static void raw_copy (char *&ls, const BSTR rs)
    {
        if (rs)
        {
#if defined (__WIN__)
            ::RTStrUcs2ToUtf8 (&ls, (PRTUCS2) rs);
#else
            char *s = NULL;
            ::RTStrUcs2ToUtf8 (&s, (PRTUCS2) rs);
            raw_copy (ls, s);
            ::RTStrFree (s);
#endif
        }
    }

    char *str;

    friend class Bstr; // to access our raw_copy()
};

// symmetric compare operators
inline bool operator== (const char *l, const Utf8Str &r) { return r.operator== (l); }
inline bool operator!= (const char *l, const Utf8Str &r) { return r.operator!= (l); }

// work around error C2593 of the stupid MSVC 7.x ambiguity resolver
WORKAROUND_MSVC7_ERROR_C2593_FOR_BOOL_OP (Bstr)
WORKAROUND_MSVC7_ERROR_C2593_FOR_BOOL_OP (Utf8Str)

////////////////////////////////////////////////////////////////////////////////

// inlined Bstr members that depend on Utf8Str

inline Bstr::Bstr (const Utf8Str &that) : bstr (NULL) { raw_copy (bstr, that); }
inline Bstr::Bstr (const char *that) : bstr (NULL) { raw_copy (bstr, that); }

inline Bstr &Bstr::operator = (const Utf8Str &that)
{
    setNull();
    raw_copy (bstr, that);
    return *this;
}
inline Bstr &Bstr::operator = (const char *that)
{
    setNull();
    raw_copy (bstr, that);
    return *this;
}

inline const Bstr &Bstr::cloneTo (char **pstr) const
{
    if (pstr) {
        *pstr = NULL;
        Utf8Str::raw_copy (*pstr, bstr);
    }
    return *this;
}

////////////////////////////////////////////////////////////////////////////////

/**
 *  This class is a printf-like formatter for Utf8Str strings. Its purpose is
 *  to construct Utf8Str objects from a format string and a list of arguments
 *  for the format string.
 *
 *  The usage of this class is like the following:
 *  <code>
 *      Utf8StrFmt string ("program name = %s", argv[0]);
 *  </code>
 */
class Utf8StrFmt : public Utf8Str
{
public:

    /**
     *  Constructs a new string given the format string and the list
     *  of the arguments for the format string.
     *
     *  @param format   printf-like format string (in UTF-8 encoding)
     *  @param ...      list of the arguments for the format string
     *
     *  @note  Be extremely careful when passing exactly one argument in the
     *         ellipsis. If this is a string the C++ could decide to use the
     *         other constructor since va_list is defined as char * on some
     *         platforms. If unsure, add an extra dummy argument.
     */
    explicit Utf8StrFmt (const char *format, ...)
    {
        va_list args;
        va_start (args, format);
        init (format, args);
        va_end (args);
    }

    /**
     *  Constructs a new string given the format string and the list
     *  of the arguments for the format string.
     *
     *  @param format   printf-like format string (in UTF-8 encoding)
     *  @param args     list of arguments for the format string
     */
    Utf8StrFmt (const char *format, va_list args) { init (format, args); }

private:

    void init (const char *format, va_list args);

    static DECLCALLBACK(size_t) strOutput (void *pvArg, const char *pachChars,
                                           size_t cbChars);
};

} // namespace com

#endif // __VBox_com_string_h__

