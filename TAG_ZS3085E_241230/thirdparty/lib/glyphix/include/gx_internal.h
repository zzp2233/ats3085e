/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_assert.h"
#include "gx_version.h"

#define GX_UNUSED(var) ((void)(var))

#if defined(__CC_ARM) && !defined(GX_COPMATIBLE_CXX_98)
#define GX_COPMATIBLE_CXX_98
#endif

#ifdef GX_COPMATIBLE_CXX_98
#include <stdint.h>
#else
#include <cstdint>
#endif

#ifdef GX_COPMATIBLE_CXX_98
class nullptr_t {
public:
    template<class T> inline operator T *() const { return 0; }
    template<class C, class T> inline operator T C::*() const { return 0; }

private:
    void operator&() const;
};

#define nullptr nullptr_t()
#define GX_OVERRIDE
#define GX_DEFAULT                                                                                 \
    {}
#define GX_DELETE
#define GX_NOEXCEPT
#define GX_STATIC_ASSERT
#else
#define GX_OVERRIDE
#define GX_DEFAULT            = default
#define GX_DELETE             = delete
#define GX_NOEXCEPT           noexcept
#define GX_STATIC_ASSERT(...) static_assert(__VA_ARGS__)
#endif

#if __cplusplus >= 201703L
#define GX_NODISCARD [[nodiscard]]
#else
#define GX_NODISCARD
#endif

// remove some macro definitions
#undef min
#undef max

namespace gx {
class Logger;

//! The BiDi layout direction. Control the layout direction of UI elements (e.g. text, images).
enum LayoutDirection {
    LeftToRight = 0, //!< Left-to-right layout mode (default), which is the most typical layout.
    RightToLeft = 1, //!< Right to left layout, currently unused.
    LayoutAuto = 2   //!< The layout mode inherited from the parent widget.
};

enum AlignmentFlag {
    AlignLeft = 0x0001,
    AlignRight = 0x0002,
    AlignHCenter = 0x0004,
    AlignJustify = 0x0008,
    AlignTop = 0x0010,
    AlignBottom = 0x0020,
    AlignVCenter = 0x0040,
    AlignBaseline = 0x0080,
    AlignCenter = AlignHCenter | AlignVCenter,
    AlignHorizontalMask = 0x000f,
    AlignVerticalMask = 0x00f0,
    AlignmentMask = 0x00ff
};

enum TextOverflowMode { TextClip = 0x0000, TextEllipsis = 0x0100, TextOverflowMask = 0x0f00 };

enum ObjectFitMode {
    ObjectFitNone = 0x0000,
    ObjectFitContain = 0x1000,
    ObjectFitFill = 0x2000,
    ObjectFitCover = 0x3000,
    ObjectFitScaleDown = 0x4000,
    ObjectFitMask = 0xf000,
};

enum Direction { NoDirection = 0, Horizontal = 1, Vertical = 2, AllDirections = 3 };

//! Manipulate the pixel format.
class PixelFormat {
public:
    /**
     * Enumerates the pixel formats built into Glyphix. Glyphix only uses the int type to
     * represent the pixel format without restricting it in Format, so you can extend the
     * pixel format as needed for specific hardware.
     * @see Pixmap
     */
    enum Format {
        RGB565,        //!< RGB format, 16bpp
        RGB888,        //!< RGB format, 24bpp
        XRGB8888,      //!< RGB format, 32bpp, X is a useless channel
        ARGB8888,      //!< ARGB format, 32bpp, support an alpha channel
        ARGB4444,      //!< ARGB format, 16bpp, support an alpha channel
        A8,            //!< Alpha format, 8bpp
        A4,            //!< Alpha format, 4bpp
        A2,            //!< Alpha format, 2bpp
        Mono,          //!< Binary color type, not yet supported
        UnknownFormat, //!< Unknown pixel format, which implies an illegal value (do not use).
        //! User-defined pixel formats starting from this enumeration value. Glyphix does not
        //! recognise these formats, therefore, you need implement your own paint engine. The user
        //! defined pixel format is not cross platform, this means that the same enumeration value
        //! may have different meanings on different platforms and cannot be used universally.
        UserFormat
    };
    //! Get the bit depth of the specified pixel format. This function does not recognise custom
    //! pixel formats.
    //! \param pixfmt A pixel format value. It must be a value in the \ref Format enumeration that
    //! is smaller than \ref UnknownFormat.
    //! \return The bit depth of the pixel format by \p pixfmt.
    static int depth(int pixfmt);
    //! Gets the number of bytes occupied by a row of pixels in the specified pixel format. This
    //! function does not recognise custom pixel formats.
    //! \param pixfmt A pixel format value. It must be a value in the \ref Format enumeration that
    //! is smaller than \ref UnknownFormat.
    //! \param length Number of pixels.
    //! \return The number of bytes occupied by \p length pixels by \p pixfmt format.
    static int pitch(int pixfmt, int length);
    //! Gets the name string for the specified pixel format.
    //! \param pixfmt A pixel format value. It must be a value in the \ref Format enumeration that
    //! is smaller than \ref UnknownFormat.
    //! \return The name string of pixel format specified by \p pixfmt.
    static const char *name(int pixfmt);
};

template<typename T> inline const T &min(const T &x, const T &y) { return x < y ? x : y; }

template<typename T> inline const T &max(const T &x, const T &y) { return x > y ? x : y; }

template<typename T> inline T abs(const T &x) { return x < 0 ? -x : x; }

template<typename T> inline const T &bound(const T &lower, const T &val, const T &upper) {
    return max(lower, min(val, upper));
}

inline bool fuzzyZero(float a, float epsilon) { return a > -epsilon && a < epsilon; }

inline bool fuzzyZero(float a) { return fuzzyZero(a, 1e-5f); }

inline bool fuzzyEqual(float a, float b) { return (abs(a - b) * 100000.f <= min(abs(a), abs(b))); }

template<typename Out, typename In>
inline Out function_pointer_cast(In in) { // NOLINT(readability-*)
    // Suppress warnings with the -Wcast-function-type option.
    return *reinterpret_cast<Out *>(&in);
}

/* Borrowed from Boost::noncopyable. Add the template to give each noncopyable
 * a different signature. */
template<typename T> class NonCopyable {
protected:
    NonCopyable() GX_DEFAULT;
    ~NonCopyable() GX_DEFAULT;

#ifndef GX_COPMATIBLE_CXX_98
public:
    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;
#else
private: // emphasize the following members are private
    NonCopyable(const noncopyable &);
    NonCopyable &operator=(const noncopyable &);
#endif
};

#if !defined(DOXYGEN_DOCUMENTATION_BUILD)
// NOLINTBEGIN(bugprone-macro-parentheses)
#define GX_FLAGS(Enum)                                                                             \
    friend inline Enum operator|(Enum a, Enum b) { return Enum(int(a) | int(b)); }                 \
    friend inline Enum operator&(Enum a, Enum b) { return Enum(int(a) & int(b)); }                 \
    friend inline Enum operator~(Enum a) { return Enum(~int(a)); }                                 \
    friend inline Enum &operator|=(Enum &a, Enum b) { return a = Enum(int(a) | int(b)); }          \
    friend inline Enum &operator&=(Enum &a, Enum b) { return a = Enum(int(a) & int(b)); }
// NOLINTEND(bugprone-macro-parentheses)
#else
#define GX_FLAGS(Enum)
#endif

const Logger &operator<<(const Logger &, const PixelFormat::Format &);
} // namespace gx
