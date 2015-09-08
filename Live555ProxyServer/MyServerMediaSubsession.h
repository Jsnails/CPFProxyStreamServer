#pragma once
#include "MyServerMediaSession.h"
class CMyServerMediaSubsession : public OnDemandServerMediaSubsession
{
public:
	CMyServerMediaSubsession(MediaSubsession& mediaSubsession);
	virtual ~CMyServerMediaSubsession();

	char const* codecName() const { return fClientMediaSubsession.codecName(); }

	ServerMediaSession * GetfParentSession();

private: // redefined virtual functions
	virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
		unsigned& estBitrate);
	virtual void closeStreamSource(FramedSource *inputSource);
	virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
		unsigned char rtpPayloadTypeIfDynamic,
		FramedSource* inputSource);
//protected: // redefined virtual functions
//	virtual char const* sdpLines();
private:
	
	static void subsessionByeHandler(void* clientData);
	void subsessionByeHandler();

	int verbosityLevel() const { return ((CMyServerMediaSession*)fParentSession)->fVerbosityLevel; }

public:
	friend class ProxyRTSPClient;
	MediaSubsession& fClientMediaSubsession; // the 'client' media subsession object that corresponds to this 'server' media subsession
	CMyServerMediaSubsession* fNext; // used when we're part of a queue
	Boolean fHaveSetupStream;
};

