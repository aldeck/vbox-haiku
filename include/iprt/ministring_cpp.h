/** @file
 * VirtualBox mini C++ string class. This is a base for both Utf8Str and
 * other places where IPRT may want to use a lean C++ string class.
 */

/*
 * Copyright (C) 2007-2009 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___VBox_ministring_h
#define ___VBox_ministring_h

#include <iprt/mem.h>
#include <iprt/string.h>

#include <new>

namespace iprt
{

/**
 *  "MiniString" is a small C++ string class that does not depend on anything
 *  else except IPRT memory management functions. This is used as the base of
 *  both the Utf8Str class that COM uses as well as C++ code in IPRT that
 *  prefers to have a string class, like in xml.cpp.
 *
 *  Semantics are like in std::string, except it can do a lot less.
 *
 *  Much of the code in here used to be in com::Utf8Str so that com::Utf8Str
 *  can now derive from MiniString and only contain code that is COM-specific,
 *  such as com::Bstr conversions. Compared to the old Utf8Str though, MiniString
 *  always knows the length of its member string and the size of the buffer
 *  so it can use memcpy() instead of strdup().
 */

class RT_DECL_CLASS MiniString
{
public:
    /**
     * Creates an empty string that has no memory allocated.
     */
    MiniString()
        : m_psz(NULL),
          m_cbLength(0),
          m_cbAllocated(0)
    {
    }

    /**
     * Creates a copy of another MiniString. This allocates
     * s.length() + 1 bytes for the new instance.
     * @param s
     */
    MiniString(const MiniString &s)
    {
        copyFrom(s);
    }

    /**
     * Creates a copy of another MiniString. This allocates
     * strlen(pcsz) + 1 bytes for the new instance.
     * @param pcsz
     */
    MiniString(const char *pcsz)
    {
        copyFrom(pcsz);
    }

    /**
     * Destructor.
     */
    virtual ~MiniString()
    {
        cleanup();
    }

    /**
     * Returns the length of the member string. This is always cached
     * so calling this is cheap and requires no strlen() invocation.
     * @return
     */
    size_t length() const
    {
        return m_cbLength;
    }

    /**
     * Returns the number of bytes allocated in the internal string buffer,
     * which is at least length() + 1 if length() > 0.
     * @return
     */
    size_t capacity() const
    {
        return m_cbAllocated;
    }

    /**
     * Requests that the contained memory buffer have at least cb bytes allocated.
     * This may expand or shrink the string's storage, but will never truncate the
     * contained string. In other words, cb will be ignored if it's smaller than
     * length() + 1.
     * @param cb new minimum size of member memory buffer
     */
    void reserve(size_t cb)
    {
        if (    (cb != m_cbAllocated)
             && (cb > m_cbLength + 1)
           )
        {
            m_psz = (char*)RTMemRealloc(m_psz, cb);
#ifdef RT_EXCEPTIONS_ENABLED
            if (!m_psz)
                throw std::bad_alloc();
#endif
            m_cbAllocated = cb;
        }
    }

    /**
     * Deallocates all memory.
     */
    inline void setNull()
    {
        cleanup();
    }

    /**
     *  Returns a non-const raw pointer that allows to modify the string directly.
     *  @warning
     *      1)  Be sure not to modify data beyond the allocated memory! Call
     *          capacity() to find out how large that buffer is.
     *      2)  After any operation that modifies the length of the string,
     *          you _must_ call MiniString::jolt(), or subsequent copy operations
     *          may go nowhere. Better not use mutableRaw() at all.
     */
    char* mutableRaw()
    {
        return m_psz;
    }

    /**
     * Intended to be called after something has messed with the internal string
     * buffer (e.g. after using mutableRaw() or Utf8Str::asOutParam()). Resets
     * the internal lengths correctly. Otherwise subsequent copy operations may
     * go nowhere.
     */
    void jolt()
    {
        if (m_psz)
        {
            m_cbLength = strlen(m_psz);
            m_cbAllocated = m_cbLength + 1;
        }
        else
        {
            m_cbLength = 0;
            m_cbAllocated = 0;
        }
    }

