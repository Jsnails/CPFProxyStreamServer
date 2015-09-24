#include "stdafx.h"

#define MEDIA_SERVER_VERSION_STRING "0.82"
//#define ACCESS_CONTROL

int main(int argc, char** argv)
{
    // Begin by setting up our usage environment:
    TaskScheduler* scheduler = BasicTaskScheduler::createNew();//创建Hander

    UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

    UserAuthenticationDatabase* authDB = NULL;

#ifdef ACCESS_CONTROL
    // To implement client access control to the RTSP server, do the following:
    authDB = new UserAuthenticationDatabase;
    authDB->addUserRecord("admin", "admin"); // replace these with real strings
    // Repeat the above with each <username>, <password> that you wish to allow
    // access to the server.
#endif

    RTSPServer* rtspServer;
    portNumBits rtspServerPortNum = 554;

    rtspServer = RTSPServer::createNew(*env, rtspServerPortNum, authDB); //exec 1.
    if (rtspServer == NULL)
    {
        rtspServerPortNum = 8554;
        rtspServer = RTSPServer::createNew(*env, rtspServerPortNum, authDB);
    }

    //string sStreamUrl = "rtsp://192.168.1.250:554/Streaming/Channels/1?transportmode=unicast&profile=Profile_1";
    //string sStreamUrl = "rtsp://admin:12345@192.168.1.250:554/Streaming/Channels/1?transportmode=unicast&profile=Profile_1";
    //string sStreamUrl = "rtsp://218.204.223.237:554/live/1/0547424F573B085C/gsfp90ef4k0a6iap.sdp";
    //string sStreamUrl = "rtsp://218.204.223.237:554/live/1/66251FC11353191F/e7ooqwcfbqjoo80j.sdp";
    //string sStreamUrl = "rtsp://218.204.223.237:554/live/1/67A7572844E51A64/f68g2mj7wjua3la7.sdp";
    //string sStreamUrl = "rtsp://admin:12345@192.168.1.250:554/h264/ch1/main/av_stream";

    string sStreamUrl2 = "rtsp://admin:admin@192.168.1.212:554/cam/realmonitor?channel=1&subtype=0";     //公司大华rtsp://username:password@ip:port/cam/realmonitor?channel=1&subtype=0
    //string sStreamUrl  = "rtsp://218.204.223.237:554/live/1/0547424F573B085C/gsfp90ef4k0a6iap.sdp";    //公司大华rtsp://192.168.1.96/abc.264
    //string sStreamUrl2 = "rtsp://192.168.1.96/abc.264";rtsp://192.168.1.216/profile?token=media_profile1&SessionTimeout=60
    //string sStreamUrl2 = "rtsp://admin:admin@192.168.1.216/profile?token=media_profile1";//索尼

    //CMyServerMediaSession* sms = CMyServerMediaSession::createNew(*env, rtspServer, sStreamUrl.c_str(), "test.264", "", "",554, 0, -1);
    CMyServerMediaSession* sms2 = CMyServerMediaSession::createNew(*env, rtspServer, sStreamUrl2.c_str(), "abc.264", "", "", 554, 3, -1);

    //rtspServer->addServerMediaSession(sms);
    rtspServer->addServerMediaSession(sms2);

    if (rtspServer == NULL)
    {
        *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
        exit(1);
    }

    //*env << "LIVE555 Media Server\n";
    //*env << "\tversion " << MEDIA_SERVER_VERSION_STRING
    //     << " (LIVE555 Streaming Media library version "
    //     << LIVEMEDIA_LIBRARY_VERSION_STRING << ").\n";

    if (rtspServer->setUpTunnelingOverHTTP(80) || rtspServer->setUpTunnelingOverHTTP(8000) || rtspServer->setUpTunnelingOverHTTP(8080))
    {
        *env << "(We use port " << rtspServer->httpServerPortNum() << " for optional RTSP-over-HTTP tunneling, or for HTTP live streaming (for indexed Transport Stream files only).)\n";
    }
    else
    {
        *env << "(RTSP-over-HTTP tunneling is not available.)\n";
    }

    //*env << rtspServer->rtspURL(sms)<<"\n";

    *env << rtspServer->rtspURL(sms2);
    //rtspServer->removeServerMediaSession(sms2);

    env->taskScheduler().doEventLoop(); // does not return
    return 0; // only to prevent compiler warning
}