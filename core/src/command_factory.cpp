// command_factory.cpp
// 
// Copyright (c) 2006 - 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include <f32file.h>
#include <fshell/common.mmh>
#include "command_factory.h"
#include "command_wrappers.h"
#include "commands.h"
#include "hello.h"
#include "kill.h"
#include "ps.h"
#include "undertaker.h"
#include "fuser.h"
#include "gobble.h"
#include "xmodem.h"
#include "ymodem.h"
#include "version.h"
#include "ciftest.h"
#include "while.h"
#include "if_command.h"
#include "worker_thread.h"

//
// Constants.
//

//const TUid KFshellDllUid = {FSHELL_UID2_FSHELL_DLL};
const TUid KFshellExeUid = {FSHELL_UID2_FSHELL_EXE};
const TUint KPipsExeUidValue = 0x20004c45;
const TUid KPipsExeUid = { KPipsExeUidValue };
_LIT(KFshellPrefix, "fshell_"); // This	MUST be in lower case.
_LIT(KCifExtension, ".cif");


//
// CDummyCommandConstructor.
//

class CDummyCommandConstructor : public CCommandConstructorBase
	{
public:
	static CDummyCommandConstructor* NewLC(const TDesC& aCommandName);
private: // From CCommandConstructorBase.
	virtual MCommand* ConstructCommandL();
	virtual void AppendDescriptionL(RLtkBuf16& /*aBuf*/) const { }
	};

CDummyCommandConstructor* CDummyCommandConstructor::NewLC(const TDesC& aCommandName)
	{
	CDummyCommandConstructor* self = new(ELeave) CDummyCommandConstructor();
	CleanupStack::PushL(self);
	self->BaseConstructL(aCommandName);
	return self;
	}

MCommand* CDummyCommandConstructor::ConstructCommandL()
	{
	ASSERT(EFalse);
	return NULL;
	}


//
// CCommandFactory.
//

CCommandFactory* CCommandFactory::NewL(RFs& aFs)
	{
	CCommandFactory* self = new(ELeave) CCommandFactory(aFs);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
	}

CCommandFactory::~CCommandFactory()
	{
	Cancel();
	iCommands.ResetAndDestroy();
	iLock.Close();
	delete iThreadPool;
	}

TInt CompareCommandNames(const CCommandConstructorBase& aCommand1, const CCommandConstructorBase& aCommand2)
	{
	return aCommand1.CommandName().Compare(aCommand2.CommandName());
	}

TInt CCommandFactory::FindCommandL(const TDesC& aCommandName)
	{
	CCommandConstructorBase* dummyCommand = CDummyCommandConstructor::NewLC(aCommandName);
	TInt ret = iCommands.FindInOrder(dummyCommand, TLinearOrder<CCommandConstructorBase>(CompareCommandNames));
	CleanupStack::PopAndDestroy(dummyCommand);
	return ret;
	}

void CCommandFactory::GetCommandInfoL(const TDesC& aCommand, RLtkBuf16& aDescriptionBuf)
	{
	CheckExternalCommands();
	WaitLC();

	TInt pos = FindCommandL(aCommand);
	User::LeaveIfError(pos);

	iCommands[pos]->AppendDescriptionL(aDescriptionBuf);
	CleanupStack::PopAndDestroy(); // WaitLC
	}

void KillAndCloseProcess(RProcess& aProcess)
	{
	if (aProcess.Handle() != KNullHandle && aProcess.Handle() != KCurrentProcessHandle)
		{
		aProcess.Kill(KErrGeneral);
		aProcess.Close();
		}
	}

MCommand* CCommandFactory::CreateCommandL(const TDesC& aCommandName, TError& aErrorContext, const TDesC& aArguments)
	{
	WaitLC();
	MCommand* command = NULL;
	RProcess process; // It makes DoCreateCommandL simpler if it doesn't have to worry about cleaning this up
	TRAPD(err, command = DoCreateCommandL(aCommandName, aArguments, process));
	if (err)
		{
		KillAndCloseProcess(process);
		aErrorContext.Set(err, TError::EFailedToConstructCommand, aCommandName);
		User::Leave(err);
		}
	CleanupStack::PopAndDestroy(); // WaitLC()
	return command;
	}

