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

#ifndef SRS_RTMP_PROTOCOL_STACK_HPP
#define SRS_RTMP_PROTOCOL_STACK_HPP

/*
#include <srs_rtmp_stack.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <vector>
#include <string>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_flv.hpp>

class ISrsProtocolReaderWriter;
class SrsFastBuffer;
class SrsPacket;
class SrsStream;
class SrsAmf0Object;
class SrsAmf0Any;
class SrsMessageHeader;
class SrsCommonMessage;
class SrsChunkStream;
class SrsSharedPtrMessage;
class IMergeReadHandler;

class SrsProtocol;
class ISrsProtocolReaderWriter;
class SrsCommonMessage;
class SrsCreateStreamPacket;
class SrsFMLEStartPacket;
class SrsPublishPacket;
class SrsOnMetaDataPacket;
class SrsPlayPacket;
class SrsCommonMessage;
class SrsPacket;
class SrsAmf0Object;
class IMergeReadHandler;

/****************************************************************************
 *****************************************************************************
 ****************************************************************************/
/**
 * amf0 command message, command name macros
 */
#define RTMP_AMF0_COMMAND_CONNECT               "connect"
#define RTMP_AMF0_COMMAND_CREATE_STREAM         "createStream"
#define RTMP_AMF0_COMMAND_CLOSE_STREAM          "closeStream"
#define RTMP_AMF0_COMMAND_PLAY                  "play"
#define RTMP_AMF0_COMMAND_PAUSE                 "pause"
#define RTMP_AMF0_COMMAND_ON_BW_DONE            "onBWDone"
#define RTMP_AMF0_COMMAND_ON_STATUS             "onStatus"
#define RTMP_AMF0_COMMAND_RESULT                "_result"
#define RTMP_AMF0_COMMAND_ERROR                 "_error"
#define RTMP_AMF0_COMMAND_RELEASE_STREAM        "releaseStream"
#define RTMP_AMF0_COMMAND_FC_PUBLISH            "FCPublish"
#define RTMP_AMF0_COMMAND_UNPUBLISH             "FCUnpublish"
#define RTMP_AMF0_COMMAND_PUBLISH               "publish"
#define RTMP_AMF0_DATA_SAMPLE_ACCESS            "|RtmpSampleAccess"

/**
 * the signature for packets to client.
 */
#define RTMP_SIG_FMS_VER                        "3,5,3,888"
#define RTMP_SIG_AMF0_VER                       0
#define RTMP_SIG_CLIENT_ID                      "ASAICiss"

/**
 * onStatus consts.
 */
#define StatusLevel                             "level"
#define StatusCode                              "code"
#define StatusDescription                       "description"
#define StatusDetails                           "details"
#define StatusClientId                          "clientid"
// status value
#define StatusLevelStatus                       "status"
// status error
#define StatusLevelError                        "error"
// code value
#define StatusCodeConnectSuccess                "NetConnection.Connect.Success"
#define StatusCodeConnectRejected               "NetConnection.Connect.Rejected"
#define StatusCodeStreamReset                   "NetStream.Play.Reset"
#define StatusCodeStreamStart                   "NetStream.Play.Start"
#define StatusCodeStreamPause                   "NetStream.Pause.Notify"
#define StatusCodeStreamUnpause                 "NetStream.Unpause.Notify"
#define StatusCodePublishStart                  "NetStream.Publish.Start"
#define StatusCodeDataStart                     "NetStream.Data.Start"
#define StatusCodeUnpublishSuccess              "NetStream.Unpublish.Success"

/****************************************************************************
*****************************************************************************
****************************************************************************/

/**
 * the decoded message payload.
 * @remark we seperate the packet from message,
 *        for the packet focus on logic and domain data,
 *        the message bind to the protocol and focus on protocol, such as header.
 *         we can merge the message and packet, using OOAD hierachy, packet extends from message,
 *         it's better for me to use components -- the message use the packet as payload.
 */
class SrsPacket
{
public:
    SrsPacket();
    virtual ~SrsPacket();
public:
    /**
     * the subpacket can override this encode,
     * for example, video and audio will directly set the payload withou memory copy,
     * other packet which need to serialize/encode to bytes by override the
     * get_size and encode_packet.
     */
    virtual int encode(int& size, char*& payload);
    // decode functions for concrete packet to override.
public:
    /**
     * subpacket must override to decode packet from stream.
     * @remark never invoke the super.decode, it always failed.
     */
    virtual int decode(SrsStream* stream);
    // encode functions for concrete packet to override.
public:
    /**
     * the cid(chunk id) specifies the chunk to send data over.
     * generally, each message perfer some cid, for example,
     * all protocol control messages perfer RTMP_CID_ProtocolControl,
     * SrsSetWindowAckSizePacket is protocol control message.
     */
    virtual int get_prefer_cid();
    /**
     * subpacket must override to provide the right message type.
     * the message type set the RTMP message type in header.
     */
    virtual int get_message_type();
protected:
    /**
     * subpacket can override to calc the packet size.
     */
    virtual int get_size();
    /**
     * subpacket can override to encode the payload to stream.
     * @remark never invoke the super.encode_packet, it always failed.
     */
    virtual int encode_packet(SrsStream* stream);
};

/**
* the protocol provides the rtmp-message-protocol services,
* to recv RTMP message from RTMP chunk stream,
* and to send out RTMP message over RTMP chunk stream.
*/
class SrsProtocol
{
private:
    class AckWindowSize
    {
    public:
        int ack_window_size;
        int64_t acked_size;
        
