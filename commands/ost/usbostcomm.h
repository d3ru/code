// usbostcomm.h
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
// Reverse-engineered using CProxyServer

#ifndef USBOSTCOMM_H
#define USBOSTCOMM_H

class RUsbOstComm : public RSessionBase
	{
	enum TCmd
		{
		EConnect = 1,
		EDisconnect = 2,
		EOpen = 5,
		EClose = 6,
		ERegisterId = 7,
		ERead = 11,
		EReadCancel = 12,
		EWrite = 13,
		};

public:

	TInt Connect()
		{
		_LIT(KServerName, "!UsbOstRouter");
		_LIT(KServerProcName, "usbostrouter");
		TInt startupAttempts = 2;
		for(;;)
			{
			TInt err = CreateSession(KServerName, TVersion(1,0,0));
			if (err != KErrNotFound && err != KErrServerTerminated)
				{
				return err;
				}

			if (startupAttempts-- == 0)
				{
				return err;
				}

			RProcess server;
			err = server.Create(KServerProcName, KNullDesC);
			if (err) return err;

			TRequestStatus stat;
			server.Rendezvous(stat);

			if (stat != KRequestPending)
				{
				server.Kill(KErrNone);
				}
			else
				{
				server.Resume();
				}
			User::WaitForRequest(stat);
			err = (server.ExitType() == EExitPanic) ? KErrGeneral : stat.Int();
			server.Close();

			if (err && err != KErrAlreadyExists)
				{
				return err;
				}
			}
		}

	TInt Disconnect()
		{
		return SendReceive(EDisconnect);
		}

	TInt Open()
		{
		return SendReceive(EOpen);
		}

	void Close()
		{
		// Grr doesn't just use base class close, that'd be far too easy
		if (Handle())
			{
			SendReceive(EClose);
			}
		RHandleBase::Close();
		}

	TInt RegisterProtocolID(TInt aId, TBool aNeedHeader)
		{
		return SendReceive(ERegisterId, TIpcArgs(aId, aNeedHeader));
		}

	void ReadMessage(TRequestStatus& aStatus, TDes8& aDes)
		{
		SendReceive(ERead, TIpcArgs(aDes.MaxLength(), &aDes), aStatus);
		}

	void ReadCancel()
		{
		SendReceive(EReadCancel);
		}
	
	void WriteMessage(TRequestStatus& aStatus, const TDesC8& aDes)
		{
		return SendReceive(EWrite, TIpcArgs(EFalse, aDes.Length(), &aDes), aStatus);
		}
	};

#endif