MCommand* CCommandFactory::DoCreateCommandL(const TDesC& aCommandName, const TDesC& aArguments, RProcess& aProcess)
	{
	RProcess& process(aProcess);

	TInt ret = FindCommandL(aCommandName);
	if (ret >= 0 && (iCommands[ret]->Type() != CCommandConstructorBase::ETypeExe || static_cast<CExeCommandConstructor*>(iCommands[ret])->ExeName().Length()))
		{
		// (1) we explicitly know about it
		return iCommands[ret]->ConstructCommandL();
		}

	/* The comment on (2) below is no longer correct because we now scan \resource\cif rather than \sys\bin so we don't automatically know about fshell_ prefixed exes
	if (iFileSystemScanned)
		{
		// (2) We successfully scanned the file system (which means we had enough PlatSec capabilities to do so),
		//     so any command that we don't know about must be a process command.
		User::LeaveIfError(process.Create(aCommandName, aArguments));
		return CProcessCommand::NewL(aCommandName, process);
		}
	*/

	// We didn't manage to scan the file system, so this command could be any kind of external command.

	// See if a .exe extension was explicitly specified.
	_LIT(KExeSuffix, ".exe");
	if (aCommandName.Right(KExeSuffix().Length()).CompareF(KExeSuffix) == 0)
		{
		// (3) The user spelled it out for us.
		return CProcessCommand::NewL(aCommandName);
		}
	else if (aCommandName.Right(KScriptSuffix().Length()).CompareF(KScriptSuffix) == 0)
		{
		// (3a) ditto
		return CScriptCommandWrapper::NewL(aCommandName);
		}

	// See if it's a script
	if (CScriptCommandWrapper::ScriptExists(iFs, aCommandName))
		{
		// (4) it's a script in \system\console\scripts
		return CScriptCommandWrapper::NewL(aCommandName);
		}

	// Next, see if it's a process command prefixed with "fshell_".
	TFileName commandName(KFshellPrefix);
	commandName.Append(aCommandName);
	TInt err = process.Create(commandName, aArguments);
	if (err == KErrNone)
		{
		TUid uid2 = process.Type()[1];
		if (uid2 == KFshellExeUid)
			{
			// (6) It's an fshell exe.
			return CProcessCommand::NewL(commandName, process);
			}
		
		// Found a .exe called fshell_cmd, but it didn't have UID2 of KFshellExeUid. Ignore it and keep looking.
		KillAndCloseProcess(process);
		}

	// This is the fallback, a process command of some kind.
	err = process.Create(aCommandName, aArguments);
	User::LeaveIfError(err); // We don't have anything else to try, at this point.
	TUid uid2 = process.Type()[1];
	if (uid2 == KPipsExeUid && process.Type()[2].iUid != FSHELL_UID_PIPSRUN) // pipsrun itself appears to be a PIPS exe but needs to be treated as an fshell one
		{
		// (8) PIPS exe.
		// CPipsCommand can't make use of the process (because it needs to open it via popen3) so we still need to kill it in this case.
		KillAndCloseProcess(process);
		return CPipsCommand::NewL(aCommandName);
		}

	// (9) Process - either an fshell cmd (one which doesn't start fshell_) or a plain old non-fshell process.
	return CProcessCommand::NewL(aCommandName, process);
	}

void CCommandFactory::ListCommandsL(RArray<TPtrC>& aList)
	{
	CheckExternalCommands();
	WaitLC();
	const TInt numCommands = iCommands.Count();
	for (TInt i = 0; i < numCommands; ++i)
		{
		CCommandConstructorBase* constructor = iCommands[i];
		if (!(constructor->Attributes() & CCommandConstructorBase::EAttNotInHelp))
			{
			TInt err = aList.Append(TPtrC(iCommands[i]->CommandName()));
			if (err && (err != KErrAlreadyExists))
				{
				User::Leave(err);
				}
			}
		}
	CleanupStack::PopAndDestroy(); // WaitLC.
	}

