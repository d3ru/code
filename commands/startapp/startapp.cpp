// startapp.cpp
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

#include <fshell/ioutils.h>
#include <fshell/common.mmh>
#include <apgcli.h>
#include <apacmdln.h>
#include <fshell/loggingallocator.h> //TOMSCI DEBUG

using namespace IoUtils;

class CCmdStartApp : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdStartApp();
private:
	CCmdStartApp();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	HBufC* iAppName;
	TFileName2 iDocument;
	TBool iBackground;
	TInt iScreen;
	//TBool iWait;
	};

EXE_BOILER_PLATE(CCmdStartApp)

CCommandBase* CCmdStartApp::NewLC()
	{
	CCmdStartApp* self = new(ELeave) CCmdStartApp();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdStartApp::~CCmdStartApp()
	{
	//TOMSCI TEMP FOR TESTING	delete iAppName;
	}

CCmdStartApp::CCmdStartApp()
	{
	RLoggingAllocator::Install_WeakLink();
	}

const TDesC& CCmdStartApp::Name() const
	{
	_LIT(KName, "startapp");	
	return KName;
	}

void CCmdStartApp::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iAppName, _L("appname"));
	}

void CCmdStartApp::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendBoolL(iBackground, _L("background"));
	aOptions.AppendFileNameL(iDocument, _L("document"));
	aOptions.AppendIntL(iScreen, _L("screen"));
	//aOptions.AppendBoolL(iWait, _L("wait"));
	}

void CCmdStartApp::DoRunL()
	{
	RApaLsSession lsSession;
	LeaveIfErr(lsSession.Connect(), _L("Couldn't connect to AppArc"));
	CleanupClosePushL(lsSession);

	TThreadId threadId;
	CApaCommandLine* cl = CApaCommandLine::NewLC();
	cl->SetExecutableNameL(*iAppName);
	if (iBackground)
		{
		cl->SetCommandL(EApaCommandBackground);
		}
	if (iDocument.Length())
		{
		cl->SetDocumentNameL(iDocument);
		}
	if (iScreen)
		{
		cl->SetDefaultScreenL(iScreen);
		}
	TRequestStatus stat;
	LeaveIfErr(lsSession.StartApp(*cl, threadId, &stat), _L("Couldn't start app %S"), iAppName);

	User::WaitForRequest(stat);
	LeaveIfErr(stat.Int(), _L("Failed to rendezvous with application %S"), iAppName);

	CleanupStack::PopAndDestroy(cl);
	CleanupStack::PopAndDestroy(&lsSession);
	}
