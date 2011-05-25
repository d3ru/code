// traceserver.h
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
// Reverse-engineered using proxyserver

#ifndef TRACESERVER_H
#define TRACESERVER_H

class RTraceServer : public RSessionBase
	{
	enum TCmd
		{
		EGetTraceDestination = 2,
		EStartTracing = 4,
		EStopTracing = 5,
		EStatus = 6,
		ENotifyStatusChanged = 14,
		};

public:

	enum TTraceDestination
		{
		EUsb = 0,
		};

	TInt Connect()
		{
		_LIT(KServerName, "!TraceServer");
		_LIT(KServerProcName, "traceserver");
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

	TInt IsTracing() const
		{
		TBool isTracing = EFalse;
		TPckg<TBool> pkg(isTracing);
		TInt err = SendReceive(EStatus, TIpcArgs(&pkg));
		return err ? err : isTracing;
		}

	TInt GetTraceDestination() const
		{
		TInt dest = EUsb;
		TPckg<TInt> pkg(dest);
		TInt err = SendReceive(EGetTraceDestination, TIpcArgs(&pkg));
		return err ? err : dest;
		}

	TInt StartTracing()
		{
		return SendReceive(EStartTracing);
		}

	TInt StopTracing()
		{
		return SendReceive(EStopTracing);
		}
	};

#endif