TInt CCommandFactory::CountUniqueCommandsL()
	{
	CheckExternalCommands();
	WaitLC();
	TInt count = 0;
	const TInt numCommands = iCommands.Count();
	for (TInt i = 0; i < numCommands; ++i)
		{
		CCommandConstructorBase* constructor = iCommands[i];
		TUint attributes = constructor->Attributes();
		if (!(attributes & (CCommandConstructorBase::EAttNotInHelp | CCommandConstructorBase::EAttAlias)))
			{
			++count;
			}
		}
	CleanupStack::PopAndDestroy(); // WaitLC.
	return count;
	}

CCommandFactory::CCommandFactory(RFs& aFs)
	: CActive(CActive::EPriorityStandard), iFs(aFs), iFactoryThreadId(RThread().Id()), iFactoryAllocator(&User::Allocator())
	{
	CActiveScheduler::Add(this);
	}

void CCommandFactory::ConstructL()
	{
	User::LeaveIfError(iLock.CreateLocal());
	User::LeaveIfError(iFs.DriveList(iDriveList));
	iThreadPool = CThreadPool::NewL();

	AddThreadCommandL(CCmdExit::NewLC); // Note, this command should never execute as 'exit' is handled explicitly by CParser. It exists so that 'exit' appears in fshell's help list and also to support 'exit --help'.
	AddThreadCommandL(CCmdHelp::NewLC, CThreadCommand::ESharedHeap);
	AddThreadCommandL(CCmdCd::NewLC, CThreadCommand::EUpdateEnvironment);
	AddThreadCommandL(CCmdClear::NewLC);
	AddThreadCommandL(CCmdFg::NewLC, CThreadCommand::ESharedHeap);
	AddThreadCommandL(CCmdBg::NewLC, CThreadCommand::ESharedHeap);
	AddThreadCommandL(CCmdJobs::NewLC, CThreadCommand::ESharedHeap);
	AddThreadCommandL(CCmdExport::NewLC, CThreadCommand::EUpdateEnvironment);
	AddThreadCommandL(CCmdExists::NewLC);
	AddThreadCommandL(CCmdVar::NewLC, CThreadCommand::EUpdateEnvironment);
	AddThreadCommandL(CCmdReattach::NewLC, CThreadCommand::ESharedHeap);
	AddThreadCommandL(CCmdDisown::NewLC, CThreadCommand::ESharedHeap);
	AddThreadCommandL(CCmdSource::NewLC, CThreadCommand::EUpdateEnvironment);
	AddThreadCommandL(CCmdForEach::NewLC, CThreadCommand::EUpdateEnvironment);
	AddThreadCommandL(CCmdHello::NewLC);
	AddThreadCommandL(CCmdRm::NewLC);
	AddThreadCommandL(CCmdCp::NewLC);
	AddThreadCommandL(CCmdMv::NewLC);
	AddThreadCommandL(CCmdMkDir::NewLC);
	AddThreadCommandL(CCmdRmDir::NewLC);
	AddThreadCommandL(CCmdKill::NewLC);
	AddThreadCommandL(CCmdLs::NewLC);
	AddThreadCommandL(CCmdPs::NewLC);
	AddThreadCommandL(CCmdMatch::NewLC);
	AddThreadCommandL(CCmdEcho::NewLC);
	AddThreadCommandL(CCmdMore::NewLC);
	AddThreadCommandL(CCmdUndertaker::NewLC);
	AddThreadCommandL(CCmdFileUser::NewLC);
	AddThreadCommandL(CCmdTrace::NewLC);
	AddThreadCommandL(CCmdMemInfo::NewLC);
	AddThreadCommandL(CCmdGobble::NewLC);
	AddThreadCommandL(CCmdDump::NewLC);
	AddThreadCommandL(CCmdSleep::NewLC);
	AddThreadCommandL(CCmdEnv::NewLC);
	AddThreadCommandL(CCmdSort::NewLC);
	AddThreadCommandL(CCmdInfoPrint::NewLC);
	AddThreadCommandL(CCmdRDebug::NewLC);
	AddThreadCommandL(CCmdDate::NewLC);
#ifdef FSHELL_CORE_SUPPORT_FSCK
	AddThreadCommandL(CCmdFsck::NewLC);
#endif
	AddThreadCommandL(CCmdDriver::NewLC);
#ifdef FSHELL_CORE_SUPPORT_CHUNKINFO
	AddThreadCommandL(CCmdChunkInfo::NewLC);
#endif
#ifdef FSHELL_CORE_SUPPORT_SVRINFO
	AddThreadCommandL(CCmdSvrInfo::NewLC);
#endif
	AddThreadCommandL(CCmdXmodem::NewLC);
	AddThreadCommandL(CCmdYmodem::NewLC);
	AddThreadCommandL(CCmdTickle::NewLC);
	AddThreadCommandL(CCmdTicks::NewLC);
	AddThreadCommandL(CCmdUpTime::NewLC);
	AddThreadCommandL(CCmdStart::NewLC);
	AddThreadCommandL(CCmdCompare::NewLC);
	AddThreadCommandL(CCmdTime::NewLC);
	AddThreadCommandL(CCmdRepeat::NewLC); // TODO: Should this have EUpdateEnvironment? It seems weird that source and foreach do but repeat doesn't. -TomS
	AddThreadCommandL(CCmdDebug::NewLC);
#ifdef FSHELL_CORE_SUPPORT_READMEM
	AddThreadCommandL(CCmdReadMem::NewLC);
#endif
	AddThreadCommandL(CCmdE32Header::NewLC);
#ifdef FSHELL_CORE_SUPPORT_OBJINFO
	AddThreadCommandL(CCmdObjInfo::NewLC);
#endif
	AddThreadCommandL(CCmdVersion::NewLC);
	AddThreadCommandL(CCmdTouch::NewLC);
	AddThreadCommandL(CCmdDialog::NewLC);
	AddThreadCommandL(CCmdConsole::NewLC);
	AddThreadCommandL(CCmdPcons::NewLC);
	AddThreadCommandL(CCmdIoInfo::NewLC);
#ifdef __WINS__
	AddThreadCommandL(CCmdJit::NewLC);
#endif
	AddThreadCommandL(CCmdDebugPort::NewLC);
	AddThreadCommandL(CCmdRom::NewLC);
	AddThreadCommandL(CCmdWhich::NewLC, CThreadCommand::ESharedHeap); // The 'which' command might reasonably want the list of external commands to be generated, meaning it has to share the main heap
	AddThreadCommandL(CCmdTee::NewLC);
	AddThreadCommandL(CCmdError::NewLC);
#ifdef FSHELL_CORE_SUPPORT_BUILTIN_REBOOT
	AddThreadCommandL(CCmdReboot::NewLC);
#endif
	AddThreadCommandL(CCmdCifTest::NewLC);
	AddThreadCommandL(CCmdWhoAmI::NewLC);
	AddThreadCommandL(CCmdWhile::NewLC, CThreadCommand::EUpdateEnvironment); // The while command has a lifetime beyond its thread (it's sort of like a 'local' command) so has to be in the same heap as its parent. Also it shares the environment
	AddThreadCommandL(CCmdEndWhile::NewLC, CThreadCommand::EUpdateEnvironment);
	// These two don't require EUpdateEnvironment because they do their stuff by completing the pipeline with an error, and basically do nothing significant in their DoRunLs.
	AddThreadCommandL(CCmdBreak::NewLC);
	AddThreadCommandL(CCmdContinue::NewLC);
	AddThreadCommandL(CCmdIf::NewLC, CThreadCommand::EUpdateEnvironment); // Similar to while
	AddThreadCommandL(CCmdElse::NewLC, CThreadCommand::EUpdateEnvironment);
	AddThreadCommandL(CCmdEndIf::NewLC, CThreadCommand::EUpdateEnvironment);
	AddThreadCommandL(CCmdTitle::NewLC);
	AddThreadCommandL(CCmdAttrib::NewLC);

	// Add some DOS-style namings of common commands.
	AddThreadCommandL(_L("del"), CCmdRm::NewLC, CCommandConstructorBase::EAttAlias);
	AddThreadCommandL(_L("md"), CCmdMkDir::NewLC, CCommandConstructorBase::EAttAlias);
	AddThreadCommandL(_L("rd"), CCmdRmDir::NewLC, CCommandConstructorBase::EAttAlias);
	AddThreadCommandL(_L("copy"), CCmdCp::NewLC, CCommandConstructorBase::EAttAlias);
	AddThreadCommandL(_L("cls"), CCmdClear::NewLC, CCommandConstructorBase::EAttAlias);
	AddThreadCommandL(_L("move"), CCmdMv::NewLC, CCommandConstructorBase::EAttAlias);
	AddThreadCommandL(_L("ren"), CCmdMv::NewLC, CCommandConstructorBase::EAttAlias);
	_LIT(KDirArg, "-l");
	AddAliasCommandL(_L("dir"), CCmdLs::NewLC, &KDirArg, NULL, CCommandConstructorBase::EAttAlias);

	// Add aliases for drive letters so that "c:" is treated like "cd c:\".
	const TInt numDrives = iDriveList.Length();
	for (TInt i = 0; i < numDrives; ++i)
		{
		if (iDriveList[i] & KDriveAttLocal|KDriveAttRom)
			{
			TChar driveLetter;
			if (RFs::DriveToChar(i, driveLetter) == KErrNone)
				{
				driveLetter.LowerCase();
				TBuf<2> name;
				name.SetLength(2);
				name[0] = driveLetter;
				name[1] = ':';
				TBuf<3> dir(name);
				dir.Append(_L("\\"));
				AddAliasCommandL(name, CCmdCd::NewLC, NULL, &dir, CCommandConstructorBase::EAttNotInHelp | CCommandConstructorBase::EAttAlias, CThreadCommand::EUpdateEnvironment);
				}
			}
		}
	}