        AckWindowSize();
    };
// peer in/out
private:
    /**
    * underlayer socket object, send/recv bytes.
    */
    ISrsProtocolReaderWriter* skt;
    /**
    * requests sent out, used to build the response.
    * key: transactionId
    * value: the request command name
    */
    std::map<double, std::string> requests;
// peer in
private:
    /**
    * chunk stream to decode RTMP messages.
    */
    std::map<int, SrsChunkStream*> chunk_streams;
    /**
    * cache some frequently used chunk header.
    * cs_cache, the chunk stream cache.
    * @see https://github.com/ossrs/srs/issues/249
    */
    SrsChunkStream** cs_cache;
    /**
    * bytes buffer cache, recv from skt, provide services for stream.
    */
    SrsFastBuffer* in_buffer;
    /**
    * input chunk size, default to 128, set by peer packet.
    */
    int32_t in_chunk_size;
    /**
    * input ack size, when to send the acked packet.
    */
    AckWindowSize in_ack_size;
    /**
    * whether auto response when recv messages.
    * default to true for it's very easy to use the protocol stack.
    * @see: https://github.com/ossrs/srs/issues/217
    */
    bool auto_response_when_recv;
    /**
    * when not auto response message, manual flush the messages in queue.
    */
    std::vector<SrsPacket*> manual_response_queue;
// peer out
private:
    /**
    * cache for multiple messages send,
    * initialize to iovec[SRS_CONSTS_IOVS_MAX] and realloc when consumed,
    * it's ok to realloc the iovs cache, for all ptr is ok.
    */
    iovec* out_iovs;
    int nb_out_iovs;
    /**
    * output header cache.
    * used for type0, 11bytes(or 15bytes with extended timestamp) header.
    * or for type3, 1bytes(or 5bytes with extended timestamp) header.
    * the c0c3 caches must use unit SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE bytes.
    * 
    * @remark, the c0c3 cache cannot be realloc.
    */
    char out_c0c3_caches[SRS_CONSTS_C0C3_HEADERS_MAX];
    // whether warned user to increase the c0c3 header cache.
    bool warned_c0c3_cache_dry;
    /**
    * output chunk size, default to 128, set by config.
    */
    int32_t out_chunk_size;
public:
    SrsProtocol(ISrsProtocolReaderWriter* io);
    virtual ~SrsProtocol();
public:
    /**
    * set the auto response message when recv for protocol stack.
    * @param v, whether auto response message when recv message.
    * @see: https://github.com/ossrs/srs/issues/217
    */
    virtual void set_auto_response(bool v);
    /**
    * flush for manual response when the auto response is disabled
    * by set_auto_response(false), we default use auto response, so donot
    * need to call this api(the protocol sdk will auto send message).
    * @see the auto_response_when_recv and manual_response_queue.
    */
    virtual int manual_response_flush();
public:
#ifdef SRS_PERF_MERGED_READ
    /**
    * to improve read performance, merge some packets then read,
    * when it on and read small bytes, we sleep to wait more data.,
    * that is, we merge some data to read together.
    * @param v true to ename merged read.
    * @param handler the handler when merge read is enabled.
    * @see https://github.com/ossrs/srs/issues/241
    */
    virtual void set_merge_read(bool v, IMergeReadHandler* handler);
    /**
    * create buffer with specifeid size.
    * @param buffer the size of buffer.
    * @remark when MR(SRS_PERF_MERGED_READ) disabled, always set to 8K.
    * @remark when buffer changed, the previous ptr maybe invalid.
    * @see https://github.com/ossrs/srs/issues/241
    */
    virtual void set_recv_buffer(int buffer_size);
#endif
public:
    /**
    * set/get the recv timeout in us.
    * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    */
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    /**
    * set/get the send timeout in us.
    * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    */
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    /**
    * get recv/send bytes.
    */
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
public:
    /**
    * recv a RTMP message, which is bytes oriented.
    * user can use decode_message to get the decoded RTMP packet.
    * @param pmsg, set the received message, 
    *       always NULL if error, 
    *       NULL for unknown packet but return success.
    *       never NULL if decode success.
    * @remark, drop message when msg is empty or payload length is empty.
    */
    virtual int recv_message(SrsCommonMessage** pmsg);
    /**
    * decode bytes oriented RTMP message to RTMP packet,
    * @param ppacket, output decoded packet, 
    *       always NULL if error, never NULL if success.
    * @return error when unknown packet, error when decode failed.
    */
    virtual int decode_message(SrsCommonMessage* msg, SrsPacket** ppacket);
    /**
    * send the RTMP message and always free it.
    * user must never free or use the msg after this method,
    * for it will always free the msg.
    * @param msg, the msg to send out, never be NULL.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_message(SrsSharedPtrMessage* msg, int stream_id);
    /**
    * send the RTMP message and always free it.
    * user must never free or use the msg after this method,
    * for it will always free the msg.
    * @param msgs, the msgs to send out, never be NULL.
    * @param nb_msgs, the size of msgs to send out.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id);
    /**
    * send the RTMP packet and always free it.
    * user must never free or use the packet after this method,
    * for it will always free the packet.
    * @param packet, the packet to send out, never be NULL.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
public:
    /**
     * expect a specified message, drop others util got specified one.
     * @pmsg, user must free it. NULL if not success.
     * @ppacket, user must free it, which decode from payload of message. NULL if not success.
     * @remark, only when success, user can use and must free the pmsg and ppacket.
     * for example:
     *          SrsCommonMessage* msg = NULL;
     *          SrsConnectAppResPacket* pkt = NULL;
     *          if ((ret = protocol->expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
     *              return ret;
     *          }
     *          // use then free msg and pkt
     *          srs_freep(msg);
     *          srs_freep(pkt);
     * user should never recv message and convert it, use this method instead.
     * if need to set timeout, use set timeout of SrsProtocol.
     */
    template<class T>
    int expect_message(SrsCommonMessage** pmsg, T** ppacket)
    {
        *pmsg = NULL;
        *ppacket = NULL;
        
        int ret = ERROR_SUCCESS;
        
        while (true) {
            SrsCommonMessage* msg = NULL;
            if ((ret = recv_message(&msg)) != ERROR_SUCCESS) {
                if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                    srs_error("recv message failed. ret=%d", ret);
                }
                return ret;
            }
            srs_verbose("recv message success.");
            
            SrsPacket* packet = NULL;
            if ((ret = decode_message(msg, &packet)) != ERROR_SUCCESS) {
                srs_error("decode message failed. ret=%d", ret);
                srs_freep(msg);
                srs_freep(packet);
                return ret;
            }
            
            T* pkt = dynamic_cast<T*>(packet);
            if (!pkt) {
                srs_info("drop message(type=%d, size=%d, time=%"PRId64", sid=%d).", 
                    msg->header.message_type, msg->header.payload_length,
                    msg->header.timestamp, msg->header.stream_id);
                srs_freep(msg);
                srs_freep(packet);
                continue;
            }
            
            *pmsg = msg;
            *ppacket = pkt;
            break;
        }
        
        return ret;
    }
