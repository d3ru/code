// muxserver.cpp
// 
// Copyright (c) 2010 - 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include <e32property.h>
#include <fshell/ltkutils.h>
#include <fshell/descriptorutils.h>
#include "muxserver.h"

//#define MUXCONS_LOGGING

#ifdef MUXCONS_LOGGING
#include <e32debug.h>
#define LOG(args...) RDebug::Print(args)
#else
#define LOG(args...)
#endif

_LIT(KDefaultMuxServ, "MuxConsSrv");
_LIT(KMuxServFmt, "MuxConsSrv-%d");
const TMuxVersion KCurrentVersion = EVersion1_0;

EXE_BOILER_PLATE(CCmdMuxserver)

TUint32 ByteSwap(TUint32 aVal)
	{
	return 0
		| ((aVal & 0x000000ff) << 24)
		| ((aVal & 0x0000ff00) << 8)
		| ((aVal & 0x00ff0000) >> 8)
		| ((aVal & 0xff000000) >> 24);
	}

TUint32 FromBigEndian(TUint32 aVal)
	{
#ifdef __BIG_ENDIAN__ // I know Symbian doesn't actually define the endian macros (afaics) but if they do, we'll do the right thing(TM)
	return aVal;
#else
	// Assume little-endian unless specifically told otherwise
	return ByteSwap(aVal);
#endif
	}

TInt32 FromBigEndian(TInt32 aVal)
	{
	return (TInt32)FromBigEndian((TUint32)aVal);
	}

TUint32 ToBigEndian(TUint32 aVal)
	{
#ifdef __BIG_ENDIAN__ // I know Symbian doesn't actually define the endian macros (afaics) but if they do, we'll do the right thing(TM)
	return aVal;
#else
	// Assume little-endian unless specifically told otherwise
	return ByteSwap(aVal);
#endif
	}

TInt32 ToBigEndian(TInt32 aVal)
	{
	return (TInt32)ToBigEndian((TUint32)aVal);
	}

TInt GetInt(const TAny* aPtr)
	{
	TInt result;
	memcpy(&result, aPtr, 4);
	return result;
	}

TUint16 GetUint16(const TAny* aPtr)
	{
	TUint16 result;
	memcpy(&result, aPtr, 2);
	return result;
	}

CCommandBase* CCmdMuxserver::NewLC()
	{
	CCmdMuxserver* self = new(ELeave) CCmdMuxserver();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdMuxserver::~CCmdMuxserver()
	{
	iDeleting = ETrue;
	Cancel();
	iOutputCollectors.ResetAndDestroy();
	delete iMuxServer;
	iPutFile.Close();
	if (iOstServer.Handle())
		{
		// RUsbOstComm::Close isn't safe to call when not open, sigh
		iOstServer.Close();
		}
	}

CCmdMuxserver::CCmdMuxserver()
	: CCommandBase(EManualComplete), iInputBufFreeSpace((TUint8*)iInputBuf.Ptr(), 0, iInputBuf.MaxLength())
	{
	}

const TDesC& CCmdMuxserver::Name() const
	{
	_LIT(KName, "muxserver");
	return KName;
	}

void CCmdMuxserver::ArgumentsL(RCommandArgumentList& /*aArguments*/)
	{
	}

void CCmdMuxserver::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendBoolL(iPing, _L("ping"));
	aOptions.AppendBoolL(iUseOst, _L("ost"));
	aOptions.AppendIntL(iNestLevel, _L("nest"));
	}

void CCmdMuxserver::DoRunL()
	{
	//__DEBUGGER();

	if (iUseOst)
		{
		iPing = ETrue;

		// Check there isn't already an OST muxserver running
		TInt ostServerPid = 0;
		TInt err = RProperty::Get(RProcess().SecureId(), KOstPubSubKey, ostServerPid);
		TBool ostRunning = EFalse;
		if (!err && ostServerPid)
			{
			RProcess p;
			err = p.Open(ostServerPid);
			if (!err && p.ExitType() == EExitPending) ostRunning = ETrue;
			p.Close();
			}
		if (ostRunning)
			{
			// If OST is already running we exit quietly, on the basis that we're only ever launched like this in a situation where it's not an error so long as *something* is listening on OST
			Complete();
			return;
			}
		RProperty::Define(RProcess().SecureId(), KOstPubSubKey, RProperty::EInt);
		LeaveIfErr(RProperty::Set(RProcess().SecureId(), KOstPubSubKey, (TInt)RProcess().Id()), _L("Couldn't set KOstPubSubKey"));

		LeaveIfErr(iOstServer.Connect(), _L("Couldn't connect to ustostrouter"));
		LeaveIfErr(iOstServer.Open(), _L("Couldn't open RUsbOstComm"));
		LeaveIfErr(iOstServer.RegisterProtocolID(KFshellOstProtocolId, EFalse), _L("Failed to register protocol id 0x%x"), KFshellOstProtocolId);
		}

	iMuxServer = new(ELeave) CMuxServer(*this);
	iServerName.Copy(KDefaultMuxServ);
	TRAPD(err, iMuxServer->ConstructL(iServerName));
	if (err == KErrAlreadyExists)
		{
		// Retry with unique name
		iServerId = TUint(RProcess().Id());
		iServerName.Format(KMuxServFmt, iServerId);
		TRAP(err, iMuxServer->ConstructL(iServerName));
		}
	LeaveIfErr(err, _L("Couldn't start mux server %S"), &iServerName);

	if (!iPing)
		{
		Printf(_L("Muxserver v%d.%d starting... (If this wasn't intentional hit CTRL-C four times to exit!)\r\n"), KCurrentVersion >> 16, KCurrentVersion & 0xFFFF);
		}

	if (iUseOst)
		{
		SetErrorReported(ETrue); // Any errors after we start the mux server (eg on disconnection) risk ending back with muxserver itself, because in OST-land we don't have an underlying console
		iOstServer.ReadMessage(iStatus, iInputBufFreeSpace);
		}
	else
		{
		User::LeaveIfError(Stdin().CaptureAllKeys());			// To prevent other things (like fshell) interpreting binary data as special key presses (like ctrl-C).
		LeaveIfErr(Stdin().SetMode(RIoReadWriteHandle::EBinary), _L("Unable to set stdin to binary mode"));	// To prevent vt100cons from scanning for ANSI escape sequences. 
		LeaveIfErr(Stdout().SetMode(RIoReadWriteHandle::EBinary), _L("Unable to set stdout to binary mode")); // To tell iosrv to not mess about with line endings.
		Stdin().SetReadMode(RIoReadHandle::EOneOrMore);
		Stdin().Read(iInputBufFreeSpace, iStatus);
		}
	SetActive();

	if (iPing)
		{
		SendPing();
		}
	}

