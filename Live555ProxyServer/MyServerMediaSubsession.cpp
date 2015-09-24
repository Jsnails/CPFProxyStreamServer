#include "stdafx.h"
#include "MyServerMediaSubsession.h"

static void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) 
{
	if (resultCode == 0) 
	{
		((CMyRTSPClent*)rtspClient)->continueAfterSETUP();
	}
	delete[] resultString;
}

CMyServerMediaSubsession::CMyServerMediaSubsession(MediaSubsession& mediaSubsession)
	: OnDemandServerMediaSubsession(mediaSubsession.parentSession().envir(), True/*reuseFirstSource*/),
	fClientMediaSubsession(mediaSubsession), fNext(NULL), fHaveSetupStream(False) {
}

UsageEnvironment& operator<<(UsageEnvironment& env, const CMyServerMediaSubsession& psmss) { // used for debugging
	return env << "ProxyServerMediaSubsession[\"" << psmss.codecName() << "\"]";
}

CMyServerMediaSubsession::~CMyServerMediaSubsession() {
	if (verbosityLevel() > 0) {
		envir() << *this << "::~ProxyServerMediaSubsession()\n";
	}
}

FramedSource* CMyServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) 
{
	CMyServerMediaSession* const sms = (CMyServerMediaSession*)fParentSession;

	if (verbosityLevel() > 0) {
		envir() << *this << "::createNewStreamSource(session id " << clientSessionId << ")\n";
	}

	// If we haven't yet created a data source from our 'media subsession' object, initiate() it to do so:
	if (fClientMediaSubsession.readSource() == NULL) {
		fClientMediaSubsession.receiveRawMP3ADUs(); // hack for MPA-ROBUST streams
		fClientMediaSubsession.receiveRawJPEGFrames(); // hack for proxying JPEG/RTP streams. (Don't do this if we're transcoding.)
		fClientMediaSubsession.initiate();
		if (verbosityLevel() > 0) {
			envir() << "\tInitiated: " << *this << "\n";
		}

		if (fClientMediaSubsession.readSource() != NULL) {
			// Add to the front of all data sources a filter that will 'normalize' their frames' presentation times,
			// before the frames get re-transmitted by our server:
			char const* const codecName = fClientMediaSubsession.codecName();
			FramedFilter* normalizerFilter = sms->fPresentationTimeSessionNormalizer
				->createNewPresentationTimeSubsessionNormalizer(fClientMediaSubsession.readSource(), fClientMediaSubsession.rtpSource(),
				codecName);
			fClientMediaSubsession.addFilter(normalizerFilter);

			// Some data sources require a 'framer' object to be added, before they can be fed into
			// a "RTPSink".  Adjust for this now:
			if (strcmp(codecName, "H264") == 0) 
			{
				fClientMediaSubsession.addFilter(H264VideoStreamDiscreteFramer::createNew(envir(), fClientMediaSubsession.readSource()));
			} 
			else if (strcmp(codecName, "H265") == 0) 
			{
				fClientMediaSubsession.addFilter(H265VideoStreamDiscreteFramer::createNew(envir(), fClientMediaSubsession.readSource()));
			} 
			else if (strcmp(codecName, "MP4V-ES") == 0) 
			{
				fClientMediaSubsession.addFilter(MPEG4VideoStreamDiscreteFramer
					::createNew(envir(), fClientMediaSubsession.readSource(),
					True/* leave PTs unmodified*/));
			} 
			else if (strcmp(codecName, "MPV") == 0) 
			{
				fClientMediaSubsession.addFilter(MPEG1or2VideoStreamDiscreteFramer
					::createNew(envir(), fClientMediaSubsession.readSource(),
					False, 5.0, True/* leave PTs unmodified*/));
			} 
			else if (strcmp(codecName, "DV") == 0) 
			{
				fClientMediaSubsession.addFilter(DVVideoStreamFramer
					::createNew(envir(), fClientMediaSubsession.readSource(),
					False, True/* leave PTs unmodified*/));
			}
		}

		if (fClientMediaSubsession.rtcpInstance() != NULL) 
		{
			fClientMediaSubsession.rtcpInstance()->setByeHandler(subsessionByeHandler, this);
		}
	}

	CMyRTSPClent* const proxyRTSPClient = sms->fProxyRTSPClient;
	if (clientSessionId != 0) {
		// We're being called as a result of implementing a RTSP "SETUP".
		if (!fHaveSetupStream) {
			// This is our first "SETUP".  Send RTSP "SETUP" and later "PLAY" commands to the proxied server, to start streaming:
			// (Before sending "SETUP", enqueue ourselves on the "RTSPClient"s 'SETUP queue', so we'll be able to get the correct
			//  "ProxyServerMediaSubsession" to handle the response.  (Note that responses come back in the same order as requests.))
			Boolean queueWasEmpty = proxyRTSPClient->fSetupQueueHead == NULL;
			if (queueWasEmpty) 
			{
				proxyRTSPClient->fSetupQueueHead = this;
			} 
			else 
			{
				proxyRTSPClient->fSetupQueueTail->fNext = this;
			}
			proxyRTSPClient->fSetupQueueTail = this;

			// Hack: If there's already a pending "SETUP" request (for another track), don't send this track's "SETUP" right away, because
			// the server might not properly handle 'pipelined' requests.  Instead, wait until after previous "SETUP" responses come back.
			if (queueWasEmpty) 
			{
				proxyRTSPClient->sendSetupCommand(fClientMediaSubsession, ::continueAfterSETUP,
					False, proxyRTSPClient->fStreamRTPOverTCP, False, proxyRTSPClient->auth());
				++proxyRTSPClient->fNumSetupsDone;
				fHaveSetupStream = True;
			}
		} 
		else 
		{
			// This is a "SETUP" from a new client.  We know that there are no other currently active clients (otherwise we wouldn't
			// have been called here), so we know that the substream was previously "PAUSE"d.  Send "PLAY" downstream once again,
			// to resume the stream:
			if (!proxyRTSPClient->fLastCommandWasPLAY) 
			{ // so that we send only one "PLAY"; not one for each subsession
				proxyRTSPClient->sendPlayCommand(fClientMediaSubsession.parentSession(), NULL, -1.0f/*resume from previous point*/,
					-1.0f, 1.0f, proxyRTSPClient->auth());
				proxyRTSPClient->fLastCommandWasPLAY = True;
			}
		}
	}

	estBitrate = fClientMediaSubsession.bandwidth();
	if (estBitrate == 0) estBitrate = 50; // kbps, estimate
	return fClientMediaSubsession.readSource();
}

