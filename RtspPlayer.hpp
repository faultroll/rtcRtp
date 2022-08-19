//
//  RtspPlayer.hpp
//  toolForTest
//
//  Created by cx on 2018/9/6.
//  Copyright © 2018年 cx. All rights reserved.
//

#ifndef RtspPlayer_hpp
#define RtspPlayer_hpp

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
// #include <functional>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h> // sockaddr_in

struct sdp_payload;
namespace rtc {

    enum RtspPlayerState {
        RtspSendOptions = 0,
        RtspHandleOptions,
        RtspSendDescribe,
        RtspHandleDescribe,
        RtspSendVideoSetup,
        RtspHandleVideoSetup,
        RtspSendAudioSetup,
        RtspHandleAudioSetup,
        RtspSendPlay,
        RtspHandlePlay,
        RtspSendPause,
        RtspHandlePause,
        RtspSendTerminate,
        RtspHandleTerminate,
        RtspIdle,
        RtspTurnOff,
    };
    
    enum RtspPlayerCSeq {
        RTSPOPTIONS = 1,
        RTSPDESCRIBE,
        RTSPVIDEO_SETUP,
        RTSPAUDIO_SETUP,
        RTSPPLAY,
        RTSPPAUSE,
        RTSPTEARDOWN,
    };
    
    // h264 nalu
    struct Nalu {
        unsigned type :5;
        unsigned nal_ref_idc :2;
        unsigned forbidden_zero_bit :1;
    };
    // h264 rtp fu
    struct FU {
        unsigned type :5;
        unsigned R :1;
        unsigned E :1;
        unsigned S :1;
    };
    
    class RtspPlayer {
    public:
        RtspPlayer();
        ~RtspPlayer();
        bool Play(std::string url);
        void Stop();
    protected:
        bool NetworkInit(const char *ip, const short port);
        bool RTPSocketInit(int videoPort, int audioPort);
        bool GetIPFromUrl(std::string url, char *ip, unsigned short *port);
        void EventInit();
        bool HandleRtspMsg(const char *buf, ssize_t bufsize);
        void HandleRtspState();
        
        void HandleRtpMsg(const char *buf, ssize_t bufsize);
        
        // rtsp message send/handle function
        void SendDescribe(std::string url);
        void HandleDescribe(const char *buf, ssize_t bufsize);
        void RtspSetup(const std::string url, int track, int CSeq, char *proto, short rtp_port, short rtcp_port);
        void SendVideoSetup();
        bool HandleVideoSetup(const char *buf, ssize_t bufsize);
        void SendPlay(const std::string url);
        
        std::vector<std::string> GetSDPFromMessage(const char *buffer, size_t length, const char *pattern);
    private:
        std::atomic<bool> _Terminated;
        std::atomic<bool> _NetWorked;
        std::thread *_PlayThreadPtr;
        std::atomic<RtspPlayerState> _PlayState;
        fd_set _readfd;
        fd_set _writefd;
        fd_set _errorfd;
        
        std::string _rtspurl;
        char _rtspip[256];
        // std::string _rtspauth; // base64
        char *_rtspauth;
        char _rtsprealm[256];
        bool _authed = false;
        
        int _Eventfd = 0;
        int _RtspSocket = 0;
        int _RtpVideoSocket = 0;
        // int _RtpAudioSocket = 0;
        struct sockaddr_in _RtpVideoAddr;
        
        struct sdp_payload *_SdpParser;
        
        long _RtspSessionID = 0;
        
        // std::function<void(unsigned char *nalu, ssize_t size)> onVideoFrameGet;
        FILE *_fp;
    };
    
} //namespace rtc
#endif /* RtspPlayer_hpp */
