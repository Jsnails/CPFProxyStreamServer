#pragma once


class CMyServerMediaSession : public ServerMediaSession
{
public:
	static CMyServerMediaSession* createNew(UsageEnvironment& env,
		RTSPServer* ourRTSPServer, // Note: We can be used by just one "RTSPServer"
		char const* inputStreamURL, // the "rtsp://" URL of the stream we'll be proxying
		char const* streamName = NULL,
		char const* username = NULL, char const* password = NULL,
		portNumBits tunnelOverHTTPPortNum = 0,
		// for streaming the *proxied* (i.e., back-end) stream
		int verbosityLevel = 0,
		int socketNumToServer = -1);
	// Hack: "tunnelOverHTTPPortNum" == 0xFFFF (i.e., all-ones) means: Stream RTP/RTCP-over-TCP, but *not* using HTTP
	// "verbosityLevel" == 1 means display basic proxy setup info; "verbosityLevel" == 2 means display RTSP client protocol also.
	// If "socketNumToServer" is >= 0, then it is the socket number of an already-existing TCP connection to the server.
	//      (In this case, "inputStreamURL" must point to the socket's endpoint, so that it can be accessed via the socket.)

	virtual ~CMyServerMediaSession();

	char const* url() const;

	char describeCompletedFlag;
	// initialized to 0; set to 1 when the back-end "DESCRIBE" completes.
	// (This can be used as a 'watch variable' in "doEventLoop()".)
	Boolean describeCompletedSuccessfully() const { return fClientMediaSession != NULL; }
	// This can be used - along with "describeCompletdFlag" - to check whether the back-end "DESCRIBE" completed *successfully*.

protected:
	CMyServerMediaSession(UsageEnvironment& env, RTSPServer* ourRTSPServer,
		char const* inputStreamURL, char const* streamName,
		char const* username, char const* password,
		portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
		int socketNumToServer,
		void* ourCreateNewProxyRTSPClientFunc
		= NULL);

	// If you subclass "ProxyRTSPClient", then you will also need to define your own function
	// - with signature "createNewProxyRTSPClientFunc" (see above) - that creates a new object
	// of this subclass.  You should also subclass "ProxyServerMediaSession" and, in your
	// subclass's constructor, initialize the parent class (i.e., "ProxyServerMediaSession")
	// constructor by passing your new function as the "ourCreateNewProxyRTSPClientFunc"
	// parameter.

protected:
	friend class CMyRTSPClent;
	friend class CMyServerMediaSubsession;

	RTSPServer*   fOurRTSPServer;
	CMyRTSPClent* fProxyRTSPClient;
	MediaSession* fClientMediaSession;

private:
	friend class ProxyRTSPClient;
	friend class ProxyServerMediaSubsession;
	void continueAfterDESCRIBE(char const* sdpDescription);
	void resetDESCRIBEState(); // undoes what was done by "contineAfterDESCRIBE()"

private:
	int fVerbosityLevel;
	class PresentationTimeSessionNormalizer* fPresentationTimeSessionNormalizer;
	void* fCreateNewProxyRTSPClientFunc;
};
