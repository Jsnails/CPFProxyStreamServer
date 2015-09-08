// stdafx.h : ��׼ϵͳ�����ļ��İ����ļ���
// ���Ǿ���ʹ�õ��������ĵ�
// �ض�����Ŀ�İ����ļ�
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>
#include <string>
using namespace std;
#include <RTSPClient.hh>
#include <BasicUsageEnvironment.hh>
#include <OnDemandServerMediaSubsession.hh>
#include <RTSPCommon.hh>
#include <Media.hh>
#include <GroupsockHelper.hh>
#include <H264VideoStreamDiscreteFramer.hh>
#include <H265VideoStreamDiscreteFramer.hh>
#include <MPEG4VideoStreamDiscreteFramer.hh>
#include <DVVideoStreamFramer.hh>
#include <MPEG1or2VideoStreamDiscreteFramer.hh>
#include <SimpleRTPSink.hh>
#include <VP8VideoRTPSink.hh>
#include <VorbisAudioRTPSink.hh>
#include <TheoraVideoRTPSink.hh>
#include <T140TextRTPSink.hh>
#include <MPEG1or2VideoRTPSink.hh>
#include <MPEG4GenericRTPSink.hh>
#include <MP3ADURTPSink.hh>
#include <MPEG1or2AudioRTPSink.hh>
#include <MPEG4ESVideoRTPSink.hh>
#include <MPEG4LATMAudioRTPSink.hh>
#include <H265VideoRTPSink.hh>
#include <H264VideoRTPSink.hh>
#include <H263plusVideoRTPSink.hh>
#include <GSMAudioRTPSink.hh>
#include <DVVideoRTPSink.hh>
#include <AC3AudioRTPSink.hh>
#include <ProxyServerMediaSession.hh>

#include "MyRTSPClent.h"
#include "MyServerMediaSubsession.h"
#include "MyServerMediaSession.h"

// TODO: �ڴ˴����ó�����Ҫ������ͷ�ļ�
static const int MILLION = 1000000;

#define SEND_GET_PARAMETER_IF_SUPPORTED