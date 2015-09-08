#include "stdafx.h"
#include "MyRTSPClent.h"
#include "MyServerMediaSession.h"
#include "MyServerMediaSubsession.h"

UsageEnvironment& operator<<(UsageEnvironment& env, const CMyServerMediaSession& psms) { // used for debugging
	return env << "ProxyServerMediaSession[\"" << psms.url() << "\"]";
}

CMyServerMediaSession* CMyServerMediaSession
	::createNew(UsageEnvironment& env, RTSPServer* ourRTSPServer,
	char const* inputStreamURL, char const* streamName,
	char const* username, char const* password,
	portNumBits tunnelOverHTTPPortNum, int verbosityLevel, int socketNumToServer) 
{
	return new CMyServerMediaSession(env, ourRTSPServer, inputStreamURL, streamName, username, password,
		tunnelOverHTTPPortNum, verbosityLevel, socketNumToServer);
}


CMyServerMediaSession::CMyServerMediaSession(UsageEnvironment& env, RTSPServer* ourRTSPServer,
    char const* inputStreamURL, char const* streamName,
    char const* username, char const* password,
    portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
    int socketNumToServer,
    void * ourCreateNewProxyRTSPClientFunc)
    : ServerMediaSession(env, streamName, NULL, NULL, False, NULL),
    describeCompletedFlag(0), fOurRTSPServer(ourRTSPServer), fClientMediaSession(NULL),
    fVerbosityLevel(verbosityLevel),
    fPresentationTimeSessionNormalizer(new PresentationTimeSessionNormalizer(envir())),
    fCreateNewProxyRTSPClientFunc(ourCreateNewProxyRTSPClientFunc)
{
	// Open a RTSP connection to the input stream, and send a "DESCRIBE" command.
	// We'll use the SDP description in the response to set ourselves up.
	fProxyRTSPClient= new CMyRTSPClent(*this, inputStreamURL, username, password,
		                                tunnelOverHTTPPortNum,
		                                verbosityLevel > 0 ? verbosityLevel-1 : verbosityLevel,
		                                socketNumToServer);

	CMyRTSPClent::sendDESCRIBE(fProxyRTSPClient);
}

CMyServerMediaSession::~CMyServerMediaSession() {
	if (fVerbosityLevel > 0) {
		envir() << *this << "::~ProxyServerMediaSession()\n";
	}

	// Begin by sending a "TEARDOWN" command (without checking for a response):
	if (fProxyRTSPClient != NULL) 
        fProxyRTSPClient->sendTeardownCommand(*fClientMediaSession, NULL, fProxyRTSPClient->auth());

	// Then delete our state:
	Medium::close(fClientMediaSession);
	Medium::close(fProxyRTSPClient);
	delete fPresentationTimeSessionNormalizer;
}

char const* CMyServerMediaSession::url() const {
	return fProxyRTSPClient == NULL ? NULL : fProxyRTSPClient->url();
}

void CMyServerMediaSession::continueAfterDESCRIBE(char const* sdpDescription) 
{
	describeCompletedFlag = 1;

	// Create a (client) "MediaSession" object from the stream's SDP description ("resultString"), then iterate through its
    // "MediaSubsession" objects, to set up corresponding "ServerMediaSubsession" objects that we'll use to serve the stream's tracks.
    do {
        fClientMediaSession = MediaSession::createNew(envir(), sdpDescription);
        if (fClientMediaSession == NULL) break;

        MediaSubsessionIterator iter(*fClientMediaSession);
        for (MediaSubsession* mss = iter.next(); mss != NULL; mss = iter.next())
        {
            ServerMediaSubsession* smss = new CMyServerMediaSubsession(*mss);//´´½¨ServerMediaSubsession,jamin

            addSubsession(smss);
            if (fVerbosityLevel > 0)
            {
                envir() <<*this << "\n\n" << "added new \"ProxyServerMediaSubsession\" for " << mss->protocolName() << "/" << mss->mediumName() << "/" << mss->codecName() << " track\n";
            }
        }
    } while (0);
}

void CMyServerMediaSession::resetDESCRIBEState() 
{
	// Delete all of our "ProxyServerMediaSubsession"s; they'll get set up again once we get a response to the new "DESCRIBE".
	if (fOurRTSPServer != NULL) {
		// First, close any RTSP client connections that may have already been set up:
		fOurRTSPServer->closeAllClientSessionsForServerMediaSession(this);
	}
	deleteAllSubsessions();

	// Finally, delete the client "MediaSession" object that we had set up after receiving the response to the previous "DESCRIBE":
	Medium::close(fClientMediaSession); fClientMediaSession = NULL;
}

