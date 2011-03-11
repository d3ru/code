// stdioserver.h
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

#ifndef FSHELL_STDIOSERVER_H
#define FSHELL_STDIOSERVER_H

#include <e32base.h>
#include <fshell/iocli.h>
#include <fshell/common.mmh>
#include <fshell/descriptorutils.h>

using namespace LtkUtils;

class CStdioServer : public CServer2
	{
public:
	CStdioServer();
	void ConstructL();
	~CStdioServer();
	//void SessionClosed(CStdioSession* aSession);

protected:
	CSession2* NewSessionL(const TVersion& aVersion, const RMessage2& aMessage) const;

private:
	RIoSession iIoSession;
	//RArray<CStdioSession*> iSessions;
	//TInt iNextSessionId;
	};

class CStdioSession : public CSession2
	{
public:
	CStdioSession();
	void ConstructL(RIoSession& aIoSession, RThread& aClient);
	~CStdioSession();

protected:
	void ServiceL(const RMessage2 &aMessage);

private:
	TIoHandleSet iHandles;
	RIoConsole iConsole;
	RLtkBuf8 iBuf;
	RMessagePtr2 iReadMsg;
	RMessagePtr2 iNotifyMsg; // We don't support notify but in order to stall rather than spin we at least accept the message
	};

enum TFn
	{
	ERead,
	EWrite,
	EFlush,
	ECheckMedia,
	ENotifyActivity,
	ECancelNotify,
	EEcho,
	};

#endif