    /**
     * Assigns a copy of pcsz to "this".
     * @param pcsz
     * @return
     */
    MiniString& operator=(const char *pcsz)
    {
        if (m_psz != pcsz)
        {
            cleanup();
            copyFrom(pcsz);
        }
        return *this;
    }

    /**
     * Assigns a copy of s to "this".
     * @param s
     * @return
     */
    MiniString& operator=(const MiniString &s)
    {
        if (this != &s)
        {
            cleanup();
            copyFrom(s);
        }
        return *this;
    }

    /**
     * Appends a copy of @a that to "this".
     * @param that
     */
    void append(const MiniString &that)
    {
        size_t cbThis = length();
        size_t cbThat = that.length();

        if (cbThat)
        {
            size_t cbBoth = cbThis + cbThat + 1;

            reserve(cbBoth);
                // calls realloc(cbBoth) and sets m_cbAllocated

            memcpy(m_psz + cbThis, that.m_psz, cbThat);
            m_psz[cbThis + cbThat] = '\0';
            m_cbLength = cbBoth - 1;
        }
    }

    /**
     * Returns the contained string as a C-style const char* pointer.
     * @return
     */
    inline const char* c_str() const
    {
        return m_psz;
    }

    /**
     * Like c_str(), for compatibility with lots of VirtualBox Main code.
     * @return
     */
    inline const char* raw() const
    {
        return m_psz;
    }

    /** Intended to to pass instances as input (|char *|) parameters to methods. */
    inline operator const char*() const
    {
        return c_str();
    }

    /**
     * Returns true if the member string has no length. This states nothing about
     * how much memory might be allocated.
     * @return
     */
    bool isEmpty() const
    {
        return length() == 0;
    }

    enum CaseSensitivity
    {
        CaseSensitive,
        CaseInsensitive
    };

    /**
     * Compares the member string to pcsz.
     * @param pcsz
     * @param cs Whether comparison should be case-sensitive.
     * @return
     */
    int compare(const char *pcsz, CaseSensitivity cs = CaseSensitive) const
    {
        if (m_psz == pcsz)
            return 0;
        if (m_psz == NULL)
            return -1;
        if (pcsz == NULL)
            return 1;

        if (cs == CaseSensitive)
            return ::RTStrCmp(m_psz, pcsz);
        else
            return ::RTStrICmp(m_psz, pcsz);
    }

    int compare(const MiniString &that, CaseSensitivity cs = CaseSensitive) const
    {
        return compare(that.m_psz, cs);
    }

    bool operator==(const MiniString &that) const { return !compare(that); }
    bool operator!=(const MiniString &that) const { return !!compare(that); }
    bool operator<(const MiniString &that) const { return compare(that) < 0; }
    bool operator>(const MiniString &that) const { return compare(that) > 0; }

    bool operator==(const char *that) const { return !compare(that); }
    bool operator!=(const char *that) const { return !!compare(that); }
    bool operator<(const char *that) const { return compare(that) < 0; }
    bool operator>(const char *that) const { return compare(that) > 0; }

    static const size_t npos;

    /**
     * Looks for pcszFind in "this" starting at "pos" and returns its position,
     * counting from the beginning of "this" at 0. Returns npos if not found.
     */
    size_t find(const char *pcszFind, size_t pos = 0) const;

    /**
     * Returns a substring of "this" as a new Utf8Str. Works exactly like
     * its equivalent in std::string except that this interprets pos and n
     * as UTF-8 codepoints instead of bytes. With the default parameters "0"
     * and "npos", this always copies the entire string.
     * @param pos Index of first codepoint to copy from "this", counting from 0.
     * @param n Number of codepoints to copy, starting with the one at "pos".
     */
    iprt::MiniString substr(size_t pos = 0, size_t n = npos) const;