private:
    /**
    * send out the messages, donot free it, 
    * the caller must free the param msgs.
    */
    virtual int do_send_messages(SrsSharedPtrMessage** msgs, int nb_msgs);
    /**
    * send iovs. send multiple times if exceed limits.
    */
    virtual int do_iovs_send(iovec* iovs, int size);
    /**
    * underlayer api for send and free packet.
    */
    virtual int do_send_and_free_packet(SrsPacket* packet, int stream_id);
    /**
    * use simple algorithm to send the header and bytes.
    * @remark, for do_send_and_free_packet to send.
    */
    virtual int do_simple_send(SrsMessageHeader* mh, char* payload, int size);
    /**
    * imp for decode_message
    */
    virtual int do_decode_message(SrsMessageHeader& header, SrsStream* stream, SrsPacket** ppacket);
    /**
    * recv bytes oriented RTMP message from protocol stack.
    * return error if error occur and nerver set the pmsg,
    * return success and pmsg set to NULL if no entire message got,
    * return success and pmsg set to entire message if got one.
    */
    virtual int recv_interlaced_message(SrsCommonMessage** pmsg);
    /**
    * read the chunk basic header(fmt, cid) from chunk stream.
    * user can discovery a SrsChunkStream by cid.
    */
    virtual int read_basic_header(char& fmt, int& cid);
    /**
    * read the chunk message header(timestamp, payload_length, message_type, stream_id) 
    * from chunk stream and save to SrsChunkStream.
    */
    virtual int read_message_header(SrsChunkStream* chunk, char fmt);
    /**
    * read the chunk payload, remove the used bytes in buffer,
    * if got entire message, set the pmsg.
    */
    virtual int read_message_payload(SrsChunkStream* chunk, SrsCommonMessage** pmsg);
    /**
    * when recv message, update the context.
    */
    virtual int on_recv_message(SrsCommonMessage* msg);
    /**
    * when message sentout, update the context.
    */
    virtual int on_send_packet(SrsMessageHeader* mh, SrsPacket* packet);
private:
    /**
    * auto response the ack message.
    */
    virtual int response_acknowledgement_message();
    /**
    * auto response the ping message.
    */
    virtual int response_ping_message(int32_t timestamp);
};

/**
 * incoming chunk stream maybe interlaced,
 * use the chunk stream to cache the input RTMP chunk streams.
 */
class SrsChunkStream
{
public:
    /**
     * represents the basic header fmt,
     * which used to identify the variant message header type.
     */
    char fmt;
    /**
     * represents the basic header cid,
     * which is the chunk stream id.
     */
    int cid;
    /**
     * cached message header
     */
    SrsMessageHeader header;
    /**
     * whether the chunk message header has extended timestamp.
     */
    bool extended_timestamp;
    /**
     * partially read message.
     */
    SrsCommonMessage* msg;
    /**
     * decoded msg count, to identify whether the chunk stream is fresh.
     */
    int64_t msg_count;
public:
    SrsChunkStream(int _cid);
    virtual ~SrsChunkStream();
};

/**
 * the original request from client.
 */
class SrsRequest
{
public:
    // client ip.
    std::string ip;
public:
    /**
     * tcUrl: rtmp://request_vhost:port/app/stream
     * support pass vhost in query string, such as:
     *    rtmp://ip:port/app?vhost=request_vhost/stream
     *    rtmp://ip:port/app...vhost...request_vhost/stream
     */
    std::string tcUrl;
    std::string pageUrl;
    std::string swfUrl;
    double objectEncoding;
    // data discovery from request.
public:
    // discovery from tcUrl and play/publish.
    std::string schema;
    // the vhost in tcUrl.
    std::string vhost;
    // the host in tcUrl.
    std::string host;
    // the port in tcUrl.
    std::string port;
    // the app in tcUrl, without param.
    std::string app;
    // the param in tcUrl(app).
    std::string param;
    // the stream in play/publish
    std::string stream;
    // for play live stream,
    // used to specified the stop when exceed the duration.
    // @see https://github.com/ossrs/srs/issues/45
    // in ms.
    double duration;
    // the token in the connect request,
    // used for edge traverse to origin authentication,
    // @see https://github.com/ossrs/srs/issues/104
    SrsAmf0Object* args;
public:
    SrsRequest();
    virtual ~SrsRequest();
public:
    /**
     * deep copy the request, for source to use it to support reload,
     * for when initialize the source, the request is valid,
     * when reload it, the request maybe invalid, so need to copy it.
     */
    virtual SrsRequest* copy();
    /**
     * update the auth info of request,
     * to keep the current request ptr is ok,
     * for many components use the ptr of request.
     */
    virtual void update_auth(SrsRequest* req);
    /**
     * get the stream identify, vhost/app/stream.
     */
    virtual std::string get_stream_url();
    /**
     * strip url, user must strip when update the url.
     */
    virtual void strip();
};

/**
 * the response to client.
 */
class SrsResponse
{
public:
    /**
     * the stream id to response client createStream.
     */
    int stream_id;
public:
    SrsResponse();
    virtual ~SrsResponse();
};

/**
 * the rtmp client type.
 */
enum SrsRtmpConnType
{
    SrsRtmpConnUnknown,
    SrsRtmpConnPlay,
    SrsRtmpConnFMLEPublish,
    SrsRtmpConnFlashPublish,
};
std::string srs_client_type_string(SrsRtmpConnType type);
bool srs_client_type_is_publish(SrsRtmpConnType type);

/**
 * store the handshake bytes,
 * for smart switch between complex and simple handshake.
 */
class SrsHandshakeBytes
{
public:
    // [1+1536]
    char* c0c1;
    // [1+1536+1536]
    char* s0s1s2;
    // [1536]
    char* c2;
public:
    SrsHandshakeBytes();
    virtual ~SrsHandshakeBytes();
public:
    virtual int read_c0c1(ISrsProtocolReaderWriter* io);
    virtual int read_s0s1s2(ISrsProtocolReaderWriter* io);
    virtual int read_c2(ISrsProtocolReaderWriter* io);
    virtual int create_c0c1();
    virtual int create_s0s1s2(const char* c1 = NULL);
    virtual int create_c2();
};

/**
 * implements the client role protocol.
 */