void CMyServerMediaSubsession::closeStreamSource(FramedSource* inputSource) {
	if (verbosityLevel() > 0) {
		envir() << *this << "::closeStreamSource()\n";
	}
	// Because there's only one input source for this 'subsession' (regardless of how many downstream clients are proxying it),
	// we don't close the input source here.  (Instead, we wait until *this* object gets deleted.)
	// However, because (as evidenced by this function having been called) we no longer have any clients accessing the stream,
	// then we "PAUSE" the downstream proxied stream, until a new client arrives:
	if (fHaveSetupStream) 
    {
		CMyServerMediaSession* const sms = (CMyServerMediaSession*)fParentSession;
		CMyRTSPClent* const proxyRTSPClient = sms->fProxyRTSPClient;
		if (proxyRTSPClient->fLastCommandWasPLAY) { // so that we send only one "PAUSE"; not one for each subsession
			proxyRTSPClient->sendPauseCommand(fClientMediaSubsession.parentSession(), NULL, proxyRTSPClient->auth());
			proxyRTSPClient->fLastCommandWasPLAY = False;
		}
	}
}

RTPSink* CMyServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) {
		if (verbosityLevel() > 0) {
			envir() << *this << "::createNewRTPSink()\n";
		}

		// Create (and return) the appropriate "RTPSink" object for our codec:
		RTPSink* newSink;
		char const* const codecName = fClientMediaSubsession.codecName();
		if (strcmp(codecName, "AC3") == 0 || strcmp(codecName, "EAC3") == 0) 
		{
			newSink = AC3AudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				fClientMediaSubsession.rtpTimestampFrequency()); 
#if 0 // This code does not work; do *not* enable it:
		} else if (strcmp(codecName, "AMR") == 0 || strcmp(codecName, "AMR-WB") == 0) {
			Boolean isWideband = strcmp(codecName, "AMR-WB") == 0;
			newSink = AMRAudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				isWideband, fClientMediaSubsession.numChannels());