void CCmdMuxserver::DoCancel()
	{
	if (iUseOst)
		{
		iOstServer.ReadCancel();
		}
	else
		{
		Stdin().ReadCancel();
		}
	}

void CCmdMuxserver::RunL()
	{
	if (iStatus.Int() != KErrNone)
		{
		LOG(_L("[%d] Error %d returned from muxserver read"), iNestLevel, iStatus.Int());
		SetErrorReported(ETrue); // So the complete doesn't try talking to the port - might end up forcing instanciation of a defcons (if OST is being used) that would lead to a muxcons leading to a deadlock...
		Complete(iStatus.Int());
		return;
		}

	iInputBuf.SetLength(iInputBuf.Length() + iInputBufFreeSpace.Length());

	//LOG(_L("[%d] Muxcons: read %d bytes"), iNestLevel, iInputBufFreeSpace.Length());
	//LtkUtils::HexDumpToRDebug(iInputBuf);

	const TUint8* ptr = iInputBuf.Ptr();
	const TUint8* end = ptr + iInputBuf.Size();
	TBool shouldIssueNextRead = ETrue;

	while (ptr < end)
		{
		// Need the loop here in case we get multiple packets in a single data buffer
		int remainder = end - ptr;
		if (remainder > 4)
			{
			// We have enough data for a header - see what we've got
			TUint32 packetHeader;
			memcpy(&packetHeader, ptr, 4);
			packetHeader = FromBigEndian(packetHeader);
			TBool headerOk = ((packetHeader & KPacketHeaderMask) == KPacketHeader);
			TInt packetLen = packetHeader & KPacketLengthMask;

			if (!iUseOst && packetHeader == 0x03030303)
				{
				// Magic sequence of 4 CTRL-C characters
				Stdout().SetMode(RIoReadWriteHandle::EText); // If we don't set these back before printing, iosrv's cursor tracking will be off and the fshell prompt will be drawn in the wrong place
				Stdin().SetMode(RIoReadWriteHandle::EText);
				Printf(_L("Exiting muxserver!\r\n\r\n"));
				SetErrorReported(ETrue); // So the cancel isn't reported
				Complete(KErrCancel);
				return;
				}
			else if (!headerOk || packetLen < 8 || packetLen > KMaxPacketLen)
				{
				// Then this wasn't a valid packet
				LOG(_L("Invalid packet header 0x%08x, skipping"), packetHeader);
				iInputBuf.Delete(0, 1);
				end = ptr + iInputBuf.Size();
				continue;
				}

			if (remainder >= packetLen)
				{
				// Got all of it - decode it
				TInt32 channelOrCmd;
				memcpy(&channelOrCmd, ptr + 4, 4);
				channelOrCmd = FromBigEndian(channelOrCmd);
				TInt nestLevel = ptr[2] >> 4;

				const TUint8* payloadStart = ptr + 8;
				TInt payloadLen = packetLen - 8;
				if (iNestLevel == 0 && channelOrCmd == ELaunchNestedMuxcons)
					{
					// We are the ones managing nest ids, so we have to snoop this command and handle it ourselves
					HandleCommandLaunchNestedMuxcons(nestLevel);
					}
				else if (nestLevel != 0 && iNestLevel == 0)
					{
					// The packet is destined for someone else and we are the root muxserver
					TInt channel = COMMAND_ID_FROM_NEST_ID(nestLevel);
					CMuxSession* session = iMuxServer->SessionForId(channel);
					if (session)
						{
						shouldIssueNextRead = session->SendNestedPacket(TPtrC8(ptr, packetLen));
						if (!shouldIssueNextRead)
							{
							iReadBlockedOnSession = session;
							LOG(_L("Nested session id %d couldn't accept the packet - flowing off"), nestLevel);
							break; // We have to stop parsing here, in this case
							}
						}
					else
						{
						LOG(_L("Nested session %d not found - already dead?"), channel);
						}
					}
				else if (channelOrCmd < 0)
					{
					TPtrC8 payload(payloadStart, payloadLen);
					LOG(_L("Received muxcons[%d] command %d payloadLen %d"), iNestLevel, channelOrCmd, payload.Length());
					HandleCommand(channelOrCmd, payload);
					}
				else
					{
					// Channel
					TConsoleCommandOpCode opcode;
					memcpy(&opcode, payloadStart, 4);
					TPtrC8 payload(payloadStart + 4, payloadLen - 4);
					LOG(_L("Received message for channel %d opcode %d payloadLen %d"), channelOrCmd, opcode, payload.Length());
					CMuxSession* session = iMuxServer->SessionForId(channelOrCmd);
					if (session)
						{
						session->HandleMessage(opcode, payload);
						}
					else
						{
						LOG(_L("Session not found - already dead?"));
						}
					}
				ptr += packetLen;
				continue;
				}
			}

		// Else the remainder of the buf isn't a whole packet - buffer it if we haven't already, and wait for the rest
		break;
		}

	// OST is packet based so iInputBufFreeSpace isn't strictly necessary but it makes the code simpler for both mechanisms to use the same buffers
	if (ptr == end) iInputBuf.Zero();
	else iInputBuf.Delete(0, ptr - iInputBuf.Ptr());
	iInputBufFreeSpace.Set((TUint8*)iInputBuf.Ptr() + iInputBuf.Length(), 0, iInputBuf.MaxLength() - iInputBuf.Length());

	if (!shouldIssueNextRead) return;

	if (iUseOst)
		{
		iOstServer.ReadMessage(iStatus, iInputBufFreeSpace);
		}
	else
		{
		Stdin().Read(iInputBufFreeSpace, iStatus);
		}
	SetActive();
	}