void CCommandFactory::WatchFileSystem()
	{
	// We can't call NotifyChange if we're not the thread that the active object was originally queued in
	if (RThread().Id() == iFactoryThreadId && !IsActive())
		{
		_LIT(KExecutableDir, "?:\\sys\\bin\\");
		iFs.NotifyChange(ENotifyAll, iStatus, KExecutableDir);
		SetActive();
		}
	}

void CCommandFactory::AddCommandL(CCommandConstructorBase* aCommandConstructor)
	{
	TInt err = iCommands.InsertInOrder(aCommandConstructor, TLinearOrder<CCommandConstructorBase>(CompareCommandNames));
	if (err == KErrAlreadyExists)
		{
		// Decide whether this command implementation should override what we already know about.
		// Note, the precedence should match that in CCommandFactory::CreateCommandL for the case where is wasn't possible
		// to scan the file system.

		// The order of precedence is:
		//
		// 1) Local (thread) commands (because if they get overridden, there is no way to access them - external commands can always be specified explicitly using a file extension).
		// 2) Script commands
		// 3) "fshell_" prefixed EXE commands.
		// 4) Other EXE/PIPS commands.

		ASSERT(aCommandConstructor->Attributes() & CCommandConstructorBase::EAttExternal); // Assert that local commands have a unique name.
		TInt pos = FindCommandL(aCommandConstructor->CommandName());
		ASSERT(pos >= 0);
		CCommandConstructorBase* existingCommand = iCommands[pos];
		TBool override(EFalse);
		if (existingCommand->Type() == CCommandConstructorBase::ETypeExe && aCommandConstructor->Type() == CCommandConstructorBase::ETypeExe)
			{
			// Existing is an exe, check if we need to override because of fshell_ prefix
			CExeCommandConstructor* exeCommand = static_cast<CExeCommandConstructor*>(aCommandConstructor);
			CExeCommandConstructor* existingExeCommand = static_cast<CExeCommandConstructor*>(existingCommand);
			if (existingExeCommand->ExeName().Left(KFshellPrefix().Length()).Compare(KFshellPrefix) != 0)
				{
				// Existing is not an "fshell_" prefixed command.
				if (exeCommand->ExeName().Left(KFshellPrefix().Length()).Compare(KFshellPrefix) == 0)
					{
					// (3) aCommandConstructor is "fshell_" prefixed - allow it to override.
					override = ETrue;
					}
				}
			}
		else if ((existingCommand->Attributes() & CCommandConstructorBase::EAttExternal) != 0 && aCommandConstructor->Type() == CCommandConstructorBase::ETypeScript)
			{
			// (2) script overrides any other external command
			override = ETrue;
			}

		if (override)
			{
			delete existingCommand;
			existingCommand = NULL;
			iCommands.Remove(pos);
			iCommands.Insert(aCommandConstructor, pos);
			}
		else
			{
			delete aCommandConstructor;
			}
		}
	else
		{
		User::LeaveIfError(err);
		}
	}