class SrsRtmpClient
{
private:
    SrsHandshakeBytes* hs_bytes;
protected:
    SrsProtocol* protocol;
    ISrsProtocolReaderWriter* io;
public:
    SrsRtmpClient(ISrsProtocolReaderWriter* skt);
    virtual ~SrsRtmpClient();
    // protocol methods proxy
public:
    /**
     * set the recv timeout in us.
     * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
     */
    virtual void set_recv_timeout(int64_t timeout_us);
    /**
     * set the send timeout in us.
     * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
     */
    virtual void set_send_timeout(int64_t timeout_us);
    /**
     * get recv/send bytes.
     */
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    /**
     * recv a RTMP message, which is bytes oriented.
     * user can use decode_message to get the decoded RTMP packet.
     * @param pmsg, set the received message,
     *       always NULL if error,
     *       NULL for unknown packet but return success.
     *       never NULL if decode success.
     * @remark, drop message when msg is empty or payload length is empty.
     */
    virtual int recv_message(SrsCommonMessage** pmsg);
    /**
     * decode bytes oriented RTMP message to RTMP packet,
     * @param ppacket, output decoded packet,
     *       always NULL if error, never NULL if success.
     * @return error when unknown packet, error when decode failed.
     */
    virtual int decode_message(SrsCommonMessage* msg, SrsPacket** ppacket);
    /**
     * send the RTMP message and always free it.
     * user must never free or use the msg after this method,
     * for it will always free the msg.
     * @param msg, the msg to send out, never be NULL.
     * @param stream_id, the stream id of packet to send over, 0 for control message.
     */
    virtual int send_and_free_message(SrsSharedPtrMessage* msg, int stream_id);
    /**
     * send the RTMP message and always free it.
     * user must never free or use the msg after this method,
     * for it will always free the msg.
     * @param msgs, the msgs to send out, never be NULL.
     * @param nb_msgs, the size of msgs to send out.
     * @param stream_id, the stream id of packet to send over, 0 for control message.
     */
    virtual int send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id);
    /**
     * send the RTMP packet and always free it.
     * user must never free or use the packet after this method,
     * for it will always free the packet.
     * @param packet, the packet to send out, never be NULL.
     * @param stream_id, the stream id of packet to send over, 0 for control message.
     */
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
public:
    /**
     * handshake with server, try complex, then simple handshake.
     */
    virtual int handshake();
    /**
     * only use simple handshake
     */
    virtual int simple_handshake();
    /**
     * only use complex handshake
     */
    virtual int complex_handshake();
    /**
     * set req to use the original request of client:
     *      pageUrl and swfUrl for refer antisuck.
     *      args for edge to origin traverse auth, @see SrsRequest.args
     */
    virtual int connect_app(std::string app, std::string tc_url, SrsRequest* req, bool debug_srs_upnode);
    /**
     * connect to server, get the debug srs info.
     *
     * @param app, the app to connect at.
     * @param tc_url, the tcUrl to connect at.
     * @param req, the optional req object, use the swfUrl/pageUrl if specified. NULL to ignore.
     *
     * SRS debug info:
     * @param srs_server_ip, debug info, server ip client connected at.
     * @param srs_server, server info.
     * @param srs_primary, primary authors.
     * @param srs_authors, authors.
     * @param srs_id, int, debug info, client id in server log.
     * @param srs_pid, int, debug info, server pid in log.
     */
    virtual int connect_app2(
        std::string app, std::string tc_url, SrsRequest* req, bool debug_srs_upnode,
        std::string& srs_server_ip, std::string& srs_server, std::string& srs_primary,
        std::string& srs_authors, std::string& srs_version, int& srs_id,
        int& srs_pid
    );
    /**
     * create a stream, then play/publish data over this stream.
     */
    virtual int create_stream(int& stream_id);
    /**
     * start play stream.
     */
    virtual int play(std::string stream, int stream_id);
    /**
     * start publish stream. use flash publish workflow:
     *       connect-app => create-stream => flash-publish
     */
    virtual int publish(std::string stream, int stream_id);
    /**
     * start publish stream. use FMLE publish workflow:
     *       connect-app => FMLE publish
     */
    virtual int fmle_publish(std::string stream, int& stream_id);
public:
    /**
     * expect a specified message, drop others util got specified one.
     * @pmsg, user must free it. NULL if not success.
     * @ppacket, user must free it, which decode from payload of message. NULL if not success.
     * @remark, only when success, user can use and must free the pmsg and ppacket.
     * for example:
     *          SrsCommonMessage* msg = NULL;
     *          SrsConnectAppResPacket* pkt = NULL;
     *          if ((ret = client->expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
     *              return ret;
     *          }
     *          // use then free msg and pkt
     *          srs_freep(msg);
     *          srs_freep(pkt);
     * user should never recv message and convert it, use this method instead.
     * if need to set timeout, use set timeout of SrsProtocol.
     */
    template<class T>
    int expect_message(SrsCommonMessage** pmsg, T** ppacket)
    {
        return protocol->expect_message<T>(pmsg, ppacket);
    }
};

/**
 * the rtmp provices rtmp-command-protocol services,
 * a high level protocol, media stream oriented services,
 * such as connect to vhost/app, play stream, get audio/video data.
 */