void CCmdMuxserver::SendPing()
	{
	StartOutput(EPing);
	AppendOutput(KCurrentVersion);
	AppendOutput(iServerId);
	SendOutput();
	}

void CCmdMuxserver::StartOutput(TInt aChannelId, TConsoleCommandOpCode aOpcode)
	{
	LOG(_L("Mux[%d] StartOutput channel=%d opcode=%d"), iNestLevel, aChannelId, aOpcode);
	iOutputBuf.Zero();
	iOutputBuf.SetLength(4); // Leave space for len
	((TUint8*)iOutputBuf.Ptr())[2] = iNestLevel << 4; // Set nest level
	AppendOutput(ToBigEndian((TInt32)aChannelId));
	AppendOutput(aOpcode);
	}

void CCmdMuxserver::StartOutput(TMuxConsCommand aCmd)
	{
	LOG(_L("Mux[%d] StartOutput cmd=%d"), iNestLevel, aCmd);
	iOutputBuf.Zero();
	iOutputBuf.SetLength(4); // Leave space for len
	((TUint8*)iOutputBuf.Ptr())[2] = iNestLevel << 4; // Set nest level
	AppendOutput(ToBigEndian((TInt32)aCmd));
	}

void CCmdMuxserver::AppendOutput(TInt32 aInt)
	{
	iOutputBuf.Append(TPckg<TInt32>(aInt));
	}

void CCmdMuxserver::AppendOutput(const TDesC8& aData)
	{
	iOutputBuf.Append(aData);
	}

void CCmdMuxserver::SendOutput(TInt aNestChannelToFakeOwnerOf)
	{
	ASSERT(iOutputBuf.Length() <= 0xFFF); // Otherwise we'll overlap with the bits we use for the nest level
	TInt nestLevel = ((TUint8*)iOutputBuf.Ptr())[2] >> 4;
	if (aNestChannelToFakeOwnerOf != 0)
		{
		// For when we need to send a ENewConsoleCreated packet on behalf of someone else
		nestLevel = iNestLevelsActualOwners[NEST_ID_FROM_COMMAND_ID(aNestChannelToFakeOwnerOf)];
		}
	TUint32 packetHeader = ToBigEndian((TUint32)(KPacketHeader | iOutputBuf.Length() | (nestLevel << 12)));
	memcpy((void*)iOutputBuf.Ptr(), &packetHeader, 4);
	//if (FromBigEndian(*(TInt32*)(iOutputBuf.Ptr() + 4)) == EPing)
	//	{
	//	LOG(_L("Pingleping"));
	//	}
	LOG(_L("Muxcons[%d] writing packet id %d of len %d for nest %d"), iNestLevel, FromBigEndian(*(TInt32*)(iOutputBuf.Ptr() + 4)), iOutputBuf.Length(), nestLevel);
	if (iUseOst)
		{
		TRequestStatus stat;
		iOstServer.WriteMessage(stat, iOutputBuf);
		User::WaitForRequest(stat);
		}
	else
		{
		Stdout().Write(iOutputBuf);
		}
	}

