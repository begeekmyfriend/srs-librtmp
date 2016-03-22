/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_KERNEL_FILE_HPP
#define SRS_KERNEL_FILE_HPP

/*
#include <srs_kernel_file.hpp>
*/
#include <srs_core.hpp>

#include <string>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

/**
* file writer, to write to file.
*/
class SrsFileWriter
{
private:
    std::string path;
    int fd;
public:
    SrsFileWriter();
    virtual ~SrsFileWriter();
public:
    /**
     * open file writer, in truncate mode.
     * @param p a string indicates the path of file to open.
     */
    virtual int open(std::string p);
    /**
     * open file writer, in append mode.
     * @param p a string indicates the path of file to open.
     */
    virtual int open_append(std::string p);
    /**
     * close current writer.
     * @remark user can reopen again.
     */
    virtual void close();
public:
    virtual bool is_open();
    virtual void lseek(int64_t offset);
    virtual int64_t tellg();
public:
    /**
    * write to file. 
    * @param pnwrite the output nb_write, NULL to ignore.
    */
    virtual int write(void* buf, size_t count, ssize_t* pnwrite);
    /**
     * for the HTTP FLV, to writev to improve performance.
     * @see https://github.com/ossrs/srs/issues/405
     */
    virtual int writev(iovec* iov, int iovcnt, ssize_t* pnwrite);
};

/**
* file reader, to read from file.
*/
class SrsFileReader
{
private:
    std::string path;
    int fd;
public:
    SrsFileReader();
    virtual ~SrsFileReader();
public:
    /**
     * open file reader.
     * @param p a string indicates the path of file to open.
     */
    virtual int open(std::string p);
    /**
     * close current reader.
     * @remark user can reopen again.
     */
    virtual void close();
public:
    // TODO: FIXME: extract interface.
    virtual bool is_open();
    virtual int64_t tellg();
    virtual void skip(int64_t size);
    virtual int64_t lseek(int64_t offset);
    virtual int64_t filesize();
public:
    /**
    * read from file. 
    * @param pnread the output nb_read, NULL to ignore.
    */
    virtual int read(void* buf, size_t count, ssize_t* pnread);
};

#endif