void CCommandFactory::AddThreadCommandL(TCommandConstructor aConstructor, TUint aFlags)
	{
	CCommandConstructorBase* constructor = CThreadCommandConstructor::NewLC(aConstructor, aFlags, iThreadPool);
	AddCommandL(constructor);
	CleanupStack::Pop(constructor);
	}

void CCommandFactory::AddThreadCommandL(const TDesC& aCommandName, TCommandConstructor aConstructor, TUint aAttributes, TUint aFlags)
	{
	CCommandConstructorBase* constructor = CThreadCommandConstructor::NewLC(aCommandName, aConstructor, aFlags, iThreadPool);
	constructor->SetAttributes(aAttributes);
	AddCommandL(constructor);
	CleanupStack::Pop(constructor);
	}

void CCommandFactory::AddAliasCommandL(const TDesC& aAliasName, TCommandConstructor aConstructor, const TDesC* aAdditionalArguments, const TDesC* aReplacementArguments, TUint aAttributes, TUint aFlags)
	{
	CCommandConstructorBase* aliasedCommand = CThreadCommandConstructor::NewLC(aConstructor, aFlags, iThreadPool);
	CCommandConstructorBase* constructor = CAliasCommandConstructor::NewLC(aAliasName, aliasedCommand, aAdditionalArguments, aReplacementArguments);
	CleanupStack::Pop(2, aliasedCommand); // Now owned by "constructor".
	CleanupStack::PushL(constructor);
	constructor->SetAttributes(aAttributes);
	AddCommandL(constructor);
	CleanupStack::Pop(constructor);
	}