void CCmdMuxserver::WriteDataL(TInt aChannelId, const RMessage2& aMessage)
	{
	TInt payloadLen = aMessage.GetDesLengthL(0);
	TInt done = 0;

	TBool wide = EFalse;
	TPtr wideBuf(NULL, 0, 0);
	if (aMessage.Function() == EWriteData) // as opposed to EWriteData8
		{
		wide = ETrue;
		wideBuf.Set((TUint16*)iTempWriteBuf.Ptr(), 0, iTempWriteBuf.MaxLength() / 2);
		}

	if (IS_NEST_CHANNEL(aChannelId))
		{
		// Go straight to output, don't re-encapsulate, as the data is already a valid mux packet
		ASSERT(!wide);
		ASSERT(payloadLen <= KMaxPacketLen);
		aMessage.ReadL(0, iOutputBuf);
		SendOutput();
		return;
		}

	while (done < payloadLen)
		{
		if (wide)
			{
			aMessage.ReadL(0, wideBuf, done);
			done += wideBuf.Length();
			}
		else
			{
			aMessage.ReadL(0, iTempWriteBuf, done);
			done += iTempWriteBuf.Length();
			}

		LOG(_L("Mux[%d] WriteDataL for channel %d done %d bytes"), iNestLevel, aChannelId, done);

		if (aChannelId < 0)
			{
			// A TMuxConsCommand result - send appropriate pending status
			StartOutput((TMuxConsCommand)aChannelId);
			AppendOutput(KRequestPending);
			}
		else
			{
			StartOutput(aChannelId, (TConsoleCommandOpCode)aMessage.Function());
			}

		if (wide)
			{
			iOutputBuf.Append(TPtrC8((TUint8*)wideBuf.Ptr(), wideBuf.Size()));
			}
		else
			{
			iOutputBuf.Append(iTempWriteBuf);
			}
		SendOutput();
		}
	}

void CCmdMuxserver::HandleCommand(TInt aCmd, const TDesC8& aPayload)
	{
	switch (aCmd)
		{
	case ECreateNewConsole:
		{
		RProcess proc;
		TBuf<120> args;
		args.Format(_L("--console muxcons --console-title servername=%S --console-flags 1"), &iServerName);
		TInt err = proc.Create(_L("fshell.exe"), args);
		if (!err) proc.Resume();
		proc.Close();
		break;
		}
	case EListConsoles:
		{
		if (aPayload.Length() < 4)
			{
			StartOutput(EListConsoles);
			AppendOutput(KErrCorrupt);
			SendOutput();
			return;
			}

		TInt n = iMuxServer->CountSessions();
		TBool createIfNecessary = GetInt(aPayload.Ptr());
		if (n == 0 && createIfNecessary)
			{
			iListOnNextCreate = ETrue;
			HandleCommand(ECreateNewConsole, KNullDesC8);
			}
		else
			{
			ListConsolesAndTitles();
			}
		break;
		}
	case EConsoleShouldClose:
		{
		TInt id = GetInt(aPayload.Ptr());
		CMuxSession* session = iMuxServer->SessionForId(id);
		if (session != NULL)
			{
			session->Died(); // Don't think we're allowed to actually destroy the session from the server side
			}
		break;
		}
	case EShutdown:
		{
		LOG(_L("Shutting down muxserver"));
		StartOutput(EShutdown);
		SendOutput();
		Complete(KErrNone);
		break;
		}
	case EPutFile:
		{
		memcpy(&iPutFileId, aPayload.Ptr(), 4);
		TUint16 fileNameLen;
		memcpy(&fileNameLen, aPayload.Ptr() + 4, 2);

		// Copy filename into temp buf (steal iOutputBuf briefly) in case the packet isn't aligned
		memcpy((void*)iOutputBuf.Ptr(), aPayload.Ptr() + 6, fileNameLen*2);
		TPtrC16 fileName((TUint16*)(iOutputBuf.Ptr()), fileNameLen);
		TPtrC8 remainingData(aPayload.Mid(6 + fileNameLen*2));
		TInt err = KErrNone;
		if (iPutFile.SubSessionHandle() == 0)
			{
			FsL().MkDirAll(fileName);
			TUint fileMode = EFileWrite;
#if FSHELL_PLATFORM_S60 >= 5 || defined(FSHELL_PLATFORM_FOUNDATION)
			fileMode |= EFileWriteBuffered;
#endif
			err = iPutFile.Replace(FsL(), fileName, fileMode);
			}
		else
			{
			err = KErrNotReady;
			}

		if (!err)
			{
			err = iPutFile.Write(remainingData);
			}

		if (err)
			{
			iPutFile.Close();
			LOG(_L("Error %d writing file %S"), err, &fileName);
			StartOutput(EPutFile);
			AppendOutput(err);
			AppendOutput(iPutFileId);
			SendOutput();
			}
		break;
		}
	case EContinuePutFile:
		{
		if (aPayload.Length() == 0)
			{
			// Indicates we're finished
			iPutFile.Close();
			StartOutput(EPutFile);
			AppendOutput(KErrNone);
			AppendOutput(iPutFileId);
			SendOutput();
			break;
			}
			
		TInt err;
		if (iPutFile.SubSessionHandle() != 0)
			{
			err = iPutFile.Write(aPayload);
			}
		else
			{
			err = KErrNotReady;
			}

		if (err)
			{
			iPutFile.Close();
			LOG(_L("Error %d writing to putfile"), err);
			StartOutput(EPutFile);
			AppendOutput(err);
			AppendOutput(iPutFileId);
			SendOutput();
			}
		break;
		}
	case EPing:
		{
		// Just ping right back
		SendPing();
		break;
		}
	default:
		if (aCmd == ELaunchProcess || aCmd <= ECustomCommandBase)
			{
			TUint16 procLen = GetUint16(aPayload.Ptr());
			TInt argsLen = (aPayload.Length()/2) - 1 - procLen;
			// Copy into output buf temporarily, to guarantee alignment.
			memcpy((void*)iOutputBuf.Ptr(), aPayload.Ptr() + 2, aPayload.Length()-2);
			TPtrC16 procName((TUint16*)(iOutputBuf.Ptr()), procLen);
			TPtrC16 args(((TUint16*)iOutputBuf.Ptr()) + procLen, argsLen);

			if (aCmd == ELaunchProcess)
				{
				// Straight-forward RProcess::Create //and rendezvous
				RProcess proc;
				TInt err = proc.Create(procName, args);
				if (err == KErrNone)
					{
					/*TRequestStatus stat;
					proc.Rendezvous(stat);
					if (stat != KRequestPending)
						{
						proc.Kill(stat.Int());
						}
					else
						{
						proc.Resume();
						}
					User::WaitForRequest(stat);
					err = stat.Int();
					*/
					proc.Resume();
					proc.Close();
					}
				else
					{
					StartOutput(ELaunchProcess);
					AppendOutput(err);
					SendOutput();
					}
				}
			else
				{
				// We need to collect the output and send it back
				StartProcessForCommandId((TMuxConsCommand)aCmd, procName, args);
				}
			}
		else
			{
			LOG(_L("Unrecognised command %d received by muxserver"), aCmd);
			StartOutput((TMuxConsCommand)aCmd);
			AppendOutput(KErrNotSupported);
			SendOutput();
			}
		break;
		}
	}

