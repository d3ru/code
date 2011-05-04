// muxserver.h
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

#ifndef MUXSERVER_H
#define MUXSERVER_H

#include <fshell/ioutils.h>
#include <fshell/common.mmh>
#include <fshell/descriptorutils.h>
#include "muxcons_types.h"

#include <fshell/usbostcomm.h>
#define KFshellOstProtocolId (0x95)

using namespace IoUtils;
using LtkUtils::RLtkBuf;
using LtkUtils::RLtkBuf8;

const TInt KMaxWriteBinaryPayload = KMaxPacketLen - 12; // Max number of payload *bytes*
const TInt KMaxWritePayload = KMaxWriteBinaryPayload / 2; // The maximum number of 16-bit payload characters for a EWriteData packet that can fit within KMaxPacketLen

const TUint KOstPubSubKey = 1; // Used to ensure we never launch more than one muxserver that tries to use the OST connection

class CMuxServer;
class CCommandOutputCollector;
class CMuxSession;

class CCmdMuxserver : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdMuxserver();

	inline void StartOutput(TInt aChannelId, TConsoleCommandOpCode aOpcode);
	inline void StartOutput(TMuxConsCommand aCmd);
	inline void AppendOutput(TInt32 aInt);
	inline void AppendOutput(const TDesC8& aData);
	inline void SendOutput(TInt aNestChannelToFakeOwnerOf = 0);

public: // For CMuxServer
	TBool ListOnNextCreate() const { return iListOnNextCreate; }
	void ListConsolesAndTitles();
	void SpecialCommandConnectedL(CMuxSession* aSession);
	TInt NestLevel() const { return iNestLevel; }

public: // For CMuxSession
	void WriteDataL(TInt aChannelId, const RMessage2& aMessage);
	void SessionClosed(CMuxSession* aSession);
	void NestedSessionReadyForData(CMuxSession* aSession);

public: // For CCommandOutputCollector
	void CommandDied(CCommandOutputCollector* aCollector, TInt aCompletionCode);

private:
	CCmdMuxserver();
	//void InitiateResync();
	void HandleCommand(TInt aCmd, const TDesC8& aPayload);
	void HandleCommandLaunchNestedMuxcons(TInt aOnBehalfOfNestLevel);
	void StartProcessForCommandId(TMuxConsCommand aCommandId, const TDesC& aProcessName, const TDesC& aProcessArgs);
	void SendPing();

private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
	void DoCancel();
	void RunL();
private:
	TBool iPing;
	TBool iUseOst;
	TInt iNestLevel;

	TBuf8<KMaxPacketLen> iInputBuf;
	TPtr8 iInputBufFreeSpace;
	TBuf8<KMaxPacketLen> iOutputBuf;
	TBuf8<KMaxWriteBinaryPayload> iTempWriteBuf; // Used during handling of EWriteData
	CMuxServer* iMuxServer;
	TBool iListOnNextCreate;
	RPointerArray<CCommandOutputCollector> iOutputCollectors;
	RFile iPutFile;
	TInt32 iPutFileId;
	TUint iServerId;
	TBuf<32> iServerName;
	TUint16 iNestLevelsInUse; // Only used when --nest isn't specified
	TUint8 iNestLevelsActualOwners[16];
	RUsbOstComm iOstServer; // Client-server connection to usbostrouter
	CMuxSession* iReadBlockedOnSession; // If we had to flow off reading because a nested session couldn't process the data, save that here
	TBool iDeleting; // To avoid triggering stuff from inside our destructor
	};

class CMuxSession;

class CMuxServer : public CServer2
	{
public:
	CMuxServer(CCmdMuxserver& aCmd);
	void ConstructL(const TDesC& aServerName);
	~CMuxServer();
	TInt CountSessions() const;
	CMuxSession* SessionForId(TInt aId);
	CMuxSession* SessionAtIndex(TInt aIndex);

	void SessionClosed(CMuxSession* aSession);

protected:
	CSession2* NewSessionL(const TVersion& aVersion, const RMessage2& aMessage) const;

private:
	CSession2* DoNewSessionL(TInt aSpecialCommandId);

private:
	CCmdMuxserver& iCmd;
	RArray<CMuxSession*> iSessions;
	TInt iNextSessionId;
	};

class CMuxSession : public CSession2
	{
public:
	CMuxSession(CCmdMuxserver& aCmd, TInt aId);
	~CMuxSession();
	TInt Id() const { return iId; }
	const TDesC& Title() const { return iTitle; }
	void Died();

	void HandleMessage(TConsoleCommandOpCode aFn, const TDesC8& aData);
	TBool SendNestedPacket(const TDesC8& aPacket);

protected:
	void ServiceL(const RMessage2 &aMessage);

private:
	void SendDataIfReady();

private:
	CCmdMuxserver& iCmd;
	TInt iId;
	RMessagePtr2 iPendingSyncMessage;
	RMessagePtr2 iReadKeyMessage;
	RMessagePtr2 iReadDataMessage;
	RMessagePtr2 iConsoleSizeChangedMessage;
	TBuf<100> iKeydataBuf;
	//RLtkBuf8 iBinaryReadData;
	RLtkBuf iTitle;
	TBool iDead;
	};

// Used to track exes launched to service mux commands like screenshot (in which case grabscreen.exe) and gather their output. It's a more complicated task
// than simply handling a mux channel like any other because we want to indicate when the command's output has finished and (crucially) what the exit code
// was. Therefore we need to keep the iProcess around, and correlate its Rendezvous with the output from the associated CMuxSession, because that information
// is not known by the console.
class CCommandOutputCollector : public CActive
	{
public:
	static CCommandOutputCollector* NewL(const TDesC& aProcessName, const TDesC& aProcessArgs, TMuxConsCommand aId, CCmdMuxserver& aMuxCmd);
	~CCommandOutputCollector();

	TMuxConsCommand Id() const { return iId; }
	void SetSessionL(CMuxSession* aSession);
	//void DataReceived(const TDesCX& aData);

private:
	CCommandOutputCollector(TMuxConsCommand aId, CCmdMuxserver& aMuxCmd);
	void ConstructL(const TDesC& aProcessName, const TDesC& aProcessArgs);
	void RunL();
	void DoCancel();

private:
	TMuxConsCommand iId;
	CCmdMuxserver& iCmd;
	CMuxSession* iSession; // Not owned
	RProcess iProcess;
	};

#endif