void CCommandFactory::CheckExternalCommands()
	{
	// We can't update anything if we're not using the same allocator as the factory object
	if (&User::Allocator() == iFactoryAllocator && !iFailedToScanFileSystem && !iFileSystemScanned)
		{
		TRAPD(err, FindExternalCommandsL()); // Will fail with KErrPermissionDenied if we don't have TCB.
		if (err == KErrNone)
			{
			WatchFileSystem();
			}
		else
			{
			iFailedToScanFileSystem = ETrue;
			}
		}
	}

void CCommandFactory::FindExternalCommandsL()
	{
	WaitLC();

	// Remove any external commands that we already know about.
	TInt numCommands = iCommands.Count();
	for (TInt i = (numCommands - 1); i >= 0; --i)
		{
		CCommandConstructorBase* command = iCommands[i];
		if (command->Attributes() & CCommandConstructorBase::EAttExternal)
			{
			iCommands.Remove(i);
			}
		}

	/*_LIT(KExeExtension, ".exe");
	TUidType exeUids(KNullUid, KFshellExeUid, KNullUid);
	AppendExternalCommandsL(exeUids, KExeExtension);

	TUidType pipsUids(KNullUid, KPipsExeUid, KNullUid);
	AppendExternalCommandsL(pipsUids, KExeExtension);
	*/
	AppendExternalCifCommandsL();
	AppendExternalScriptCommandsL();

	iFileSystemScanned = ETrue;

	CleanupStack::PopAndDestroy(); // WaitLC.
	}

