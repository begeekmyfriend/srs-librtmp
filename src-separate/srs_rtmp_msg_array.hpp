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

#ifndef SRS_RTMP_PROTOCOL_MSG_ARRAY_HPP
#define SRS_RTMP_PROTOCOL_MSG_ARRAY_HPP

/*
#include <srs_rtmp_msg_array.hpp>
*/

#include <srs_core.hpp>

class SrsSharedPtrMessage;

/**
* the class to auto free the shared ptr message array.
* when need to get some messages, for instance, from Consumer queue,
* create a message array, whose msgs can used to accept the msgs,
* then send each message and set to NULL.
*
* @remark: user must free all msgs in array, for the SRS2.0 protocol stack
*       provides an api to send messages, @see send_and_free_messages
*/
class SrsMessageArray
{
public:
    /**
    * when user already send the msg in msgs, please set to NULL,
    * for instance, msg= msgs.msgs[i], msgs.msgs[i]=NULL, send(msg),
    * where send(msg) will always send and free it.
    */
    SrsSharedPtrMessage** msgs;
    int max;
public:
    /**
    * create msg array, initialize array to NULL ptrs.
    */
    SrsMessageArray(int max_msgs);
    /**
    * free the msgs not sent out(not NULL).
    */
    virtual ~SrsMessageArray();
public:
    /**
    * free specified count of messages.
    */
    virtual void free(int count);
private:
    /**
    * zero initialize the message array.
    */
    virtual void zero(int count);
};

#endif

