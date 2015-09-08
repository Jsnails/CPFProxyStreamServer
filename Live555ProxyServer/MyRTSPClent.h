#pragma once
#include "MyServerMediaSession.h"


class CMyRTSPClent : public RTSPClient
{

public:
	CMyRTSPClent(class CMyServerMediaSession& ourServerMediaSession, char const* rtspURL,
		char const* username, char const* password,
		portNumBits tunnelOverHTTPPortNum, int verbosityLevel, int socketNumToServer);



	virtual ~CMyRTSPClent();

	void continueAfterDESCRIBE(char const* sdpDescription);
	void continueAfterLivenessCommand(int resultCode, Boolean serverSupportsGetParameter);
	void continueAfterSETUP();

private:
	void reset();

	Authenticator* auth() { return fOurAuthenticator; }

	void scheduleLivenessCommand();
	static void sendLivenessCommand(void* clientData);

	void scheduleDESCRIBECommand();
	static void sendDESCRIBE(void* clientData);

	static void subsessionTimeout(void* clientData);
	void handleSubsessionTimeout();

private:
	friend class CMyServerMediaSession;
	friend class CMyServerMediaSubsession;
	CMyServerMediaSession& fOurServerMediaSession;
	char* fOurURL;
	Authenticator* fOurAuthenticator;
	Boolean fStreamRTPOverTCP;
	class CMyServerMediaSubsession *fSetupQueueHead, *fSetupQueueTail;
	unsigned fNumSetupsDone;
	unsigned fNextDESCRIBEDelay; // in seconds
	Boolean fServerSupportsGetParameter, fLastCommandWasPLAY;
	TaskToken fLivenessCommandTask, fDESCRIBECommandTask, fSubsessionTimerTask;
};


typedef CMyRTSPClent*
	MycreateNewProxyRTSPClientFunc(CMyServerMediaSession& ourServerMediaSession,
	char const* rtspURL,
	char const* username, char const* password,
	portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
	int socketNumToServer);

CMyRTSPClent*
	MydefaultCreateNewProxyRTSPClientFunc(CMyServerMediaSession& ourServerMediaSession,
	char const* rtspURL,
	char const* username, char const* password,
	portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
	int socketNumToServer);