/*
void CCommandFactory::AppendExternalCommandsL(const TUidType& aUidType, const TDesC& / *aExtension* /)
	{
	for (TInt drive = EDriveY; ; --drive)
		{
		if (drive == -1)
			{
			drive = EDriveZ;
			}

		TChar driveLetter;
		User::LeaveIfError(RFs::DriveToChar(drive, driveLetter));
		CDir* dir = NULL;
		TFileName dirName;
		dirName.Append(driveLetter);
		dirName.Append(':');
		dirName.Append(KExecutableDir);
		// Try getting the directory contents in one go
		TInt err = iFs.GetDir(dirName, aUidType, ESortByName, dir);
		if (err == KErrNone)
			{
			CleanupStack::PushL(dir);
			const TInt count = dir->Count();
			for (TInt i = 0; i < count; ++i)
				{
				DoAppendExternalCommandL((*dir)[i], aUidType[1].iUid);
				}
			CleanupStack::PopAndDestroy(dir);
			}
		else if (err == KErrNoMemory)
			{
			// If not enough memory to read dir in one go, iterate the RDir (slower but uses less memory)
			RDir d;
			TInt err = d.Open(iFs, dirName, aUidType);
			if (err == KErrNone)
				{
				CleanupClosePushL(d);
				TEntry entry;
				while (err == KErrNone)
					{
					err = d.Read(entry);
					if (err == KErrNone)
						{
						DoAppendExternalCommandL(entry, aUidType[1].iUid);
						}
					}
				CleanupStack::PopAndDestroy(&d);
				}
			}
		else if (err == KErrPermissionDenied)
			{
			// Abort in this case because all drives will doubtless fail with the same error if we don't have enough capabilities.
			User::Leave(err);
			}

		if (drive == EDriveZ)
			{
			break;
			}
		}
	}
*/

void CCommandFactory::AppendExternalCifCommandsL()
	{
	for (TInt drive = EDriveY; ; --drive)
		{
		if (drive == -1)
			{
			drive = EDriveZ;
			}

		TChar driveLetter;
		User::LeaveIfError(RFs::DriveToChar(drive, driveLetter));
		CDir* dir = NULL;
		TFileName dirName;
		dirName.Append(driveLetter);
		dirName.Append(':');
		dirName.Append(KFshellCifPath);
		// Try getting the directory contents in one go
		TInt err = iFs.GetDir(dirName, KEntryAttNormal, ESortByName, dir);
		if (err == KErrNone)
			{
			CleanupStack::PushL(dir);
			const TInt count = dir->Count();
			for (TInt i = 0; i < count; ++i)
				{
				DoAppendExternalCommandL((*dir)[i], 0);
				}
			CleanupStack::PopAndDestroy(dir);
			}
		else if (err == KErrNoMemory)
			{
			// If not enough memory to read dir in one go, iterate the RDir (slower but uses less memory)
			RDir d;
			TInt err = d.Open(iFs, dirName, KEntryAttNormal);
			if (err == KErrNone)
				{
				CleanupClosePushL(d);
				TEntry entry;
				while (err == KErrNone)
					{
					err = d.Read(entry);
					if (err == KErrNone)
						{
						DoAppendExternalCommandL(entry, 0);
						}
					}
				CleanupStack::PopAndDestroy(&d);
				}
			}
		else if (err == KErrPermissionDenied)
			{
			// Abort in this case because all drives will doubtless fail with the same error if we don't have enough capabilities.
			User::Leave(err);
			}

		if (drive == EDriveZ)
			{
			break;
			}
		}
	}


