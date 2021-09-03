# rtcRtp
rtp/rtcp from webrtc (mainly m88 with h265), add rtsp part

- m66: https://github.com/webrtc-uwp/webrtc
- m88: https://github.com/open-webrtc-toolkit/owt-deps-webrtc

# relative code

- api/rtp_xxx api/rtcp_xxx
- call/rtp_xxx
- media/base/rtp_xxx media/engine/payload_xxx
- module/rtp_rtcp
- pc/xxx_sdp_xxx pc/rtp_xxx

mainly from codes in pc(peer_connection) folder

- client (send: onPacket)
    depacketizer(parser)
- server (recv: Receiver)
    packetizer(generator)

payload: video/audio
other: rtp-session/rtcp-statistic
