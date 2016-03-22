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

#ifndef SRS_KERNEL_MP3_HPP
#define SRS_KERNEL_MP3_HPP

/*
#include <srs_kernel_mp3.hpp>
*/
#include <srs_core.hpp>

#if !defined(SRS_EXPORT_LIBRTMP)

#include <string>

class SrsStream;
class SrsFileWriter;

/**
* encode data to aac file.
*/
class SrsMp3Encoder
{
private:
    SrsFileWriter* writer;
private:
    SrsStream* tag_stream;
public:
    SrsMp3Encoder();
    virtual ~SrsMp3Encoder();
public:
    /**
    * initialize the underlayer file stream.
    * @remark user can initialize multiple times to encode multiple mp3 files.
    * @remark, user must free the @param fw, mp3 encoder never close/free it.
    */
    virtual int initialize(SrsFileWriter* fw);
public:
    /**
    * write mp3 id3 v2.3 header.
    * @see mp3.id3v2.3.0.pdf, http://id3.org/id3v2.3.0
    */
    virtual int write_header();
    /**
    * write audio/video packet.
    * @remark assert data is not NULL.
    */
    virtual int write_audio(int64_t timestamp, char* data, int size);
};

#endif

#endif

