//
//  RtspPlayer.cpp
//  toolForTest
//
//  Created by cx on 2018/9/6.
//  Copyright © 2018年 cx. All rights reserved.
//

#include "RtspPlayer.hpp"
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
extern "C" {
#include "sdp.h"
}

#define MODULE_TAG "RtspPlayer"

#define log(tag,fmt,...)\
do {\
    printf("%s: ", tag);\
    printf(fmt, ##__VA_ARGS__);\
    printf("\n");\
} while(0)

#define SetNextState(x) _PlayState = x
#define GetNextState()  _PlayState

#define VIDEO_RTP_PORT  (12000)
#define VIDEO_RTCP_PORT (12001)
#define AUDIO_RTP_PORT  (13000)
#define AUDIO_RTCP_PORT (13001)


/*****************************************************************************
* b64_encode: Stolen from VLC's http.c.
* Simplified by Michael.
* Fixed edge cases and made it work from data (vs. strings) by Ryan.
*****************************************************************************/
static inline 
int base64_size(int in_size)
{
    return (((in_size)+2)/3*4+1);
}
static inline
char *base64_encode(char *out, int out_size, const char *in, int in_size)
{
#define __RB32(x)                                    \
    (((uint32_t)((const uint8_t*)(x))[0] << 24) |    \
               (((const uint8_t*)(x))[1] << 16) |    \
               (((const uint8_t*)(x))[2] <<  8) |    \
               (((const uint8_t*)(x))[3] <<  0))

    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *ret, *dst;
    unsigned i_bits = 0;
    int i_shift = 0;
    int bytes_remaining = in_size;

    if (in_size >= INT_MAX / 4 || out_size < base64_size(in_size))
        return NULL;

    ret = dst = out;
    while (bytes_remaining > 3) {
        i_bits = __RB32(in);
        in += 3; bytes_remaining -= 3;
        *dst++ = b64[ i_bits>>26        ];
        *dst++ = b64[(i_bits>>20) & 0x3F];
        *dst++ = b64[(i_bits>>14) & 0x3F];
        *dst++ = b64[(i_bits>>8 ) & 0x3F];
    }
    i_bits = 0;
    while (bytes_remaining) {
        i_bits = (i_bits << 8) + *in++;
        bytes_remaining--;
        i_shift += 8;
    }
    while (i_shift > 0) {
        *dst++ = b64[(i_bits << 6 >> i_shift) & 0x3f];
        i_shift -= 6;
    }
    while ((dst - ret) & 3)
        *dst++ = '=';
    *dst = '\0';

    return ret;
}

namespace rtc {
    RtspPlayer::RtspPlayer() {
        _Terminated = false;
        _NetWorked = false;
        _PlayThreadPtr = NULL;
        SetNextState(RtspIdle);
        _SdpParser = NULL;
        _rtspauth = NULL;
    }
    
    RtspPlayer::~RtspPlayer() {
        Stop();
    }
    
    bool RtspPlayer::GetIPFromUrl(std::string url, char *ip, unsigned short *port) {
        unsigned int dstip[4] = {0};
        int dstport = 0;
        // int field = ::sscanf(url.c_str(), "rtsp://%*[^:]:%*[^@]@%d.%d.%d.%d:%d", &dstip[0], &dstip[1], &dstip[2], &dstip[3], &dstport);
        // TODO(lgY): put this in another function, refactor the logic
        const char *p = url.c_str();
        p += 7; // skip "rtsp://"
        int i = 0, j = 0;
        while (*p != '/' && *p != '\0') {
            j++;
            p++;
        }
        // printf("j=%u\n", j);
        // printf("p=%s\n", p);
        p--; // skip '/' or '\0'
        while (*p != '/' && *p != '@') {
            i++;
            p--;
        }
        // printf("i=%u\n", i);
        // printf("p=%s\n", p);
        p++; // skip '/' or '@'
        // char a[j-i]; // auth
        // ::memcpy(a, p, sizeof(a));
        // a[j-i-1] = '\0';
        // printf("a=%s\n", a);
        // printf("a=%s\n", url.substr(7,j-i-1).c_str());
        if (j==i) { // no-auth
            // _rtspauth.clear();
            // printf("%s\n", _rtspauth.c_str());
            free(_rtspauth);
            _rtspauth = NULL;
            printf("%s\n", _rtspauth);
        } else {
            int size = base64_size(url.substr(7,j-i-1).size());
            _rtspauth = (char *)realloc(_rtspauth, size);
            _rtspauth = base64_encode(_rtspauth, size, url.substr(7,j-i-1).c_str(), url.substr(7,j-i-1).size()); 
            printf("%s\n", _rtspauth);
            // _rtspauth = Base64::Encode(url.substr(7,j-i-1));
            // printf("%s\n%s\n", Base64::Decode(_rtspauth, Base64::DO_PARSE_STRICT).c_str(), _rtspauth.c_str());
        }
        // char u[4+4+4+4+5+1]; // max ip:port len with '\0'
        // ::memcpy(u, p, sizeof(u));
        // u[i] = '\0';
        // printf("u=%s\n", u);
        // printf("u=%s\n", url.substr(7+j-i,i).c_str());
        int field = ::sscanf(url.substr(7+j-i,i).c_str(), "%d.%d.%d.%d:%d", &dstip[0], &dstip[1], &dstip[2], &dstip[3], &dstport);
        if (field == 4) {
            sprintf(ip, "%d.%d.%d.%d", dstip[0], dstip[1], dstip[2], dstip[3]);
            *port = dstport = 554;
        } else if (field == 5) {
            sprintf(ip, "%d.%d.%d.%d", dstip[0], dstip[1], dstip[2], dstip[3]);
            *port = dstport;
        } else {
            log(MODULE_TAG, "failed to get ip from url %d", field);
            return false;
        }
        
        return true;
    }
    
    bool RtspPlayer::NetworkInit(const char *ip, const short port) {
        _RtspSocket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_RtspSocket < 0) {
            log(MODULE_TAG, "network init failed");
            return false;
        }
        _Eventfd = std::max(_RtspSocket, _Eventfd);
        
        int ul = true;
        if (::ioctl(_RtspSocket, FIONBIO, &ul) < 0) {
            log(MODULE_TAG, "set socket non block failed");
            return false;
        }
        
        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = inet_addr(ip);
        
        if (::connect(_RtspSocket, (struct sockaddr *)&serverAddr, (socklen_t)sizeof(serverAddr)) == 0) {
            log(MODULE_TAG, "sync connect success");
        } else if (errno == EINPROGRESS){
            log(MODULE_TAG, "async connecting...");
        } else {
            log(MODULE_TAG, "invalid connect");
            return false;
        }
        
        _NetWorked = true;
        return true;
    }
    
    bool RtspPlayer::RTPSocketInit(int videoPort, int audioPort) {
        if (videoPort) {
            _RtpVideoSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (_RtpVideoSocket < 0) {
                log(MODULE_TAG, "rtp video socket init failed");
                return false;
            }
            _Eventfd = std::max(_RtpVideoSocket, _Eventfd);
            
            int ul = true;
            if (::ioctl(_RtpVideoSocket, FIONBIO, &ul) < 0) {
                log(MODULE_TAG, "failed to set rtp video socket non block");
                ::close(_RtpVideoSocket);
                return false;
            }
            
            _RtpVideoAddr.sin_family = AF_INET;
            //_RtpVideoAddr.sin_addr.s_addr = inet_addr(_rtspip);
            _RtpVideoAddr.sin_addr.s_addr = INADDR_ANY;
            _RtpVideoAddr.sin_port = htons(VIDEO_RTP_PORT);
            
            if (::bind(_RtpVideoSocket, (const struct sockaddr *)&_RtpVideoAddr, (socklen_t)sizeof(_RtpVideoAddr)) < 0) {
                log(MODULE_TAG, "failed to bind rtp video socket error %d %s", errno, strerror(errno));
                return false;
            }
            
            return true;
        }

        return false;
    }
    
    void RtspPlayer::EventInit() {
        FD_ZERO(&_readfd);
        FD_ZERO(&_writefd);
        FD_ZERO(&_errorfd);
        
        FD_SET(_RtspSocket, &_readfd);
        FD_SET(_RtspSocket, &_writefd);
        FD_SET(_RtspSocket, &_errorfd);
    }
    
    std::vector<std::string> RtspPlayer::GetSDPFromMessage(const char *buffer, size_t length, const char *pattern) {
        char *tempBuffer = (char *)malloc(length + 1);
        strcpy(tempBuffer, buffer);
        
        std::vector<std::string> rvector;
        char* tmpStr = strtok(tempBuffer, pattern);
        while (tmpStr)
        {
            rvector.push_back(std::string(tmpStr));
            tmpStr = strtok(NULL, pattern);
        }
        
        free(tempBuffer);
        
        return rvector;
    }
    
    void RtspPlayer::SendDescribe(std::string url) {
        char buf[1024];
        sprintf(buf, "DESCRIBE %s RTSP/1.0\r\n"
                "Accept: application/sdp\r\n"
                "CSeq: %d\r\n"
                "User-Agent: Lavf58.12.100\r\n"
                "\r\n", url.c_str(), RTSPDESCRIBE);
        
        ::send(_RtspSocket, buf, strlen(buf), 0);
    }
    
    void RtspPlayer::HandleDescribe(const char *buf, ssize_t bufsize) {
        // TODO(lgY): put this in another function, refactor the logic
        if (strstr(buf,"Unauthorized")) {
            if (_rtspauth && !_authed) {
                _authed = true;
                if (strstr(buf,"WWW-Authenticate")) {
                    ::sscanf(strstr(buf,"WWW-Authenticate"), "WWW-Authenticate:%s", _rtsprealm);
                    char des[1024];
                    sprintf(des, "DESCRIBE %s RTSP/1.0\r\n"
                            "Accept: application/sdp\r\n"
                            "CSeq: %d\r\n"
                            "User-Agent: Lavf58.12.100\r\n"
                            "Authorization: %s %s\r\n"
                            "\r\n", _rtspurl.c_str(), RTSPDESCRIBE, _rtsprealm, _rtspauth);

                    printf("\r\n%s", des);
                    ::send(_RtspSocket, des, strlen(des), 0);
                }
            }
            return;
        }

        std::vector<std::string> rvector = GetSDPFromMessage(buf, bufsize, "\r\n");
        std::string sdp;
        for (auto substr : rvector) {
            if (strstr(substr.c_str(),"Session:")) {
                ::sscanf(substr.c_str(), "Session:%ld", &_RtspSessionID);
            } else if (strchr(substr.c_str(), '=')) {
                sdp.append(substr);
                sdp.append("\n");
            }
        }
        
        _SdpParser = sdp_parse(sdp.c_str());
    }
    
    void RtspPlayer::RtspSetup(const std::string url, int track, int CSeq, char *proto, short rtp_port, short rtcp_port) {
        char buf[1024];
        const char *track_string = (track == RTSPVIDEO_SETUP) ? "video" : 
                                    (track == RTSPAUDIO_SETUP) ? "audio" : "video";
        if (_rtspauth) {
        sprintf(buf, "SETUP %s/track=%s RTSP/1.0\r\n"
                "CSeq: %d\r\n"
                "User-Agent: Lavf58.12.100\r\n"
                "Transport: %s;unicast;client_port=%d-%d\r\n"
                "Authorization: %s %s\r\n"
                "\r\n", url.c_str(), track_string, CSeq, proto, rtp_port, rtcp_port, _rtsprealm, _rtspauth);
        } else {
        sprintf(buf, "SETUP %s/track=%s RTSP/1.0\r\n"
                "CSeq: %d\r\n"
                "User-Agent: Lavf58.12.100\r\n"
                "Transport: %s;unicast;client_port=%d-%d\r\n"
                "\r\n", url.c_str(), track_string, CSeq, proto, rtp_port, rtcp_port);
        }
        
        printf("\r\n%s", buf);
        ::send(_RtspSocket, buf, strlen(buf), 0);
    }
    
    void RtspPlayer::SendVideoSetup() {
        if (!_SdpParser) {
            log(MODULE_TAG, "sdp parse failed");
            return;
        }

        int i = 0, j = 0;
        int videoTrackID = 0;
        for (i = 0; i < _SdpParser->medias_count; i++) {
            if (strcmp(_SdpParser->medias[i].info.type, "video") == 0) {
                for (j = 0; j < _SdpParser->medias[i].attributes_count; j++) {
                    if (strstr(_SdpParser->medias[i].attributes[j], "trackID")) {
                        ::sscanf(strstr(_SdpParser->medias[i].attributes[j], "trackID"), "trackID=%d", &videoTrackID);
                    }
                }
                RtspSetup(_rtspurl, videoTrackID, RTSPVIDEO_SETUP, _SdpParser->medias[i].info.proto, VIDEO_RTP_PORT, VIDEO_RTCP_PORT);
            }
        }
    }
    
    bool RtspPlayer::HandleVideoSetup(const char *buf, ssize_t bufsize) {
        int rtp_port = 0;
        int rtcp_port = 0;
        int remote_port = 0;
        int remote_rtcp_port = 0;
        
        if (strstr(buf, "client_port=")) {
            ::sscanf(strstr(buf, "client_port="), "client_port=%d-%d", &rtp_port, &rtcp_port);
        }
        
        if(strstr(buf, "server_port=")) {
            ::sscanf(strstr(buf, "server_port="), "server_port=%d-%d", &remote_port, &remote_rtcp_port);
        }
        
        if (!RTPSocketInit(rtp_port ? rtp_port : VIDEO_RTP_PORT, rtp_port ? rtp_port : AUDIO_RTP_PORT)) {
            log(MODULE_TAG, "rtp socket init failed");
            return false;
        }
        
        struct sockaddr_in remoteAddr;
        remoteAddr.sin_family = AF_INET;
        remoteAddr.sin_port = htons(remote_port);
        remoteAddr.sin_addr.s_addr = inet_addr(_rtspip);
        
        const unsigned char natpacket[] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        ::sendto(_RtpVideoSocket, natpacket, sizeof(natpacket), 0, (const struct sockaddr *)&remoteAddr, (socklen_t)sizeof(remoteAddr));
        
        return true;
    }
    
    void RtspPlayer::SendPlay(const std::string url) {
        char buf[1024];
        sprintf(buf, "PLAY %s RTSP/1.0\r\n"
                "CSeq: %u\r\n"
                "Session: %ld\r\n"
                "Range: npt=0.000-\r\n" // Range
                "User-Agent: Lavf58.12.100\r\n"
                "\r\n", url.c_str(), RTSPPLAY, _RtspSessionID);
        
        ::send(_RtspSocket, buf, strlen(buf), 0);
    }
    
    bool RtspPlayer::HandleRtspMsg(const char *buf, ssize_t bufsize) {
        int MsgType = 0;
        if(strstr(buf, "CSeq:")) {
            ::sscanf(strstr(buf, "CSeq:"), "CSeq:%d", &MsgType);
            // printf("\r\n%s", buf);
        } else {
            log(MODULE_TAG, "invalid rtsp message:\r\n\r\n%s", buf);
            return false;
        }
        
        switch (MsgType) {
            case RTSPOPTIONS:
                break;
            case RTSPDESCRIBE:
                HandleDescribe(buf, bufsize);
                SetNextState(RtspSendVideoSetup);
                break;
            case RTSPVIDEO_SETUP:
                if (HandleVideoSetup(buf, bufsize)) {
                    SetNextState(RtspSendPlay);
                }
                break;
            case RTSPAUDIO_SETUP:
                break;
            case RTSPPLAY:
                break;
            default:
                log(MODULE_TAG, "unknow rtsp message");
                break;
        }
        
        return true;
    }
    
    void RtspPlayer::HandleRtspState() {
        switch (GetNextState()) {
            case RtspSendOptions:
                log(MODULE_TAG, "rtsp send options");
                break;
            case RtspHandleOptions:
                log(MODULE_TAG, "rtsp handle options");
                break;
            case RtspSendDescribe:
                log(MODULE_TAG, "rtsp send describe");
                SendDescribe(_rtspurl);
                break;
            case RtspHandleDescribe:
                log(MODULE_TAG, "rtsp handle describe");
                break;
            case RtspSendVideoSetup:
                log(MODULE_TAG, "rtsp send video setup");
                SendVideoSetup();
                break;
            case RtspHandleVideoSetup:
                log(MODULE_TAG, "rtsp handle video setup");
                break;
            case RtspSendAudioSetup:
                log(MODULE_TAG, "rtsp send audio setup");
                break;
            case RtspHandleAudioSetup:
                log(MODULE_TAG, "rtsp handle audio setup");
                break;
            case RtspSendPlay:
                log(MODULE_TAG, "rtsp send play");
                SendPlay(_rtspurl);
                break;
            case RtspHandlePlay:
                log(MODULE_TAG, "rtsp handle play");
                break;
            case RtspSendPause:
                log(MODULE_TAG, "rtsp send pause");
                break;
            case RtspHandlePause:
                log(MODULE_TAG, "rtsp handle pause");
                break;
            case RtspIdle:
                break;
            case RtspTurnOff:
                log(MODULE_TAG, "rtsp turn off");
                break;
            default:
                log(MODULE_TAG, "unknown rtsp state");
                break;
        }
        
        SetNextState(RtspIdle);
    }
    
#define RTP_OFFSET (12)
#define FU_OFFSET (14)
    
    void RtspPlayer::HandleRtpMsg(const char *buf, ssize_t bufsize) {
        char header[] = {0, 0, 0, 1};
        struct Nalu nalu = *(struct Nalu *)(buf + RTP_OFFSET);
        
        if (!_fp) {
            _fp = ::fopen("test.h264", "w+");
            if (!_fp) {
                log(MODULE_TAG, "failed to oepen test.h264");
                return;
            }
        }
        
        if (nalu.type >= 0 && nalu.type < 24) { //one nalu
            ::fwrite(header, 4, 1, _fp);
            ::fwrite(buf + RTP_OFFSET, bufsize - RTP_OFFSET, 1, _fp);
            ::fflush(_fp);
        } else if (nalu.type == 28) { //fu-a slice
            struct FU fu;
            char in = buf[RTP_OFFSET + 1];
            fu.S = in >> 7;
            fu.E = (in >> 6) & 0x01;
            fu.R = (in >> 5) & 0x01;
            fu.type = in & 0x1f;
            
            if (fu.S == 1) {
                char naluType = nalu.forbidden_zero_bit << 7 | nalu.nal_ref_idc << 5 | fu.type;
                ::fwrite(header, 4, 1, _fp);
                ::fwrite(&naluType, 1, 1, _fp);
                ::fwrite(buf + FU_OFFSET, bufsize - FU_OFFSET, 1, _fp);
                ::fflush(_fp);
            } else if (fu.E == 1) {
                ::fwrite(buf + FU_OFFSET, bufsize - FU_OFFSET, 1, _fp);
                ::fflush(_fp);
            } else {
                ::fwrite(buf + FU_OFFSET, bufsize - FU_OFFSET, 1, _fp);
                ::fflush(_fp);
            }
        }
    }
    
    bool RtspPlayer::Play(std::string url) {
        char ip[256];
        unsigned short port = 0;
        
        if (!GetIPFromUrl(url, ip, &port)) {
            log(MODULE_TAG, "get ip and port failed");
            return false;
        }
        _rtspurl = url;
        ::memcpy(_rtspip, ip, sizeof(ip));
        
        if (!NetworkInit(ip, port)) {
            log(MODULE_TAG, "network uninitizial");
            return false;
        }
        
        EventInit();
        
        // internal rtsp play thread
        // _Terminated = false;
        _PlayThreadPtr = new std::thread([&] {
            char recvbuf[2048];
            
            while (!_Terminated) {
                FD_ZERO(&_readfd);
                FD_ZERO(&_errorfd);
                FD_SET(_RtspSocket, &_readfd);
                FD_SET(_RtspSocket, &_errorfd);
                
                // rtp video socket has connected
                if (_RtpVideoSocket) {
                    FD_SET(_RtpVideoSocket, &_readfd);
                }
                
                // On Linux, select() modifies timeout to reflect the amount of time not slept;
                // most other implementations do not do this.  (POSIX.1-2001 permits either behavior.)
                // This causes problems both when Linux code which reads timeout is ported to other
                // operating systems, and when code is ported to Linux that reuses a struct timeval for
                // multiple select()s in a loop without reinitializing it. Consider timeout to be undefined after select() returns.
                struct timeval timeout = {1, 0}; 
                int r = ::select(_Eventfd + 1, &_readfd, &_writefd, &_errorfd, &timeout);
                if (r < 0) {
                    log(MODULE_TAG, "event error %d...", r);
                    break;
                } else if (r == 0) {
                    // log(MODULE_TAG, "event over time...");
                } else {
                    if (FD_ISSET(_RtspSocket, &_readfd)) {
                        ::memset(recvbuf, 0, sizeof(recvbuf));
                        ssize_t recvbytes = ::recv(_RtspSocket, recvbuf, sizeof(recvbuf), 0);
                        if (recvbytes <= 0) {
                            log(MODULE_TAG, "socket peer close");
                            break;
                        } else {
                            if (!HandleRtspMsg(recvbuf, recvbytes)) {
                                log(MODULE_TAG, "failed to handle rtsp msg");
                            }
                        }
                    }
                    
                    if (FD_ISSET(_RtpVideoSocket, &_readfd)) {
                        socklen_t socklen = sizeof(_RtpVideoAddr);
                        ::memset(recvbuf, 0, sizeof(recvbuf));
                        ssize_t recvbytes = ::recvfrom(_RtpVideoSocket, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)&_RtpVideoAddr, &socklen);
                        log(MODULE_TAG, "recv rtp video packet %ld bytes", recvbytes);
                        
                        HandleRtpMsg(recvbuf, recvbytes);
                    }
                    
                    if (FD_ISSET(_RtspSocket, &_writefd)) {
                        log(MODULE_TAG, "async connect success");
                        SetNextState(RtspSendDescribe);
                        FD_CLR(_RtspSocket, &_writefd);
                    }
                    
                    if (FD_ISSET(_RtspSocket, &_errorfd)) {
                        log(MODULE_TAG, "event error occur");
                        break;
                    }
                }
                
                HandleRtspState();
            }
            
            if (_fp) {
                ::fclose(_fp);
            }
        });
        
        return true;
    }
    
    void RtspPlayer::Stop() {
        _Terminated = true;
        SetNextState(RtspTurnOff);
        if (_PlayThreadPtr) {
            _PlayThreadPtr->join();
            delete _PlayThreadPtr;
            _PlayThreadPtr = NULL;
        }
        // NetworkCleanUp
        if (_RtspSocket > 0) {
            ::close(_RtspSocket);
            _RtspSocket = 0;
        }
        // RTPSocketCleanUp
        if (_RtpVideoSocket > 0) {
            ::close(_RtpVideoSocket);
            _RtpVideoSocket = 0;
        }
        // if (_RtpAudioSocket > 0) {
        //     ::close(_RtpAudioSocket);
        //     _RtpAudioSocket = 0;
        // }
        if (_SdpParser) {
            sdp_destroy(_SdpParser);
            _SdpParser = NULL;
        }
        if (_rtspauth) {
            free(_rtspauth);
            _rtspauth = NULL;
        }
    }
}