void CCmdMuxserver::HandleCommandLaunchNestedMuxcons(TInt aOnBehalfOfNestLevel)
	{
	// First find a free nest level
	TInt nestLevel = 0;
	for (TInt i = 0; i < 16; i++)
		{
		if ((iNestLevelsInUse & (1 << i)) == 0)
			{
			iNestLevelsInUse |= (1 << i);
			nestLevel = i+1;
			break;
			}
		}

	if (nestLevel == 0)
		{
		LOG(_L("Run out of nest levels!"));
		StartOutput(ELaunchNestedMuxcons);
		AppendOutput(KErrOverflow);
		SendOutput(COMMAND_ID_FROM_NEST_ID(aOnBehalfOfNestLevel));
		return;
		}
	
	iNestLevelsActualOwners[nestLevel] = aOnBehalfOfNestLevel;
	RProcess proc;
	TBuf<100> args;
	args.Format(_L("--ping --nest %d --console muxcons --console-title channel=%d,servername=%S"), nestLevel, COMMAND_ID_FROM_NEST_ID(nestLevel), &iServerName);
	LOG(_L("Muxserver[%d] launching muxserver.exe %S"), iNestLevel, &args);
	TInt err = proc.Create(_L("muxserver.exe"), args);
	if (err)
		{
		StartOutput(ELaunchNestedMuxcons);
		AppendOutput(err);
		SendOutput(COMMAND_ID_FROM_NEST_ID(aOnBehalfOfNestLevel));
		}
	else
		{
		proc.Resume();
		proc.Close();
		}
	}

void CCmdMuxserver::ListConsolesAndTitles()
	{
	iListOnNextCreate = EFalse;
	StartOutput(EListConsoles);
	TInt n = iMuxServer->CountSessions();
	AppendOutput(n);
	for (TInt i = 0; i < n; i++)
		{
		CMuxSession* session = iMuxServer->SessionAtIndex(i);
		AppendOutput(session->Id());
		}
	SendOutput();

	for (TInt i = 0; i < n; i++)
		{
		CMuxSession* session = iMuxServer->SessionAtIndex(i);
		StartOutput(session->Id(), ESetTitle);
		const TDesC& title = session->Title();
		AppendOutput(TPtrC8((TUint8*)title.Ptr(), title.Size()));
		SendOutput();
		}

	/*
	StartOutput(EGetServerInfo);
	AppendOutput(KCurrentVersion);
	TPckgBuf<TUint16> nameLen(iServerName.Length());
	AppendOutput(nameLen);
	AppendOutput(TPtrC8((TUint8*)iServerName.Ptr(), iServerName.Size()));
	SendOutput();
	*/
	}

