// traceswitch.cpp
// 
// Copyright (c) 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include <fshell/memoryaccesscmd.h>
#include <fshell/common.mmh>
#include "traceserver.h"
#include <fshell/ltkutils.h>

using namespace IoUtils;

class CCmdTraceSwitch : public CMemoryAccessCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdTraceSwitch();
private:
	CCmdTraceSwitch();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	enum TOperation
		{
		EStatus,
		EStart,
		EStop
		};
	TOperation iOperation;

	RTraceServer iTraceServer;
	//RBuf8 iBuf;
	};

EXE_BOILER_PLATE(CCmdTraceSwitch)

CCommandBase* CCmdTraceSwitch::NewLC()
	{
	CCmdTraceSwitch* self = new(ELeave) CCmdTraceSwitch();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdTraceSwitch::~CCmdTraceSwitch()
	{
	iTraceServer.Close();
	//iBuf.Close();
	}

CCmdTraceSwitch::CCmdTraceSwitch()
	{
	}

const TDesC& CCmdTraceSwitch::Name() const
	{
	_LIT(KName, "traceswitch");	
	return KName;
	}

void CCmdTraceSwitch::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendEnumL((TInt&)iOperation, _L("operation"));
	}

void CCmdTraceSwitch::OptionsL(RCommandOptionList& /*aOptions*/)
	{
	}

void CCmdTraceSwitch::DoRunL()
	{
	LoadMemoryAccessL();

	// Sigh, another futile attempt to block useful tooling
	TProcessProperties overrides;
	overrides.iSid = 0x2002f2fa; // The traceswitchqt app
	RProcess me;
	me.Open(me.Id()); // workaround for KCurrentThreadHandle bug in SetProcessProperties
	LeaveIfErr(iMemAccess.SetProcessProperties(me, overrides), _L("Couldn't override SID"));
	me.Close();

	LeaveIfErr(iTraceServer.Connect(), _L("Couldn't connect to traceserver.exe"));

	if (iOperation == EStatus)
		{
		TInt tracing = iTraceServer.IsTracing();
		LeaveIfErr(tracing, _L("Couldn't get trace status"));
		Write(_L("Status: "));
		if (tracing)
			{
			Printf(_L("Tracing enabled\r\n"));
			}
		else
			{
			Printf(_L("Not tracing\r\n"));
			}
		TInt dest = iTraceServer.GetTraceDestination();
		LeaveIfErr(dest, _L("Couldn't get trace destination"));
		Write(_L("Destination: "));
		switch (dest)
			{
			case RTraceServer::EUsb:
				Write(_L("USB\r\n"));
				break;
			default:
				Printf(_L("Unknown (0x%x)\r\n"), dest);
				break;
			}
		}
	else if (iOperation == EStart)
		{
		LeaveIfErr(iTraceServer.StartTracing(), _L("Couldn't start tracing"));
		}
	else if (iOperation == EStop)
		{
		LeaveIfErr(iTraceServer.StopTracing(), _L("Couldn't stop tracing"));
		}
	}
