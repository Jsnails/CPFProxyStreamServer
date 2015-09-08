#include "stdafx.h"
#include "MyRTSPClent.h"

#define SUBSESSION_TIMEOUT_SECONDS 10 // how many seconds to wait for the last track's "SETUP" to be done (note below)

///////// RTSP 'response handlers' //////////
static void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) 
{
	char const* res;

	if (resultCode == 0) 
	{
		// The "DESCRIBE" command succeeded, so "resultString" should be the stream's SDP description.
		res = resultString;
	} 
	else 
	{
		// The "DESCRIBE" command failed.
		res = NULL;
	}
	((CMyRTSPClent*)rtspClient)->continueAfterDESCRIBE(res);
	delete[] resultString;
}

static void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) 
{
	if (resultCode == 0) 
	{
		((CMyRTSPClent*)rtspClient)->continueAfterSETUP();
	}
	delete[] resultString;
}

static void continueAfterOPTIONS(RTSPClient* rtspClient, int resultCode, char* resultString)
{
	Boolean serverSupportsGetParameter = False;
	if (resultCode == 0) 
	{
		// Note whether the server told us that it supports the "GET_PARAMETER" command:
		serverSupportsGetParameter = RTSPOptionIsSupported("GET_PARAMETER", resultString);
	}
	((CMyRTSPClent*)rtspClient)->continueAfterLivenessCommand(resultCode, serverSupportsGetParameter);
	delete[] resultString;
}

#ifdef SEND_GET_PARAMETER_IF_SUPPORTED
static void continueAfterGET_PARAMETER(RTSPClient* rtspClient, int resultCode, char* resultString) 
{
	((CMyRTSPClent*)rtspClient)->continueAfterLivenessCommand(resultCode, True);
	delete[] resultString;
}
#endif

UsageEnvironment& operator<<(UsageEnvironment& env, const CMyRTSPClent& proxyRTSPClient) 
{ // used for debugging
	return env << "ProxyRTSPClient[\"" << proxyRTSPClient.url() << "\"]";
}

CMyRTSPClent::CMyRTSPClent(class CMyServerMediaSession& ourServerMediaSession, char const* rtspURL,
						   char const* username, char const* password,
						   portNumBits tunnelOverHTTPPortNum, int verbosityLevel, int socketNumToServer)
						   : RTSPClient(ourServerMediaSession.envir(), rtspURL, verbosityLevel, "ProxyRTSPClient",
						   tunnelOverHTTPPortNum == (portNumBits)(~0) ? 0 : tunnelOverHTTPPortNum, socketNumToServer),
						   fOurServerMediaSession(ourServerMediaSession), 
						   fOurURL(strDup(rtspURL)), 
						   fStreamRTPOverTCP(tunnelOverHTTPPortNum != 0),
						   fSetupQueueHead(NULL), 
						   fSetupQueueTail(NULL), 
						   fNumSetupsDone(0), 
						   fNextDESCRIBEDelay(1),
						   fServerSupportsGetParameter(False), 
						   fLastCommandWasPLAY(False),
						   fLivenessCommandTask(NULL), 
						   fDESCRIBECommandTask(NULL), 
						   fSubsessionTimerTask(NULL) 
{
}


CMyRTSPClent::~CMyRTSPClent(void)
{
	reset();

	delete fOurAuthenticator;
	delete[] fOurURL;
}

void CMyRTSPClent::continueAfterDESCRIBE(char const* sdpDescription)
{
	if (sdpDescription != NULL) 
	{
		fOurServerMediaSession.continueAfterDESCRIBE(sdpDescription);

		// Unlike most RTSP streams, there might be a long delay between this "DESCRIBE" command (to the downstream server) and the
		// subsequent "SETUP"/"PLAY" - which doesn't occur until the first time that a client requests the stream.
		// To prevent the proxied connection (between us and the downstream server) from timing out, we send periodic 'liveness'
		// ("OPTIONS" or "GET_PARAMETER") commands.  (The usual RTCP liveness mechanism wouldn't work here, because RTCP packets
		// don't get sent until after the "PLAY" command.)
		scheduleLivenessCommand();
	}
	else 
	{
		// The "DESCRIBE" command failed, most likely because the server or the stream is not yet running.
		// Reschedule another "DESCRIBE" command to take place later:
		scheduleDESCRIBECommand();
	}
}

