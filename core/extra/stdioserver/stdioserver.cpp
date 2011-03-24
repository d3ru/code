// stdioserver.cpp
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

#include "stdioserver.h"
#include <fshell/ltkutils.h>

CStdioServer::CStdioServer()
	: CServer2(CActive::EPriorityStandard, ESharableSessions) 
	{
	}

void CStdioServer::ConstructL()
	{
	User::LeaveIfError(iIoSession.Connect());
	StartL(_L("stdioserver"));
	}

CStdioServer::~CStdioServer()
	{
	iIoSession.Close();
	}

CSession2* CStdioServer::NewSessionL(const TVersion& /*aVersion*/, const RMessage2& aMessage) const
	{
	RThread client;
	aMessage.ClientL(client);
	CleanupClosePushL(client);

	CStdioSession* session = new(ELeave) CStdioSession();
	CleanupStack::PushL(session);
	session->ConstructL(const_cast<RIoSession&>(iIoSession), client);
	CleanupStack::Pop(session);
	CleanupStack::PopAndDestroy(&client);
	return session;
	}

//

CStdioSession::CStdioSession()
	{
	}

void CStdioSession::ConstructL(RIoSession& aIoSession, RThread& aClient)
	{
	TInt err = iHandles.OpenExisting(aIoSession, aClient.Id(), ETrue);
	if (err)
		{
		TFullName name;
		LtkUtils::GetFriendlyThreadName(aClient, name);
		err = iHandles.Create(aIoSession, iConsole, name);
		}
	User::LeaveIfError(err);
	}

CStdioSession::~CStdioSession()
	{
	iBuf.Close();
	iHandles.Close();
	iConsole.Close();
	}

void CStdioSession::ServiceL(const RMessage2& aMessage)
	{
	switch (aMessage.Function())
		{
	case EWrite:
		{
		iBuf.Zero();
		iBuf.ReAllocL(aMessage.Int1());
		aMessage.ReadL(0, iBuf);
		iBuf.ReAllocL(iBuf.Length() * 2);
		TPtr wbuf = iBuf.Expand();
		iHandles.Stdout().WriteL(wbuf);
		aMessage.Complete(wbuf.Length());
		return;
		}
	case ERead:
		//TODO?
		break;
	case ENotifyActivity:
		iNotifyMsg = aMessage;
		return;
	case ECancelNotify:
		if (!iNotifyMsg.IsNull()) iNotifyMsg.Complete(KErrCancel);
		break;
	default:
		break;
		}
	aMessage.Complete(KErrNone);
	}

//

void MainL()
	{
	CActiveScheduler* scheduler = new(ELeave) CActiveScheduler;
	CleanupStack::PushL(scheduler);
	CActiveScheduler::Install(scheduler);
	CStdioServer* server = new(ELeave) CStdioServer;
	CleanupStack::PushL(server);
	server->ConstructL();
	RProcess::Rendezvous(KErrNone);
	CActiveScheduler::Start();

	CleanupStack::PopAndDestroy(2, scheduler);
	}

GLDEF_C TInt E32Main()
	{
	__UHEAP_MARK;
	TInt err = KErrNoMemory;
	CTrapCleanup* cleanup = CTrapCleanup::New();
	if (cleanup)
		{
		TRAP(err, MainL());
		delete cleanup;
		}
	__UHEAP_MARKEND;
	return err;
	}