class SrsRtmpServer
{
private:
    SrsHandshakeBytes* hs_bytes;
    SrsProtocol* protocol;
    ISrsProtocolReaderWriter* io;
public:
    SrsRtmpServer(ISrsProtocolReaderWriter* skt);
    virtual ~SrsRtmpServer();
    // protocol methods proxy
public:
    /**
     * set the auto response message when recv for protocol stack.
     * @param v, whether auto response message when recv message.
     * @see: https://github.com/ossrs/srs/issues/217
     */
    virtual void set_auto_response(bool v);
#ifdef SRS_PERF_MERGED_READ
    /**
     * to improve read performance, merge some packets then read,
     * when it on and read small bytes, we sleep to wait more data.,
     * that is, we merge some data to read together.
     * @param v true to ename merged read.
     * @param handler the handler when merge read is enabled.
     * @see https://github.com/ossrs/srs/issues/241
     */
    virtual void set_merge_read(bool v, IMergeReadHandler* handler);
    /**
     * create buffer with specifeid size.
     * @param buffer the size of buffer.
     * @remark when MR(SRS_PERF_MERGED_READ) disabled, always set to 8K.
     * @remark when buffer changed, the previous ptr maybe invalid.
     * @see https://github.com/ossrs/srs/issues/241
     */
    virtual void set_recv_buffer(int buffer_size);
#endif
    /**
     * set/get the recv timeout in us.
     * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
     */
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    /**
     * set/get the send timeout in us.
     * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
     */
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    /**
     * get recv/send bytes.
     */
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    /**
     * recv a RTMP message, which is bytes oriented.
     * user can use decode_message to get the decoded RTMP packet.
     * @param pmsg, set the received message,
     *       always NULL if error,
     *       NULL for unknown packet but return success.
     *       never NULL if decode success.
     * @remark, drop message when msg is empty or payload length is empty.
     */
    virtual int recv_message(SrsCommonMessage** pmsg);
    /**
     * decode bytes oriented RTMP message to RTMP packet,
     * @param ppacket, output decoded packet,
     *       always NULL if error, never NULL if success.
     * @return error when unknown packet, error when decode failed.
     */
    virtual int decode_message(SrsCommonMessage* msg, SrsPacket** ppacket);
    /**
     * send the RTMP message and always free it.
     * user must never free or use the msg after this method,
     * for it will always free the msg.
     * @param msg, the msg to send out, never be NULL.
     * @param stream_id, the stream id of packet to send over, 0 for control message.
     */
    virtual int send_and_free_message(SrsSharedPtrMessage* msg, int stream_id);
    /**
     * send the RTMP message and always free it.
     * user must never free or use the msg after this method,
     * for it will always free the msg.
     * @param msgs, the msgs to send out, never be NULL.
     * @param nb_msgs, the size of msgs to send out.
     * @param stream_id, the stream id of packet to send over, 0 for control message.
     *
     * @remark performance issue, to support 6k+ 250kbps client,
     *       @see https://github.com/ossrs/srs/issues/194
     */
    virtual int send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id);
    /**
     * send the RTMP packet and always free it.
     * user must never free or use the packet after this method,
     * for it will always free the packet.
     * @param packet, the packet to send out, never be NULL.
     * @param stream_id, the stream id of packet to send over, 0 for control message.
     */
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
public:
    /**
     * handshake with client, try complex then simple.
     */
    virtual int handshake();
    /**
     * do connect app with client, to discovery tcUrl.
     */
    virtual int connect_app(SrsRequest* req);
    /**
     * set ack size to client, client will send ack-size for each ack window
     */
    virtual int set_window_ack_size(int ack_size);
    /**
     * @type: The sender can mark this message hard (0), soft (1), or dynamic (2)
     * using the Limit type field.
     */
    virtual int set_peer_bandwidth(int bandwidth, int type);
    /**
     * @param server_ip the ip of server.
     */
    virtual int response_connect_app(SrsRequest* req, const char* server_ip = NULL);
    /**
     * reject the connect app request.
     */
    virtual void response_connect_reject(SrsRequest* req, const char* desc);
    /**
     * response client the onBWDone message.
     */
    virtual int on_bw_done();
    /**
     * recv some message to identify the client.
     * @stream_id, client will createStream to play or publish by flash,
     *         the stream_id used to response the createStream request.
     * @type, output the client type.
     * @stream_name, output the client publish/play stream name. @see: SrsRequest.stream
     * @duration, output the play client duration. @see: SrsRequest.duration
     */
    virtual int identify_client(int stream_id, SrsRtmpConnType& type, std::string& stream_name, double& duration);
    /**
     * set the chunk size when client type identified.
     */
    virtual int set_chunk_size(int chunk_size);
    /**
     * when client type is play, response with packets:
     * StreamBegin,
     * onStatus(NetStream.Play.Reset), onStatus(NetStream.Play.Start).,
     * |RtmpSampleAccess(false, false),
     * onStatus(NetStream.Data.Start).
     */
    virtual int start_play(int stream_id);
    /**
     * when client(type is play) send pause message,
     * if is_pause, response the following packets:
     *     onStatus(NetStream.Pause.Notify)
     *     StreamEOF
     * if not is_pause, response the following packets:
     *     onStatus(NetStream.Unpause.Notify)
     *     StreamBegin
     */
    virtual int on_play_client_pause(int stream_id, bool is_pause);
    /**
     * when client type is publish, response with packets:
     * releaseStream response
     * FCPublish
     * FCPublish response
     * createStream response
     * onFCPublish(NetStream.Publish.Start)
     * onStatus(NetStream.Publish.Start)
     */
    virtual int start_fmle_publish(int stream_id);
    /**
     * process the FMLE unpublish event.
     * @unpublish_tid the unpublish request transaction id.
     */
    virtual int fmle_unpublish(int stream_id, double unpublish_tid);
    /**
     * when client type is publish, response with packets:
     * onStatus(NetStream.Publish.Start)
     */
    virtual int start_flash_publish(int stream_id);
public:
    /**
     * expect a specified message, drop others util got specified one.
     * @pmsg, user must free it. NULL if not success.
     * @ppacket, user must free it, which decode from payload of message. NULL if not success.
     * @remark, only when success, user can use and must free the pmsg and ppacket.
     * for example:
     *          SrsCommonMessage* msg = NULL;
     *          SrsConnectAppResPacket* pkt = NULL;
     *          if ((ret = server->expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
     *              return ret;
     *          }
     *          // use then free msg and pkt
     *          srs_freep(msg);
     *          srs_freep(pkt);
     * user should never recv message and convert it, use this method instead.
     * if need to set timeout, use set timeout of SrsProtocol.
     */
    template<class T>
    int expect_message(SrsCommonMessage** pmsg, T** ppacket)
    {
        return protocol->expect_message<T>(pmsg, ppacket);
    }
private:
    virtual int identify_create_stream_client(SrsCreateStreamPacket* req, int stream_id, SrsRtmpConnType& type, std::string& stream_name, double& duration);
    virtual int identify_fmle_publish_client(SrsFMLEStartPacket* req, SrsRtmpConnType& type, std::string& stream_name);
    virtual int identify_flash_publish_client(SrsPublishPacket* req, SrsRtmpConnType& type, std::string& stream_name);
private:
    virtual int identify_play_client(SrsPlayPacket* req, SrsRtmpConnType& type, std::string& stream_name, double& duration);
};

/**
* 4.1.1. connect
* The client sends the connect command to the server to request
* connection to a server application instance.
*/
class SrsConnectAppPacket : public SrsPacket
{
public:
    /**
    * Name of the command. Set to "connect".
    */
    std::string command_name;
    /**
    * Always set to 1.
    */
    double transaction_id;
    /**
    * Command information object which has the name-value pairs.
    * @remark: alloc in packet constructor, user can directly use it, 
    *       user should never alloc it again which will cause memory leak.
    * @remark, never be NULL.
    */
    SrsAmf0Object* command_object;
    /**
    * Any optional information
    * @remark, optional, init to and maybe NULL.
    */
    SrsAmf0Object* args;
public:
    SrsConnectAppPacket();
    virtual ~SrsConnectAppPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};