void CMyRTSPClent::continueAfterLivenessCommand( int resultCode, Boolean serverSupportsGetParameter )
{
	if (resultCode != 0) {
		// The periodic 'liveness' command failed, suggesting that the back-end stream is no longer alive.
		// We handle this by resetting our connection state with this server.  Any current clients will be closed, but
		// subsequent clients will cause new RTSP "SETUP"s and "PLAY"s to get done, restarting the stream.
		// Then continue by sending more "DESCRIBE" commands, to try to restore the stream.

		fServerSupportsGetParameter = False; // until we learn otherwise, in response to a future "OPTIONS" command

		if (resultCode < 0) {
			// The 'liveness' command failed without getting a response from the server (otherwise "resultCode" would have been > 0).
			// This suggests that the RTSP connection itself has failed.  Print this error code, in case it's useful for debugging:
			if (fVerbosityLevel > 0) {
				envir() << *this << ": lost connection to server ('errno': " << -resultCode << ").  Resetting...\n";
			}
		}

		reset();
		fOurServerMediaSession.resetDESCRIBEState();

		setBaseURL(fOurURL); // because we'll be sending an initial "DESCRIBE" all over again
		sendDESCRIBE(this);
		return;
	}

	fServerSupportsGetParameter = serverSupportsGetParameter;

	// Schedule the next 'liveness' command (i.e., to tell the back-end server that we're still alive):
	scheduleLivenessCommand();
}

void CMyRTSPClent::continueAfterSETUP()
{
	if (fVerbosityLevel > 0) 
	{
		//envir() << *this << "::continueAfterSETUP(): head codec: " << fSetupQueueHead->fClientMediaSubsession.codecName()
		//	<< "; numSubsessions " << fSetupQueueHead->fParentSession->numSubsessions() << "\n\tqueue:";

		for (CMyServerMediaSubsession* p = fSetupQueueHead; p != NULL; p = p->fNext) 
		{
			envir() << "\t" << p->fClientMediaSubsession.codecName();
		}
		envir() << "\n";
	}
	envir().taskScheduler().unscheduleDelayedTask(fSubsessionTimerTask); // in case it had been set

	// Dequeue the first "ProxyServerMediaSubsession" from our 'SETUP queue'.  It will be the one for which this "SETUP" was done:
	CMyServerMediaSubsession* smss = fSetupQueueHead; // Assert: != NULL
	fSetupQueueHead = fSetupQueueHead->fNext;
	if (fSetupQueueHead == NULL) fSetupQueueTail = NULL;

	if (fSetupQueueHead != NULL) 
	{
		// There are still entries in the queue, for tracks for which we have still to do a "SETUP".
		// "SETUP" the first of these now:
		sendSetupCommand(fSetupQueueHead->fClientMediaSubsession, ::continueAfterSETUP,
			False, fStreamRTPOverTCP, False, fOurAuthenticator);
		++fNumSetupsDone;
		fSetupQueueHead->fHaveSetupStream = True;
	}
	else 
	{
		if (fNumSetupsDone >= smss->GetfParentSession()->numSubsessions()) 
		{
			// We've now finished setting up each of our subsessions (i.e., 'tracks').
			// Continue by sending a "PLAY" command (an 'aggregate' "PLAY" command, on the whole session):
			sendPlayCommand(smss->fClientMediaSubsession.parentSession(), NULL, -1.0f, -1.0f, 1.0f, fOurAuthenticator);
			// the "-1.0f" "start" parameter causes the "PLAY" to be sent without a "Range:" header, in case we'd already done
			// a "PLAY" before (as a result of a 'subsession timeout' (note below))
			fLastCommandWasPLAY = True;
		} 
		else 
		{
			// Some of this session's subsessions (i.e., 'tracks') remain to be "SETUP".  They might get "SETUP" very soon, but it's
			// also possible - if the remote client chose to play only some of the session's tracks - that they might not.
			// To allow for this possibility, we set a timer.  If the timer expires without the remaining subsessions getting "SETUP",
			// then we send a "PLAY" command anyway:
			fSubsessionTimerTask = envir().taskScheduler().scheduleDelayedTask(SUBSESSION_TIMEOUT_SECONDS*MILLION, (TaskFunc*)subsessionTimeout, this);
		}
	}
}

void CMyRTSPClent::reset()
{
	envir().taskScheduler().unscheduleDelayedTask(fLivenessCommandTask); 
	fLivenessCommandTask = NULL;
	envir().taskScheduler().unscheduleDelayedTask(fDESCRIBECommandTask); 
	fDESCRIBECommandTask = NULL;
	envir().taskScheduler().unscheduleDelayedTask(fSubsessionTimerTask); 
	fSubsessionTimerTask = NULL;

	fSetupQueueHead = fSetupQueueTail = NULL;
	fNumSetupsDone = 0;
	fNextDESCRIBEDelay = 1;
	fLastCommandWasPLAY = False;

	RTSPClient::reset();
}

