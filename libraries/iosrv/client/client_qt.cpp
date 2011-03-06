// client_qt.cpp
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

#include <e32cons.h>
#include <fshell/iocli.h>
#include <fshell/ioutils.h>
#include <fshell/iocli_qt.h>

NONSHARABLE_CLASS(CMessageHandler) : public CBase
	{
public:
	~CMessageHandler();
	void ConstructL();

	void Msg(QtMsgType aType, const char* aMessage);

private:
	// It might seem more sensible to use RIoSession etc directly, but there's a lot of logic in CIoConsole for glueing non-fshell-aware things together which is desirable to have
	CConsoleBase* iConsole;
	RBuf iBuf;
	};

void MsgHandler(QtMsgType aType, const char* aMessage)
	{
	CMessageHandler* handler = (CMessageHandler*)Dll::Tls();
	if (handler) handler->Msg(aType, aMessage);
	}

EXPORT_C QtMsgHandler IoUtils::GetIosrvDebugHandler()
	{
	if (Dll::Tls()) return NULL;

	CMessageHandler* handler = new CMessageHandler;
	if (!handler) return NULL;
	TRAPD(err, handler->ConstructL());
	if (!err) err = Dll::SetTls(handler);
	
	if (err) 
		{
		delete handler;
		return NULL;
		}

	return MsgHandler;
	}

CMessageHandler::~CMessageHandler()
	{
	iBuf.Close();
	delete iConsole;
	}

void CMessageHandler::ConstructL()
	{
	iConsole = IoUtils::NewConsole();
	TName procName = RProcess().Name();
	User::LeaveIfError(iConsole->Create(procName, TSize(KConsFullScreen,KConsFullScreen)));
	}

void CMessageHandler::Msg(QtMsgType aType, const char* aMessage)
	{
	TPtrC8 ptr((const TUint8*)aMessage);
	iBuf.Zero();
	iBuf.ReAlloc(ptr.Length()); // If it fails, go ahead and try and write as much as we can fit in its current length
	iBuf.Copy(ptr.Left(iBuf.MaxLength()));
	_LIT(KCrLf, "\r\n");

	switch (aType)
		{
	case QtDebugMsg:
		iConsole->Write(iBuf);
		iConsole->Write(KCrLf);
		break;
	default:
		// Everything else needs to go to stderr. We know iocons supports this extension so no need to check
		ConsoleStdErr::Write(iConsole, iBuf);
		ConsoleStdErr::Write(iConsole, KCrLf);
		break;
		}
	}