/**
* response for SrsConnectAppPacket.
*/
class SrsConnectAppResPacket : public SrsPacket
{
public:
    /**
    * _result or _error; indicates whether the response is result or error.
    */
    std::string command_name;
    /**
    * Transaction ID is 1 for call connect responses
    */
    double transaction_id;
    /**
    * Name-value pairs that describe the properties(fmsver etc.) of the connection.
    * @remark, never be NULL.
    */
    SrsAmf0Object* props;
    /**
    * Name-value pairs that describe the response from|the server. 'code',
    * 'level', 'description' are names of few among such information.
    * @remark, never be NULL.
    */
    SrsAmf0Object* info;
public:
    SrsConnectAppResPacket();
    virtual ~SrsConnectAppResPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 4.1.2. Call
* The call method of the NetConnection object runs remote procedure
* calls (RPC) at the receiving end. The called RPC name is passed as a
* parameter to the call command.
*/
class SrsCallPacket : public SrsPacket
{
public:
    /**
    * Name of the remote procedure that is called.
    */
    std::string command_name;
    /**
    * If a response is expected we give a transaction Id. Else we pass a value of 0
    */
    double transaction_id;
    /**
    * If there exists any command info this
    * is set, else this is set to null type.
    * @remark, optional, init to and maybe NULL.
    */
    SrsAmf0Any* command_object;
    /**
    * Any optional arguments to be provided
    * @remark, optional, init to and maybe NULL.
    */
    SrsAmf0Any* arguments;
public:
    SrsCallPacket();
    virtual ~SrsCallPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};
/**
* response for SrsCallPacket.
*/
class SrsCallResPacket : public SrsPacket
{
public:
    /**
    * Name of the command. 
    */
    std::string command_name;
    /**
    * ID of the command, to which the response belongs to
    */
    double transaction_id;
    /**
    * If there exists any command info this is set, else this is set to null type.
    * @remark, optional, init to and maybe NULL.
    */
    SrsAmf0Any* command_object;
    /**
    * Response from the method that was called.
    * @remark, optional, init to and maybe NULL.
    */
    SrsAmf0Any* response;
public:
    SrsCallResPacket(double _transaction_id);
    virtual ~SrsCallResPacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 4.1.3. createStream
* The client sends this command to the server to create a logical
* channel for message communication The publishing of audio, video, and
* metadata is carried out over stream channel created using the
* createStream command.
*/
class SrsCreateStreamPacket : public SrsPacket
{
public:
    /**
    * Name of the command. Set to "createStream".
    */
    std::string command_name;
    /**
    * Transaction ID of the command.
    */
    double transaction_id;
    /**
    * If there exists any command info this is set, else this is set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
public:
    SrsCreateStreamPacket();
    virtual ~SrsCreateStreamPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};
/**
* response for SrsCreateStreamPacket.
*/
class SrsCreateStreamResPacket : public SrsPacket
{
public:
    /**
    * _result or _error; indicates whether the response is result or error.
    */
    std::string command_name;
    /**
    * ID of the command that response belongs to.
    */
    double transaction_id;
    /**
    * If there exists any command info this is set, else this is set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
    /**
    * The return value is either a stream ID or an error information object.
    */
    double stream_id;
public:
    SrsCreateStreamResPacket(double _transaction_id, double _stream_id);
    virtual ~SrsCreateStreamResPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* client close stream packet.
*/
class SrsCloseStreamPacket : public SrsPacket
{
public:
    /**
    * Name of the command, set to "closeStream".
    */
    std::string command_name;
    /**
    * Transaction ID set to 0.
    */
    double transaction_id;
    /**
    * Command information object does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
public:
    SrsCloseStreamPacket();
    virtual ~SrsCloseStreamPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
};

/**
* FMLE start publish: ReleaseStream/PublishStream
*/
class SrsFMLEStartPacket : public SrsPacket
{
public:
    /**
    * Name of the command
    */
    std::string command_name;
    /**
    * the transaction ID to get the response.
    */
    double transaction_id;
    /**
    * If there exists any command info this is set, else this is set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
    /**
    * the stream name to start publish or release.
    */
    std::string stream_name;
public:
    SrsFMLEStartPacket();
    virtual ~SrsFMLEStartPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
// factory method to create specified FMLE packet.
public:
    static SrsFMLEStartPacket* create_release_stream(std::string stream);
    static SrsFMLEStartPacket* create_FC_publish(std::string stream);
};
/**
* response for SrsFMLEStartPacket.
*/
class SrsFMLEStartResPacket : public SrsPacket
{
public:
    /**
    * Name of the command
    */
    std::string command_name;
    /**
    * the transaction ID to get the response.
    */
    double transaction_id;
    /**
    * If there exists any command info this is set, else this is set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
    /**
    * the optional args, set to undefined.
    * @remark, never be NULL, an AMF0 undefined instance.
    */
    SrsAmf0Any* args; // undefined
public:
    SrsFMLEStartResPacket(double _transaction_id);
    virtual ~SrsFMLEStartResPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* FMLE/flash publish
* 4.2.6. Publish
* The client sends the publish command to publish a named stream to the
* server. Using this name, any client can play this stream and receive
* the published audio, video, and data messages.
*/
class SrsPublishPacket : public SrsPacket
{
public:
    /**
    * Name of the command, set to "publish".
    */
    std::string command_name;
    /**
    * Transaction ID set to 0.
    */
    double transaction_id;
    /**
    * Command information object does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
    /**
    * Name with which the stream is published.
    */
    std::string stream_name;
    /**
    * Type of publishing. Set to "live", "record", or "append".
    *   record: The stream is published and the data is recorded to a new file.The file
    *           is stored on the server in a subdirectory within the directory that
    *           contains the server application. If the file already exists, it is 
    *           overwritten.
    *   append: The stream is published and the data is appended to a file. If no file
    *           is found, it is created.
    *   live: Live data is published without recording it in a file.
    * @remark, SRS only support live.
    * @remark, optional, default to live.
    */
    std::string type;
public:
    SrsPublishPacket();
    virtual ~SrsPublishPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 4.2.8. pause
* The client sends the pause command to tell the server to pause or
* start playing.
*/
class SrsPausePacket : public SrsPacket
{
public:
    /**
    * Name of the command, set to "pause".
    */
    std::string command_name;
    /**
    * There is no transaction ID for this command. Set to 0.
    */
    double transaction_id;
    /**
    * Command information object does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
    /**
    * true or false, to indicate pausing or resuming play
    */
    bool is_pause;
    /**
    * Number of milliseconds at which the the stream is paused or play resumed.
    * This is the current stream time at the Client when stream was paused. When the
    * playback is resumed, the server will only send messages with timestamps
    * greater than this value.
    */
    double time_ms;
public:
    SrsPausePacket();
    virtual ~SrsPausePacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
};

/**
* 4.2.1. play
* The client sends this command to the server to play a stream.
*/
class SrsPlayPacket : public SrsPacket
{
public:
    /**
    * Name of the command. Set to "play".
    */
    std::string command_name;
    /**
    * Transaction ID set to 0.
    */
    double transaction_id;
    /**
    * Command information does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
    /**
    * Name of the stream to play.
    * To play video (FLV) files, specify the name of the stream without a file
    *       extension (for example, "sample").
    * To play back MP3 or ID3 tags, you must precede the stream name with mp3:
    *       (for example, "mp3:sample".)
    * To play H.264/AAC files, you must precede the stream name with mp4: and specify the
    *       file extension. For example, to play the file sample.m4v, specify 
    *       "mp4:sample.m4v"
    */
    std::string stream_name;
    /**
    * An optional parameter that specifies the start time in seconds.
    * The default value is -2, which means the subscriber first tries to play the live 
    *       stream specified in the Stream Name field. If a live stream of that name is 
    *       not found, it plays the recorded stream specified in the Stream Name field.
    * If you pass -1 in the Start field, only the live stream specified in the Stream 
    *       Name field is played.
    * If you pass 0 or a positive number in the Start field, a recorded stream specified 
    *       in the Stream Name field is played beginning from the time specified in the 
    *       Start field.
    * If no recorded stream is found, the next item in the playlist is played.
    */
    double start;
    /**
    * An optional parameter that specifies the duration of playback in seconds.
    * The default value is -1. The -1 value means a live stream is played until it is no
    *       longer available or a recorded stream is played until it ends.
    * If u pass 0, it plays the single frame since the time specified in the Start field 
    *       from the beginning of a recorded stream. It is assumed that the value specified 
    *       in the Start field is equal to or greater than 0.
    * If you pass a positive number, it plays a live stream for the time period specified 
    *       in the Duration field. After that it becomes available or plays a recorded 
    *       stream for the time specified in the Duration field. (If a stream ends before the
    *       time specified in the Duration field, playback ends when the stream ends.)
    * If you pass a negative number other than -1 in the Duration field, it interprets the 
    *       value as if it were -1.
    */
    double duration;
    /**
    * An optional Boolean value or number that specifies whether to flush any
    * previous playlist.
    */
    bool reset;
public:
    SrsPlayPacket();
    virtual ~SrsPlayPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* response for SrsPlayPacket.
* @remark, user must set the stream_id in header.
*/
class SrsPlayResPacket : public SrsPacket
{
public:
    /**
    * Name of the command. If the play command is successful, the command
    * name is set to onStatus.
    */
    std::string command_name;
    /**
    * Transaction ID set to 0.
    */
    double transaction_id;
    /**
    * Command information does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* command_object; // null
    /**
    * If the play command is successful, the client receives OnStatus message from
    * server which is NetStream.Play.Start. If the specified stream is not found,
    * NetStream.Play.StreamNotFound is received.
    * @remark, never be NULL, an AMF0 object instance.
    */
    SrsAmf0Object* desc;
public:
    SrsPlayResPacket();
    virtual ~SrsPlayResPacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* when bandwidth test done, notice client.
*/
class SrsOnBWDonePacket : public SrsPacket
{
public:
    /**
    * Name of command. Set to "onBWDone"
    */
    std::string command_name;
    /**
    * Transaction ID set to 0.
    */
    double transaction_id;
    /**
    * Command information does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* args; // null
public:
    SrsOnBWDonePacket();
    virtual ~SrsOnBWDonePacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* onStatus command, AMF0 Call
* @remark, user must set the stream_id by SrsCommonMessage.set_packet().
*/
class SrsOnStatusCallPacket : public SrsPacket
{
public:
    /**
    * Name of command. Set to "onStatus"
    */
    std::string command_name;
    /**
    * Transaction ID set to 0.
    */
    double transaction_id;
    /**
    * Command information does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* args; // null
    /**
    * Name-value pairs that describe the response from the server. 
    * 'code','level', 'description' are names of few among such information.
    * @remark, never be NULL, an AMF0 object instance.
    */
    SrsAmf0Object* data;
public:
    SrsOnStatusCallPacket();
    virtual ~SrsOnStatusCallPacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* the special packet for the bandwidth test.
* actually, it's a SrsOnStatusCallPacket, but
* 1. encode with data field, to send data to client.
* 2. decode ignore the data field, donot care.
*/
class SrsBandwidthPacket : public SrsPacket
{
private:
    disable_default_copy(SrsBandwidthPacket);
public:
    /**
    * Name of command. 
    */
    std::string command_name;
    /**
    * Transaction ID set to 0.
    */
    double transaction_id;
    /**
    * Command information does not exist. Set to null type.
    * @remark, never be NULL, an AMF0 null instance.
    */
    SrsAmf0Any* args; // null
    /**
    * Name-value pairs that describe the response from the server.
    * 'code','level', 'description' are names of few among such information.
    * @remark, never be NULL, an AMF0 object instance.
    */
    SrsAmf0Object* data;
public:
    SrsBandwidthPacket();
    virtual ~SrsBandwidthPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
// help function for bandwidth packet.
public:
    virtual bool is_start_play();
    virtual bool is_starting_play();
    virtual bool is_stop_play();
    virtual bool is_stopped_play();
    virtual bool is_start_publish();
    virtual bool is_starting_publish();
    virtual bool is_stop_publish();
    virtual bool is_stopped_publish();
    virtual bool is_finish();
    virtual bool is_final();
    static SrsBandwidthPacket* create_start_play();
    static SrsBandwidthPacket* create_starting_play();
    static SrsBandwidthPacket* create_playing();
    static SrsBandwidthPacket* create_stop_play();
    static SrsBandwidthPacket* create_stopped_play();
    static SrsBandwidthPacket* create_start_publish();
    static SrsBandwidthPacket* create_starting_publish();
    static SrsBandwidthPacket* create_publishing();
    static SrsBandwidthPacket* create_stop_publish();
    static SrsBandwidthPacket* create_stopped_publish();
    static SrsBandwidthPacket* create_finish();
    static SrsBandwidthPacket* create_final();
private:
    virtual SrsBandwidthPacket* set_command(std::string command);
};

/**
* onStatus data, AMF0 Data
* @remark, user must set the stream_id by SrsCommonMessage.set_packet().
*/
class SrsOnStatusDataPacket : public SrsPacket
{
public:
    /**
    * Name of command. Set to "onStatus"
    */
    std::string command_name;
    /**
    * Name-value pairs that describe the response from the server.
    * 'code', are names of few among such information.
    * @remark, never be NULL, an AMF0 object instance.
    */
    SrsAmf0Object* data;
public:
    SrsOnStatusDataPacket();
    virtual ~SrsOnStatusDataPacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* AMF0Data RtmpSampleAccess
* @remark, user must set the stream_id by SrsCommonMessage.set_packet().
*/
class SrsSampleAccessPacket : public SrsPacket
{
public:
    /**
    * Name of command. Set to "|RtmpSampleAccess".
    */
    std::string command_name;
    /**
    * whether allow access the sample of video.
    * @see: https://github.com/ossrs/srs/issues/49
    * @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#videoSampleAccess
    */
    bool video_sample_access;
    /**
    * whether allow access the sample of audio.
    * @see: https://github.com/ossrs/srs/issues/49
    * @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#audioSampleAccess
    */
    bool audio_sample_access;
public:
    SrsSampleAccessPacket();
    virtual ~SrsSampleAccessPacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* the stream metadata.
* FMLE: @setDataFrame
* others: onMetaData
*/
class SrsOnMetaDataPacket : public SrsPacket
{
public:
    /**
    * Name of metadata. Set to "onMetaData"
    */
    std::string name;
    /**
    * Metadata of stream.
    * @remark, never be NULL, an AMF0 object instance.
    */
    SrsAmf0Object* metadata;
public:
    SrsOnMetaDataPacket();
    virtual ~SrsOnMetaDataPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 5.5. Window Acknowledgement Size (5)
* The client or the server sends this message to inform the peer which
* window size to use when sending acknowledgment.
*/
class SrsSetWindowAckSizePacket : public SrsPacket
{
public:
    int32_t ackowledgement_window_size;
public:
    SrsSetWindowAckSizePacket();
    virtual ~SrsSetWindowAckSizePacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 5.3. Acknowledgement (3)
* The client or the server sends the acknowledgment to the peer after
* receiving bytes equal to the window size.
*/
class SrsAcknowledgementPacket : public SrsPacket
{
public:
    int32_t sequence_number;
public:
    SrsAcknowledgementPacket();
    virtual ~SrsAcknowledgementPacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 7.1. Set Chunk Size
* Protocol control message 1, Set Chunk Size, is used to notify the
* peer about the new maximum chunk size.
*/
class SrsSetChunkSizePacket : public SrsPacket
{
public:
    /**
    * The maximum chunk size can be 65536 bytes. The chunk size is
    * maintained independently for each direction.
    */
    int32_t chunk_size;
public:
    SrsSetChunkSizePacket();
    virtual ~SrsSetChunkSizePacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

// 5.6. Set Peer Bandwidth (6)
enum SrsPeerBandwidthType
{
    // The sender can mark this message hard (0), soft (1), or dynamic (2)
    // using the Limit type field.
    SrsPeerBandwidthHard = 0,
    SrsPeerBandwidthSoft = 1,
    SrsPeerBandwidthDynamic = 2,
};

/**
* 5.6. Set Peer Bandwidth (6)
* The client or the server sends this message to update the output
* bandwidth of the peer.
*/
class SrsSetPeerBandwidthPacket : public SrsPacket
{
public:
    int32_t bandwidth;
    // @see: SrsPeerBandwidthType
    int8_t type;
public:
    SrsSetPeerBandwidthPacket();
    virtual ~SrsSetPeerBandwidthPacket();
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

// 3.7. User Control message
enum SrcPCUCEventType
{
    // generally, 4bytes event-data
    