void CMyRTSPClent::scheduleLivenessCommand()
{
	 //Delay a random time before sending another 'liveness' command.
	unsigned delayMax = sessionTimeoutParameter(); // if the server specified a maximum time between 'liveness' probes, then use that
	if (delayMax == 0) 
	{
		delayMax = 60;
	}

	// Choose a random time from [delayMax/2,delayMax-1) seconds:
	unsigned const us_1stPart = delayMax*500000;
	unsigned uSecondsToDelay;
	if (us_1stPart <= 1000000) 
	{
		uSecondsToDelay = us_1stPart;
	} 
	else 
	{
		unsigned const us_2ndPart = us_1stPart-1000000;
		uSecondsToDelay = us_1stPart + (us_2ndPart*our_random())%us_2ndPart;
	}

	fLivenessCommandTask = envir().taskScheduler().scheduleDelayedTask(uSecondsToDelay, sendLivenessCommand, this);
}

void CMyRTSPClent::sendLivenessCommand( void* clientData )
{
	CMyRTSPClent* rtspClient = (CMyRTSPClent*)clientData;

	// Note.  By default, we do not send "GET_PARAMETER" as our 'liveness notification' command, even if the server previously
	// indicated (in its response to our earlier "OPTIONS" command) that it supported "GET_PARAMETER".  This is because
	// "GET_PARAMETER" crashes some camera servers (even though they claimed to support "GET_PARAMETER").
#ifdef SEND_GET_PARAMETER_IF_SUPPORTED
	MediaSession* sess = rtspClient->fOurServerMediaSession.fClientMediaSession;

	if (rtspClient->fServerSupportsGetParameter && rtspClient->fNumSetupsDone > 0 && sess != NULL) 
	{
		rtspClient->sendGetParameterCommand(*sess, ::continueAfterGET_PARAMETER, "", rtspClient->auth());
	}
	else 
	{
#endif
		rtspClient->sendOptionsCommand(::continueAfterOPTIONS, rtspClient->auth());
#ifdef SEND_GET_PARAMETER_IF_SUPPORTED
	}
#endif
}

void CMyRTSPClent::scheduleDESCRIBECommand()
{
	// Delay 1s, 2s, 4s, 8s ... 256s until sending the next "DESCRIBE".  Then, keep delaying a random time from [256..511] seconds:
	unsigned secondsToDelay;
	if (fNextDESCRIBEDelay <= 256) 
	{
		secondsToDelay = fNextDESCRIBEDelay;
		fNextDESCRIBEDelay *= 2;
	} 
	else 
	{
		secondsToDelay = 256 + (our_random()&0xFF); // [256..511] seconds
	}

	if (fVerbosityLevel > 0) 
	{
		envir() << *this << ": RTSP \"DESCRIBE\" command failed; trying again in " << secondsToDelay << " seconds\n";
	}
	fDESCRIBECommandTask = envir().taskScheduler().scheduleDelayedTask(secondsToDelay*MILLION, sendDESCRIBE, this);
}

void CMyRTSPClent::sendDESCRIBE( void* clientData )
{
	CMyRTSPClent* rtspClient = (CMyRTSPClent*)clientData;
	if (rtspClient != NULL) 
		rtspClient->sendDescribeCommand(::continueAfterDESCRIBE, rtspClient->auth());
}

void CMyRTSPClent::subsessionTimeout( void* clientData )
{
	 ((CMyRTSPClent*)clientData)->handleSubsessionTimeout();
}

void CMyRTSPClent::handleSubsessionTimeout()
{
	// We still have one or more subsessions ('tracks') left to "SETUP".  But we can't wait any longer for them.  Send a "PLAY" now:
    MediaSession* sess = fOurServerMediaSession.fClientMediaSession;
    if (sess != NULL) 
        sendPlayCommand(*sess, NULL, -1.0f, -1.0f, 1.0f, fOurAuthenticator);
    fLastCommandWasPLAY = True;
}


CMyRTSPClent* MydefaultCreateNewProxyRTSPClientFunc(CMyServerMediaSession& ourServerMediaSession,
	char const* rtspURL,
	char const* username, char const* password,
	portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
	int socketNumToServer)
{
	return new CMyRTSPClent(ourServerMediaSession, rtspURL, username, password,tunnelOverHTTPPortNum, verbosityLevel, socketNumToServer);
}