void CCmdMuxserver::StartProcessForCommandId(TMuxConsCommand aCommandId, const TDesC& aProcessName, const TDesC& aProcessArgs)
	{
	TInt err = iOutputCollectors.Reserve(iOutputCollectors.Count()+1);
	CCommandOutputCollector* output = NULL;
	if (!err)
		{
		TRAP(err, output = CCommandOutputCollector::NewL(aProcessName, aProcessArgs, aCommandId, *this));
		}
	
	if (!err)
		{
		iOutputCollectors.Append(output);
		}

	if (err)
		{
		StartOutput(aCommandId);
		AppendOutput(err);
		SendOutput();
		}
	}

void CCmdMuxserver::SpecialCommandConnectedL(CMuxSession* aSession)
	{
	CCommandOutputCollector* found = NULL;
	for (TInt i = 0; i < iOutputCollectors.Count(); i++)
		{
		if (iOutputCollectors[i]->Id() == (TMuxConsCommand)aSession->Id())
			{
			found = iOutputCollectors[i];
			break;
			}
		}

	if (!found)
		{
		// Could happen if command died immediately after connecting console but before we service the connect message
		// Or the client is trying it on with an invalid console-title
		LOG(_L("No command collector found for session id %d"), aSession->Id());
		delete aSession;
		User::Leave(KErrNotReady);
		}

	found->SetSessionL(aSession);
	}

void CCmdMuxserver::CommandDied(CCommandOutputCollector* aCollector, TInt aCompletionCode)
	{
	LOG(_L("CommandDied for collector id %d err=%d"), aCollector->Id(), aCompletionCode);
	for (TInt i = 0; i < iOutputCollectors.Count(); i++)
		{
		if (iOutputCollectors[i] == aCollector)
			{
			StartOutput(aCollector->Id());
			AppendOutput(aCompletionCode);
			SendOutput();

			delete aCollector;
			iOutputCollectors.Remove(i);

			break;
			}
		}

	}

void CCmdMuxserver::SessionClosed(CMuxSession* aSession)
	{
	if (iDeleting) return;

	TInt id = aSession->Id();
	if (id >= 0)
		{
		// Don't send this for special ids
		StartOutput(id, EConsoleIsDead);
		SendOutput();
		}
	else if (IS_NEST_CHANNEL(id))
		{
		StartOutput(id, EConsoleIsDead);
		SendOutput(id);

		TInt nestId = NEST_ID_FROM_COMMAND_ID(id);
		iNestLevelsActualOwners[nestId] = 0;
		iNestLevelsInUse &= ~(1 << (nestId-1));
		}

	if (iMuxServer)
		{
		iMuxServer->SessionClosed(aSession);
		}

	if (aSession == iReadBlockedOnSession)
		{
		iReadBlockedOnSession = NULL;
		RunL(); // Re-run the event loop - bit messy, easier than refactoring run loop. Doesn't leave.
		}
	}

void CCmdMuxserver::NestedSessionReadyForData(CMuxSession* aSession)
	{
	if (aSession == iReadBlockedOnSession)
		{
		LOG(_L("Nested session %d now ready for more data, rerunning the fun"), NEST_ID_FROM_COMMAND_ID(aSession->Id()));
		ASSERT(!IsActive());
		iReadBlockedOnSession = NULL;
		RunL(); // Doesn't leave.
		}
	}

///

CMuxServer::CMuxServer(CCmdMuxserver& aCmd)
	: CServer2(CActive::EPriorityHigh), iCmd(aCmd) // Higher priority than CCmdMuxserver, so we stand a chance of offloading data faster than we process it
	{}

void CMuxServer::ConstructL(const TDesC& aServerName)
	{
	StartL(aServerName);
	}

CMuxServer::~CMuxServer()
	{
	iSessions.Close();
	}

CSession2* CMuxServer::NewSessionL(const TVersion& aVersion, const RMessage2& /*aMessage*/) const
	{
	return const_cast<CMuxServer*>(this)->DoNewSessionL(aVersion.iBuild); // Dumb const
	}

CSession2* CMuxServer::DoNewSessionL(TInt aSpecialCommandId)
	{
	iSessions.ReserveL(iSessions.Count() + 1);
	TInt commandId;
	if (aSpecialCommandId < 0)
		{
		commandId = aSpecialCommandId;
		}
	else
		{
		commandId = iNextSessionId++;
		}
	LOG(_L("CMuxServer[%d] creating session %d"), iCmd.NestLevel(), commandId);
	CMuxSession* s = new(ELeave) CMuxSession(iCmd, commandId);

	if (commandId <= ECustomCommandBase)
		{
		iCmd.SpecialCommandConnectedL(s);
		}
	else
		{
		// Normal session
		iSessions.Append(s);
		if (iCmd.ListOnNextCreate())
			{
			iCmd.ListConsolesAndTitles();
			}
		else
			{
			iCmd.StartOutput(s->Id(), ENewConsoleCreated);
			if (IS_NEST_CHANNEL(s->Id()))
				{
				// Need to pretend this message came from the real nest level owner, so that muxcons's snooping will work
				iCmd.SendOutput(s->Id());
				}
			else
				{
				iCmd.SendOutput();
				}
			}
		}
	return s;
	}