    /**
     * Returns true if "this" ends with "that".
     * @param that
     * @param cs
     * @return
     */
    bool endsWith(const iprt::MiniString &that, CaseSensitivity cs = CaseSensitive) const;

    /**
     * Returns true if "this" begins with "that".
     * @return
     */
    bool startsWith(const iprt::MiniString &that, CaseSensitivity cs = CaseSensitive) const;

    /**
     * Returns true if "this" contains "that" (strstr).
     * @param that
     * @param cs
     * @return
     */
    bool contains(const iprt::MiniString &that, CaseSensitivity cs = CaseSensitive) const;

    /**
     * Attempts to convert the member string into an 64-bit integer.
     *
     * @returns 64-bit unsigned number on success.
     * @returns 0 on failure.
     */
    int64_t toInt64() const
    {
        return RTStrToInt64(m_psz);
    }

    /**
     * Attempts to convert the member string into an unsigned 64-bit integer.
     *
     * @returns 64-bit unsigned number on success.
     * @returns 0 on failure.
     */
    uint64_t toUInt64() const
    {
        return RTStrToUInt64(m_psz);
    }

    /**
     * Attempts to convert the member string into an unsigned 64-bit integer.
     * @return IPRT error code.
     * @param i Output buffer.
     */
    int toInt(uint64_t &i) const;

    /**
     * Attempts to convert the member string into an unsigned 32-bit integer.
     * @return IPRT error code.
     * @param i Output buffer.
     */
    int toInt(uint32_t &i) const;

protected:

    /**
     * Hide operator bool() to force people to use isEmpty() explicitly.
     */
    operator bool() const { return false; }

    /**
     *  Destructor implementation, also used to clean up in operator=()
     *  before assigning a new string.
     */
    void cleanup()
    {
        if (m_psz)
        {
            RTMemFree(m_psz);
            m_psz = NULL;
            m_cbLength = 0;
            m_cbAllocated = 0;
        }
    }

    /**
     * Protected internal helper.
     * copyFrom() unconditionally sets the members to a copy of the
     * given other strings and makes no assumptions about previous
     * contents. Can therefore be used both in copy constructors,
     * when member variables have no defined value, and in assignments
     * after having called cleanup().
     *
     * This variant copies from another MiniString and is fast since
     * the length of source string is known.
     *
     * @param s
     */
    void copyFrom(const MiniString &s)
    {
        if ((m_cbLength = s.m_cbLength))
        {
            m_cbAllocated = m_cbLength + 1;
            m_psz = (char*)RTMemAlloc(m_cbAllocated);
#ifdef RT_EXCEPTIONS_ENABLED
            if (!m_psz)
                throw std::bad_alloc();
#endif
            memcpy(m_psz, s.m_psz, m_cbAllocated);      // include 0 terminator
        }
        else
        {
            m_cbAllocated = 0;
            m_psz = NULL;
        }
    }

    /**
     * Protected internal helper.
     * See copyFrom() above.
     *
     * This variant copies from a C string and needs to call strlen()
     * on it. It's therefore slower than the one above.
     *
     * @param pcsz
     */
    void copyFrom(const char *pcsz)
    {
        if (pcsz)
        {
            m_cbLength = strlen(pcsz);
            m_cbAllocated = m_cbLength + 1;
            m_psz = (char*)RTMemAlloc(m_cbAllocated);
#ifdef RT_EXCEPTIONS_ENABLED
            if (!m_psz)
                throw std::bad_alloc();
#endif
            memcpy(m_psz, pcsz, m_cbAllocated);      // include 0 terminator
        }
        else
        {
            m_cbLength = 0;
            m_cbAllocated = 0;
            m_psz = NULL;
        }
    }

    char    *m_psz;
    size_t  m_cbLength;                     // strlen(m_psz)
    size_t  m_cbAllocated;                  // size of buffer that m_psz points to; at least m_cbLength + 1
};

} // namespace iprt

#endif