#endif
		} else if (strcmp(codecName, "DV") == 0) {
			newSink = DVVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
		} else if (strcmp(codecName, "GSM") == 0) {
			newSink = GSMAudioRTPSink::createNew(envir(), rtpGroupsock);
		} else if (strcmp(codecName, "H263-1998") == 0 || strcmp(codecName, "H263-2000") == 0) {
			newSink = H263plusVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				fClientMediaSubsession.rtpTimestampFrequency()); 
		} else if (strcmp(codecName, "H264") == 0) {
            newSink = H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
                fClientMediaSubsession.fmtp_spropparametersets());
		} else if (strcmp(codecName, "H265") == 0) {
			newSink = H265VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				fClientMediaSubsession.fmtp_spropvps(),
				fClientMediaSubsession.fmtp_spropsps(),
				fClientMediaSubsession.fmtp_sproppps());
		} else if (strcmp(codecName, "JPEG") == 0) {
			newSink = SimpleRTPSink::createNew(envir(), rtpGroupsock, 26, 90000, "video", "JPEG",
				1/*numChannels*/, False/*allowMultipleFramesPerPacket*/, False/*doNormalMBitRule*/);
		} else if (strcmp(codecName, "MP4A-LATM") == 0) {
			newSink = MPEG4LATMAudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				fClientMediaSubsession.rtpTimestampFrequency(),
				fClientMediaSubsession.fmtp_config(),
				fClientMediaSubsession.numChannels());
		} else if (strcmp(codecName, "MP4V-ES") == 0) {
			newSink = MPEG4ESVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				fClientMediaSubsession.rtpTimestampFrequency(),
				fClientMediaSubsession.attrVal_unsigned("profile-level-id"),
				fClientMediaSubsession.fmtp_config()); 
		} else if (strcmp(codecName, "MPA") == 0) {
			newSink = MPEG1or2AudioRTPSink::createNew(envir(), rtpGroupsock);
		} else if (strcmp(codecName, "MPA-ROBUST") == 0) {
			newSink = MP3ADURTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
		} else if (strcmp(codecName, "MPEG4-GENERIC") == 0) {
			newSink = MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
				rtpPayloadTypeIfDynamic, fClientMediaSubsession.rtpTimestampFrequency(),
				fClientMediaSubsession.mediumName(),
				fClientMediaSubsession.attrVal_str("mode"),
				fClientMediaSubsession.fmtp_config(), fClientMediaSubsession.numChannels());
		} else if (strcmp(codecName, "MPV") == 0) {
			newSink = MPEG1or2VideoRTPSink::createNew(envir(), rtpGroupsock);
		} else if (strcmp(codecName, "OPUS") == 0) {
			newSink = SimpleRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				48000, "audio", "OPUS", 2, False/*only 1 Opus 'packet' in each RTP packet*/);
		} else if (strcmp(codecName, "T140") == 0) {
			newSink = T140TextRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
		} else if (strcmp(codecName, "THEORA") == 0) {
			newSink = TheoraVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				fClientMediaSubsession.fmtp_config()); 
		} else if (strcmp(codecName, "VORBIS") == 0) {
			newSink = VorbisAudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				fClientMediaSubsession.rtpTimestampFrequency(), fClientMediaSubsession.numChannels(),
				fClientMediaSubsession.fmtp_config()); 
		} else if (strcmp(codecName, "VP8") == 0) {
			newSink = VP8VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
		} else if (strcmp(codecName, "AMR") == 0 || strcmp(codecName, "AMR-WB") == 0) {
			// Proxying of these codecs is currently *not* supported, because the data received by the "RTPSource" object is not in a
			// form that can be fed directly into a corresponding "RTPSink" object.
			if (verbosityLevel() > 0) {
				envir() << "\treturns NULL (because we currently don't support the proxying of \""
					<< fClientMediaSubsession.mediumName() << "/" << codecName << "\" streams)\n";
			}
			return NULL;
		} else if (strcmp(codecName, "QCELP") == 0 ||
			strcmp(codecName, "H261") == 0 ||
			strcmp(codecName, "H263-1998") == 0 || strcmp(codecName, "H263-2000") == 0 ||
			strcmp(codecName, "X-QT") == 0 || strcmp(codecName, "X-QUICKTIME") == 0) {
				// This codec requires a specialized RTP payload format; however, we don't yet have an appropriate "RTPSink" subclass for it:
				if (verbosityLevel() > 0) {
					envir() << "\treturns NULL (because we don't have a \"RTPSink\" subclass for this RTP payload format)\n";
				}
				return NULL;
		} else {
			// This codec is assumed to have a simple RTP payload format that can be implemented just with a "SimpleRTPSink":
			Boolean allowMultipleFramesPerPacket = True; // by default
			Boolean doNormalMBitRule = True; // by default
			// Some codecs change the above default parameters:
			if (strcmp(codecName, "MP2T") == 0) {
				doNormalMBitRule = False; // no RTP 'M' bit
			}
			newSink = SimpleRTPSink::createNew(envir(), rtpGroupsock,
				rtpPayloadTypeIfDynamic, fClientMediaSubsession.rtpTimestampFrequency(),
				fClientMediaSubsession.mediumName(), fClientMediaSubsession.codecName(),
				fClientMediaSubsession.numChannels(), allowMultipleFramesPerPacket, doNormalMBitRule);
		}

		// Because our relayed frames' presentation times are inaccurate until the input frames have been RTCP-synchronized,
		// we temporarily disable RTCP "SR" reports for this "RTPSink" object:
		newSink->enableRTCPReports() = False;

		// Also tell our "PresentationTimeSubsessionNormalizer" object about the "RTPSink", so it can enable RTCP "SR" reports later:
		PresentationTimeSubsessionNormalizer* ssNormalizer;
		if (strcmp(codecName, "H264") == 0 ||
			strcmp(codecName, "H265") == 0 ||
			strcmp(codecName, "MP4V-ES") == 0 ||
			strcmp(codecName, "MPV") == 0 ||
			strcmp(codecName, "DV") == 0) 
		{
				// There was a separate 'framer' object in front of the "PresentationTimeSubsessionNormalizer", so go back one object to get it:
				ssNormalizer = (PresentationTimeSubsessionNormalizer*)(((FramedFilter*)inputSource)->inputSource());
		} 
		else 
		{
			ssNormalizer = (PresentationTimeSubsessionNormalizer*)inputSource;
		}
		ssNormalizer->setRTPSink(newSink);

		return newSink;
}

void CMyServerMediaSubsession::subsessionByeHandler(void* clientData) {
	((CMyServerMediaSubsession*)clientData)->subsessionByeHandler();
}

void CMyServerMediaSubsession::subsessionByeHandler() {
	if (verbosityLevel() > 0) {
		envir() << *this << ": received RTCP \"BYE\".  (The back-end stream has ended.)\n";
	}

	// This "BYE" signals that our input source has (effectively) closed, so pass this onto the front-end clients:
	fHaveSetupStream = False; // hack to stop "PAUSE" getting sent by:
	fClientMediaSubsession.readSource()->handleClosure();

	// And then treat this as if we had lost connection to the back-end server,
	// and can reestablish streaming from it only by sending another "DESCRIBE":
	CMyServerMediaSession* const sms = (CMyServerMediaSession*)fParentSession;
	CMyRTSPClent* const proxyRTSPClient = sms->fProxyRTSPClient;
	proxyRTSPClient->continueAfterLivenessCommand(1/*hack*/, proxyRTSPClient->fServerSupportsGetParameter);
}

ServerMediaSession * CMyServerMediaSubsession::GetfParentSession()
{
	return fParentSession;
}