CMuxSession* CMuxServer::SessionForId(TInt aId)
	{
	const TInt n = iSessions.Count();
	for (TInt i = 0; i < n; i++)
		{
		if (iSessions[i]->Id() == aId)
			{
			return iSessions[i];
			}
		}
	return NULL;
	}

CMuxSession* CMuxServer::SessionAtIndex(TInt aIndex)
	{
	return iSessions[aIndex];
	}

TInt CMuxServer::CountSessions() const
	{
	return iSessions.Count();
	}

void CMuxServer::SessionClosed(CMuxSession* aSession)
	{
	const TInt n = iSessions.Count();
	for (TInt i = 0; i < n; i++)
		{
		if (iSessions[i] == aSession)
			{
			iSessions.Remove(i);
			break;
			}
		}
	}

CMuxSession::CMuxSession(CCmdMuxserver& aCmd, TInt aId)
	: iCmd(aCmd), iId(aId)
	{
	}

CMuxSession::~CMuxSession()
	{
	iCmd.SessionClosed(this);
	iTitle.Close();
	//iBinaryReadData.Close();
	}

void CMuxSession::ServiceL(const RMessage2& aMessage)
	{
	if (iDead) User::Leave(KErrDisconnected);

	TConsoleCommandOpCode fn = (TConsoleCommandOpCode)aMessage.Function();
	LOG(_L("Mux[%d] session %d servicing fn %d"), iCmd.NestLevel(), iId, fn);
	TBool async = EFalse;

	if (iId < 0 && fn != EWriteData && fn != EWriteData8 && fn != EReadData8)
		{
		LOG(_L("Custom command doing something other than reading/writing data??"));
		User::Leave(KErrNotReady);
		}

	switch (fn)
		{
	case EWriteData:
	case EWriteData8:
		iCmd.WriteDataL(iId, aMessage);
		break;
	case ECursorPosRequest:
	case EScreenSizeRequest:
		if (!iPendingSyncMessage.IsNull()) User::Leave(KErrNotReady);
		iCmd.StartOutput(iId, fn);
		iCmd.SendOutput();
		iPendingSyncMessage = aMessage;
		async = ETrue;
		break;
	case ESetAbsCursorPos:
	case ESetRelCursorPos:
		iCmd.StartOutput(iId, fn);
		iCmd.AppendOutput(aMessage.Int0());
		iCmd.AppendOutput(aMessage.Int1());
		iCmd.SendOutput();
		break;
	case ESetCursorHeight:
		iCmd.StartOutput(iId, ESetCursorHeight);
		iCmd.AppendOutput(aMessage.Int0());
		iCmd.SendOutput();
		break;
	case EClearScreen:
	case EClearToEndOfLine:
		iCmd.StartOutput(iId, fn);
		iCmd.SendOutput();
		break;
	case ESetTitle:
		iTitle.Zero();
		iTitle.ReserveExtraL(aMessage.GetDesLengthL(0));
		aMessage.ReadL(0, iTitle);
		iCmd.StartOutput(iId, fn);
		iCmd.AppendOutput(TPtrC8((TUint8*)iTitle.Ptr(), iTitle.Size()));
		iCmd.SendOutput();
		break;
	case EKeypressRequest:
		if (!iReadKeyMessage.IsNull() || !iReadDataMessage.IsNull()) User::Leave(KErrNotReady);
		iReadKeyMessage = aMessage;
		async = ETrue;
		SendDataIfReady();
		break;
	case EReadData8:
		if (!iReadKeyMessage.IsNull() || !iReadDataMessage.IsNull()) User::Leave(KErrNotReady);
		iReadDataMessage = aMessage;
		async = ETrue;
		//SendDataIfReady();
		if (IS_NEST_CHANNEL(iId)) iCmd.NestedSessionReadyForData(this);
		break;
	case ECancelReadData:
		if (!iReadKeyMessage.IsNull())
			{
			iReadKeyMessage.Complete(KErrCancel);
			}
		if (!iReadDataMessage.IsNull())
			{
			iReadDataMessage.Complete(KErrCancel);
			}
		break;
	case ENotifySizeChange:
		if (!iConsoleSizeChangedMessage.IsNull()) User::Leave(KErrNotReady);
		iConsoleSizeChangedMessage = aMessage;
		async = ETrue;
		break;
	case ECancelNotifySizeChange:
		if (!iConsoleSizeChangedMessage.IsNull()) iConsoleSizeChangedMessage.Complete(KErrCancel);
		break;
	case ESetAttributes:
		iCmd.StartOutput(iId, fn);
		iCmd.AppendOutput(aMessage.Int0());
		iCmd.AppendOutput(aMessage.Int1());
		iCmd.AppendOutput(aMessage.Int2());
		iCmd.SendOutput();
		break;
	default:
		break;
		}

	if (!async) aMessage.Complete(KErrNone);
	}

