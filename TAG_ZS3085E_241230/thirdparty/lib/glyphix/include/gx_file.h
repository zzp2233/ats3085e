/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_string.h"
#include "platform/gx_os.h"

namespace gx {
class ByteArray;

/*!
 * \brief The File class provides resources to read and write to source files.
 */
class File {
public:
    //! This enumeration specifies the mode in which the open() function opens the file. These
    //! enumeration values can be combined using the `|` operator.
    enum OpenFlags {
        NoneFlag = 0,                     //!< No flags. In this state when the file is not open.
        Text = 0x01,                      //!< Open the file in text mode.
        ExistingOnly = 0x02,              //!< The file to be opened must exist.
        ReadOnly = 0x04,                  //!< Open the file as read-only.
        WriteOnly = 0x08,                 //!< Open the file as write-only.
        ReadWrite = ReadOnly | WriteOnly, //!< Open the file as read and write.
        //! Open the file in append mode (write mode). When opening a file in this mode, the file
        //! pointer will be set to the end of file.
        Append = 0x10 | WriteOnly,
        //! Open the file (write mode) and clear the contents of the original file first.
        Truncate = 0x20 | WriteOnly
    };
    GX_FLAGS(OpenFlags);
    //! The type used to indicate the file offset is actually an alias for os::off_t.
    typedef os::off_t off_t; // NOLINT(readability-*)

    //! The default constructor, which constructs an empty file object. The uri() property of this
    //! file object is empty.
    File();
    //! Constructs a file object. \p uri specifies the URI of this file object.
    //! \note Executing the constructor does not open the file, so you must call the open() member
    //! function to open the file before reading or writing it.
    explicit File(const String &uri);
    ~File(); //!< Destructor, which will automatically close if the file is already open.

    //! Check if the current file object is already open.
    GX_NODISCARD bool isOpened() const { return m_flags != 0; }
    //! Check that the file is writable.
    //! \return This function returns true when the file is opened in write mode, otherwise it
    //! returns false.
    GX_NODISCARD bool isWritable() const { return m_flags & WriteOnly; }
    //! Check that the file is readable.
    //! \return This function returns true when the file is opened in read mode, otherwise it
    //! returns false.
    GX_NODISCARD bool isReadable() const { return m_flags & ReadOnly; }
    //! Get the URI of the file.
    //! \return URI of the file. If the file has been opened or checked for existence, this URI is
    //! the absolute URI of the file in the resource framework, not necessarily the URI specified
    //! when the file was opened.
    GX_NODISCARD const String &uri() const { return m_uri; }
    //! Set the URI of the current file object, this function is only valid if the file is not open.
    void setUri(const String &uri);
    GX_NODISCARD bool open(OpenFlags flags = ReadOnly);
    bool close();
    std::size_t write(const ByteArray &data);
    std::size_t write(const void *data, std::size_t count);
    std::size_t read(void *buffer, std::size_t count);
    GX_NODISCARD ByteArray read(std::size_t maxsize);
    GX_NODISCARD ByteArray read();
    GX_NODISCARD const void *rawptr();
    GX_NODISCARD off_t tell();
    off_t seek(off_t offset);
    off_t seek(off_t offset, int whence);
    GX_NODISCARD off_t size();
    GX_NODISCARD bool exists();
    void swap(File &file);

    GX_NODISCARD static bool exists(const String &uri);
    GX_NODISCARD static ByteArray read(const String &uri);
    GX_NODISCARD static String locate(const String &origin);
    static bool copy(const String &srcUri, const String &dstUri);

private:
    uint8_t m_flags;
    class ResourceFile *m_file;
    String m_uri;
};
} // namespace gx

template<> inline void ::std::swap(gx::File &x, gx::File &y) { x.swap(y); }