    /**
    * The server sends this event to notify the client
    * that a stream has become functional and can be
    * used for communication. By default, this event
    * is sent on ID 0 after the application connect
    * command is successfully received from the
    * client. The event data is 4-byte and represents
    * the stream ID of the stream that became
    * functional.
    */
    SrcPCUCStreamBegin              = 0x00,

    /**
    * The server sends this event to notify the client
    * that the playback of data is over as requested
    * on this stream. No more data is sent without
    * issuing additional commands. The client discards
    * the messages received for the stream. The
    * 4 bytes of event data represent the ID of the
    * stream on which playback has ended.
    */
    SrcPCUCStreamEOF                = 0x01,

    /**
    * The server sends this event to notify the client
    * that there is no more data on the stream. If the
    * server does not detect any message for a time
    * period, it can notify the subscribed clients 
    * that the stream is dry. The 4 bytes of event 
    * data represent the stream ID of the dry stream. 
    */
    SrcPCUCStreamDry                = 0x02,

    /**
    * The client sends this event to inform the server
    * of the buffer size (in milliseconds) that is 
    * used to buffer any data coming over a stream.
    * This event is sent before the server starts  
    * processing the stream. The first 4 bytes of the
    * event data represent the stream ID and the next
    * 4 bytes represent the buffer length, in 
    * milliseconds.
    */
    SrcPCUCSetBufferLength          = 0x03, // 8bytes event-data