void CMuxSession::HandleMessage(TConsoleCommandOpCode aFn, const TDesC8& aData)
	{
	if (iDead) return;

	switch (aFn)
		{
		case ECursorPosResult:
		case EScreenSizeResult:
			{
			LOG(_L("Channel %d writing back result data len=%d for fn %d"), iId, aData.Length(), aFn);
			TInt err = iPendingSyncMessage.Write(0, aData);
			iPendingSyncMessage.Complete(err);
			break;
			}
		case EKeypressData:
			iKeydataBuf.Append(TPtrC16((TUint16*)aData.Ptr(), aData.Length()/2));
			SendDataIfReady();
			break;
		case EScreenSizeChanged:
			if (!iConsoleSizeChangedMessage.IsNull()) iConsoleSizeChangedMessage.Complete(KErrNone);
			break;
		default:
			break;
		}
	}

TBool CMuxSession::SendNestedPacket(const TDesC8& aPacket)
	{
	if (!iDead)
		{
		//TRAPD(err, iBinaryReadData.AppendL(aPacket));
		//ASSERT(!err);

		if (!iReadDataMessage.IsNull())
			{
			TInt err = iReadDataMessage.Write(0, aPacket);
			iReadDataMessage.Complete(err);
			return ETrue;
			}
		else
			{
			// Not ready, need to flow off
			return EFalse;
			}
		}
	return ETrue; // If we're dead we've still "accepted" the packet
	}

void CMuxSession::SendDataIfReady()
	{
	if (!iReadKeyMessage.IsNull() && iKeydataBuf.Length())
		{
		TKeyCode key = (TKeyCode)iKeydataBuf[0];
		TInt err = iReadKeyMessage.Write(0, TPckg<TKeyCode>(key));
		iReadKeyMessage.Complete(err);
		iKeydataBuf.Delete(0, 1);
		}
	/*
	else if (!iReadDataMessage.IsNull() && iBinaryReadData.Length())
		{
		TInt desLen = iReadDataMessage.GetDesMaxLength(0);
		if (desLen < 0)
			{
			iReadDataMessage.Complete(desLen);
			return;
			}
		TPtrC8 toWrite = iBinaryReadData.Left(Min(desLen, iBinaryReadData.Length()));
		TInt err = iReadDataMessage.Write(0, toWrite);
		iBinaryReadData.Delete(0, toWrite.Length());
		iReadDataMessage.Complete(err);
		}
	*/
	}

void CMuxSession::Died()
	{
	iDead = ETrue;
	if (!iPendingSyncMessage.IsNull()) iPendingSyncMessage.Complete(KErrDisconnected);
	if (!iReadKeyMessage.IsNull()) iReadKeyMessage.Complete(KErrDisconnected);
	if (!iReadDataMessage.IsNull()) iReadDataMessage.Complete(KErrDisconnected);
	if (!iConsoleSizeChangedMessage.IsNull()) iConsoleSizeChangedMessage.Complete(KErrDisconnected);
	}

//

CCommandOutputCollector* CCommandOutputCollector::NewL(const TDesC& aProcessName, const TDesC& aProcessArgs, TMuxConsCommand aCommandId, CCmdMuxserver& aMuxCmd)
	{
	LOG(_L("Starting command %S args=%S"), &aProcessName, &aProcessArgs);
	CCommandOutputCollector* self = new(ELeave) CCommandOutputCollector(aCommandId, aMuxCmd);
	CleanupStack::PushL(self);
	self->ConstructL(aProcessName, aProcessArgs);
	CleanupStack::Pop(self);
	return self;
	}

CCommandOutputCollector::~CCommandOutputCollector()
	{
	Cancel();
	iProcess.Close();
	}

void CCommandOutputCollector::SetSessionL(CMuxSession* aSession)
	{
	if (iSession != NULL) User::Leave(KErrAlreadyExists);

	LOG(_L("Successfully found session for output collector id %d"), iId);
	iSession = aSession;
	}

CCommandOutputCollector::CCommandOutputCollector(TMuxConsCommand aId, CCmdMuxserver& aMuxCmd)
	: CActive(CActive::EPriorityHigh + 1), iId(aId), iCmd(aMuxCmd) // Use priority higher than server and the command so we get serviced before any data either direction
	{
	CActiveScheduler::Add(this);
	iProcess.SetHandle(0);
	}

void CCommandOutputCollector::ConstructL(const TDesC& aProcessName, const TDesC& aProcessArgs)
	{
	User::LeaveIfError(iProcess.Create(aProcessName, aProcessArgs));
	iProcess.Logon(iStatus);
	if (iStatus != KRequestPending)
		{
		iProcess.Kill(KErrAbort);
		User::WaitForRequest(iStatus);
		User::Leave(iStatus.Int());
		}
	iProcess.Resume();
	SetActive();
	}

void CCommandOutputCollector::DoCancel()
	{
	iProcess.RendezvousCancel(iStatus);
	}

void CCommandOutputCollector::RunL()
	{
	TInt err = iStatus.Int();
	if (iProcess.ExitType() == EExitPanic && err >= 0) err = KErrGeneral;
	if (err == KRequestPending) err = KErrCompletion; // We use KRequestPending for our own purposes so we need to convert it to something else
	iCmd.CommandDied(this, err);
	}

/*
void CCommandOutputCollector::DataReceived(const TDesCX& aData)
	{
	//TODO
	}
*/