void CCommandFactory::DoAppendExternalCommandL(const TEntry& aEntry, TInt aUid)
	{
	HBufC* nameBuf = TParsePtrC(aEntry.iName).Name().AllocLC(); // Removes any extension
	nameBuf->Des().Fold();
	CCommandConstructorBase* commandConstructor = NULL;

	switch (aUid)
		{
		case FSHELL_UID2_FSHELL_EXE:
			{
			if (nameBuf->Left(KFshellPrefix().Length()).Compare(KFshellPrefix) == 0)
				{
				// Remove the fshell_ from the front, if necessary
				nameBuf->Des().Delete(0, KFshellPrefix().Length());
				}
			commandConstructor = CExeCommandConstructor::NewLC(*nameBuf, aEntry.iName);
			commandConstructor->SetAttributes(CCommandConstructorBase::EAttExternal);
			break;
			}
		case KPipsExeUidValue:
			{
			commandConstructor = CPipsCommandConstructor::NewLC(*nameBuf);
			commandConstructor->SetAttributes(CCommandConstructorBase::EAttExternal | CCommandConstructorBase::EAttNotInHelp);
			break;
			}
		case 0:
			{
			if (aEntry.iName.Right(KCifExtension().Length()).CompareF(KCifExtension) == 0)
				{
				// Only treat files with a '.cif' extension as commands (some things use '.cif-include' files).
				commandConstructor = CExeCommandConstructor::NewLC(*nameBuf, KNullDesC);
				commandConstructor->SetAttributes(CCommandConstructorBase::EAttExternal);
				}
			break;
			}
		}

	if (commandConstructor)
		{
		AddCommandL(commandConstructor);
		CleanupStack::Pop(commandConstructor);
		}
	CleanupStack::PopAndDestroy(nameBuf);
	}

void CCommandFactory::AppendExternalScriptCommandsL()
	{
	_LIT(KScriptPath, "y:\\system\\console\\scripts\\*.script");

	TFindFile find(iFs);
	CDir* matches = NULL;
	TInt err = find.FindWildByDir(KScriptPath, KNullDesC, matches);
	while (err == KErrNone)
		{
		CleanupStack::PushL(matches);
		TPtrC dir = TParsePtrC(find.File()).DriveAndPath();
		for (TInt i = 0; i < matches->Count(); i++)
			{
			const TEntry& entry = (*matches)[i];
			TPtrC name = TParsePtrC(entry.iName).Name();

			CScriptCommandConstructor* commandConstructor = CScriptCommandConstructor::NewLC(name);
			commandConstructor->SetAttributes(CCommandConstructorBase::EAttExternal);
			AddCommandL(commandConstructor);
			CleanupStack::Pop(commandConstructor);
			}
		CleanupStack::PopAndDestroy(matches);
		err = find.FindWild(matches);
		}
	}

void CCommandFactory::WaitLC() const
	{
	iLock.Wait();
	CleanupStack::PushL(TCleanupItem(Signal, const_cast<CCommandFactory*>(this)));
	}

void CCommandFactory::Signal(TAny* aSelf)
	{
	static_cast<CCommandFactory*>(aSelf)->iLock.Signal();
	}

void CCommandFactory::RunL()
	{
	if (iStatus == KErrNone)
		{
		iFileSystemScanned = EFalse;
		WatchFileSystem();
		}
	}

void CCommandFactory::DoCancel()
	{
	iFs.NotifyChangeCancel(iStatus);
	}

TInt CCommandFactory::RunError(TInt)
	{
	WatchFileSystem();
	return KErrNone;
	}