    /**
    * The server sends this event to notify the client
    * that the stream is a recorded stream. The
    * 4 bytes event data represent the stream ID of
    * the recorded stream.
    */
    SrcPCUCStreamIsRecorded         = 0x04,

    /**
    * The server sends this event to test whether the
    * client is reachable. Event data is a 4-byte
    * timestamp, representing the local server time
    * when the server dispatched the command. The
    * client responds with kMsgPingResponse on
    * receiving kMsgPingRequest.  
    */
    SrcPCUCPingRequest              = 0x06,

    /**
    * The client sends this event to the server in
    * response to the ping request. The event data is
    * a 4-byte timestamp, which was received with the
    * kMsgPingRequest request.
    */
    SrcPCUCPingResponse             = 0x07,
    
    /**
     * for PCUC size=3, the payload is "00 1A 01",
     * where we think the event is 0x001a, fms defined msg,
     * which has only 1bytes event data.
     */
    SrsPCUCFmsEvent0                = 0x1a,
};

/**
* 5.4. User Control Message (4)
* 
* for the EventData is 4bytes.
* Stream Begin(=0)              4-bytes stream ID
* Stream EOF(=1)                4-bytes stream ID
* StreamDry(=2)                 4-bytes stream ID
* SetBufferLength(=3)           8-bytes 4bytes stream ID, 4bytes buffer length.
* StreamIsRecorded(=4)          4-bytes stream ID
* PingRequest(=6)               4-bytes timestamp local server time
* PingResponse(=7)              4-bytes timestamp received ping request.
* 
* 3.7. User Control message
* +------------------------------+-------------------------
* | Event Type ( 2- bytes ) | Event Data
* +------------------------------+-------------------------
* Figure 5 Pay load for the 'User Control Message'.
*/
class SrsUserControlPacket : public SrsPacket
{
public:
    /**
    * Event type is followed by Event data.
    * @see: SrcPCUCEventType
    */
    int16_t event_type;
    /**
     * the event data generally in 4bytes.
     * @remark for event type is 0x001a, only 1bytes.
     * @see SrsPCUCFmsEvent0
     */
    int32_t event_data;
    /**
    * 4bytes if event_type is SetBufferLength; otherwise 0.
    */
    int32_t extra_data;
public:
    SrsUserControlPacket();
    virtual ~SrsUserControlPacket();
// decode functions for concrete packet to override.
public:
    virtual int decode(SrsStream* stream);
// encode functions for concrete packet to override.
public:
    virtual int get_prefer_cid();
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

#endif

