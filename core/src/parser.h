// parser.h
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

#ifndef __PARSER_H__
#define __PARSER_H__

#include <e32base.h>
#include "error.h"
#include "pipe_line.h"

class CLexer;
class CParser;
namespace IoUtils { class CCommandBase; }

_LIT(KScriptPath, "SCRIPT_PATH");
_LIT(KScriptName, "SCRIPT_NAME");
_LIT(KScriptLine, "SCRIPT_LINE");
_LIT(KScriptArgCount, "ARG_COUNT");
_LIT(KKeepGoing, "KEEP_GOING");
_LIT(KChildError, "?");

class MParserObserver
	{
public:
	virtual void HandleParserComplete(CParser& aParser, const TError& aError) = 0;
	virtual void HandleParserExit(CParser& aParser);
	virtual TBool AboutToExecutePipeLineStageL(const TDesC& aOriginalLine, const TDesC& aExpandedLine, const TDesC& aPipelineCondition);
	virtual void LineReturned(TInt aError);
	};

class MBlockObserver
	{
public:
	virtual void StartingNewBlockL(MConditionalBlock* aBlock) = 0;
	virtual void BlockFinished(MConditionalBlock* aBlock) = 0;
	};

class MConditionalBlock : public MParserObserver, public IoUtils::MCommandExtensionsV3
	{
public:
	virtual TBool Break() { return EFalse; }
	virtual TBool Continue() { return EFalse; }
	virtual TBool ElseL(CCommandBase* /*aElseCommand*/, const TDesC* /*aCondition*/) { return EFalse; }
	virtual void EndBlockL(CCommandBase* aEndCommand) = 0; // aEndCommand can be null in the case of aborting a block, in which case this fn must not leave
	void SetParentBlock(MConditionalBlock* aParent) { iParentBlock = aParent; }
	MConditionalBlock* ParentBlock() { return iParentBlock; }
	void SetBlockObserver(MBlockObserver* aObserver) { iBlockObserver = aObserver; }
	MBlockObserver* BlockObserver() const { return iBlockObserver; }
	MConditionalBlock* IsConditionalBlockCommand() { return this; }

protected:
	MConditionalBlock* iParentBlock;
	MBlockObserver* iBlockObserver;
	};

class MControlStatement : public IoUtils::MCommandExtensionsV3
	{
public:
	virtual MControlStatement* IsControlStatement() { return this; }
	virtual void SetConditionalBlock(MConditionalBlock* aBlock) { iConditionalBlock = aBlock; }

protected:
	MConditionalBlock* iConditionalBlock;
	};

class CParser : public CBase, public MPipeLineObserver, public MBlockObserver
	{
public:
	enum TMode
		{
		ENormal				= 0x00000000,
		EKeepGoing			= 0x00000001,
		EDebug				= 0x00000002,
		EExportLineNumbers	= 0x00000004
		};
public:
	static CParser* NewL(TUint aMode, const TDesC& aDes, RIoSession& aIoSession, RIoReadHandle& aStdin, RIoWriteHandle& aStdout, RIoWriteHandle& aStderr, IoUtils::CEnvironment& aEnv, CCommandFactory& aFactory, MParserObserver* aObserver, TInt aStartingLineNumber = 1);
	static CParser* NewL(TUint aMode, RIoReadHandle& aSourceHandle, RIoSession& aIoSession, RIoReadHandle& aStdin, RIoWriteHandle& aStdout, RIoWriteHandle& aStderr, IoUtils::CEnvironment& aEnv, CCommandFactory& aFactory, MParserObserver* aObserver);
	~CParser();
	void Start();
	void Start(TBool& aIsForeground);
	void Kill();
	TInt Suspend();
	TInt Resume();
	TInt BringToForeground();
	void SendToBackground();
	TInt Reattach(RIoEndPoint& aStdinEndPoint, RIoEndPoint& aStdoutEndPoint, RIoEndPoint& aStderrEndPoint);
	TBool IsDisownable() const;
	void Disown();
	void SetParentConditionalBlockL(MConditionalBlock* aBlock);
	static HBufC* ExpandVariablesLC(const TDesC& aData, CLexer& aLexer, IoUtils::CEnvironment& aEnv, TBool aEscape);
private:
	enum TCondition
		{
		ENone,
		EAnd,
		EOr,
		EAndOr
		};
private:
	CParser(TUint aMode, RIoSession& aIoSession, RIoReadHandle& aStdin, RIoWriteHandle& aStdout, RIoWriteHandle& aStderr, IoUtils::CEnvironment& aEnv, CCommandFactory& aFactory, MParserObserver* aObserver, TInt aStartingLineNumber=1);
	void ConstructL(const TDesC* aDes, RIoReadHandle* aSourceHandle);
	void CreateNextPipeLine(TBool* aIsForeground);
	void CreateNextPipeLineL(TBool* aIsForeground);
	void FindNextPipeLineL(TPtrC& aData, TCondition& aCondition, TBool& aReachedLineEnd);
	HBufC* ExpandVariablesLC(const TDesC& aData) const;
	void SkipLineRemainderL();
	void DoSkipLineRemainderL();
	void SkipToEnd();
	TBool MoreSourceData() const;
	void HandlePipeLineCompleteL(CPipeLine& aPipeLine, const TError& aError);
	static TInt CompletionCallBack(TAny* aSelf);
	static TInt NextCallBack(TAny* aSelf);
	static TInt ExitCallBack(TAny* aSelf);
	MConditionalBlock* CurrentConditionalScope();
private:	// From MPipeLineObserver.
	virtual void HandlePipeLineComplete(CPipeLine& aPipeLine, const TError& aError);
private: // From MBlockObserver
	virtual void StartingNewBlockL(MConditionalBlock* aBlock);
	virtual void BlockFinished(MConditionalBlock* aBlock);
private:
	const TUint iMode;
	TCondition iCondition;
	RIoSession& iIoSession;
	RIoReadHandle iStdin;
	RIoWriteHandle iStdout;
	RIoWriteHandle iStderr;
	IoUtils::CEnvironment& iEnv;
	CCommandFactory& iFactory;
	CLexer* iLexer1;	///< Used to find a "pipe-line's worth" of data in iData.
	CLexer* iLexer2;	///< Used to parse a particular pipe-line (after its variables have been expanded).
	MParserObserver* iObserver;
	CPipeLine* iForegroundPipeLine;
	RPointerArray<CPipeLine> iBackgroundPipeLines;
	TError iCompletionError;
	CAsyncCallBack* iCompletionCallBack;
	CAsyncCallBack* iNextPipeLineCallBack;
	CAsyncCallBack* iExitCallBack;
	TBool iAbort;
	TInt iNextLineNumber;
	TBool iOwnsIoHandles;
	RArray<MConditionalBlock*> iConditionalScopes;
	RFastLock iScopeLock; // This is necessary because the BlockFinished() callback is from a different thread (namely, the endwhile command's)
	class TRootBlock : public MConditionalBlock
		{
	public:
		TRootBlock(CParser* aParser);
		void EndBlockL(CCommandBase* aEndCommand);
		void HandleParserComplete(CParser& aParser, const TError& aError);
		};
	TRootBlock iRootBlock;
	};


#endif // __PARSER_H__
