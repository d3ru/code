// while.h
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

#ifndef __WHILE_H__
#define __WHILE_H__

#include <fshell/ioutils.h>
#include <fshell/descriptorutils.h>
#include "parser.h"

using LtkUtils::RLtkBuf;

class CCmdWhile : public CCommandBase, public MConditionalBlock
	{
public:
	static CCommandBase* NewLC();
	~CCmdWhile();
private:
	CCmdWhile();
	void DoEndBlockL(CCommandBase* aEndCommand);
private: // From CCommandBase
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private: // From MConditionalBlock
	virtual void HandleParserComplete(CParser& aParser, const TError& aError);
	virtual TBool AboutToExecutePipeLineStage(const TDesC& aOriginalLine, const TDesC& aExpandedLine, const TDesC& aPipelineCondition);
	virtual TBool Break();
	virtual TBool Continue();
	virtual void EndBlockL(CCommandBase* aEndCommand);

private:
	HBufC* iCondition;

	TBool iConditionEvaluatedTrue;

	CParser* iBlockParser; // The parser we use to actually execute the block. Owned. *Not* the parent that invoked the while command in the first place
	RLtkBuf iWhileBlock; // The text of the lines between the while and the corresponding endwhile
	TInt iBlockStartLineNumber;
	TInt iNestedWhile; // Used while assembling iWhileBlock to make sure a nested endwhile doesn't make us exit prematurely
	TError* iEndBlockError;
	CCommandBase* iEndCommand;
	enum
		{
		EAssemblingWhileBlock,
		EExecutingConditional,
		EExecutingBlock,
		} iState; // This is needed to keep track of what to do in the MParserObserver callbacks
	TBool iHasRun;
	};

class CCmdBreak : public CCommandBase, public MControlStatement
	{
public:
	static CCommandBase* NewLC();
	CCmdBreak() { SetExtension(this); }
private: // From CCommandBase
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	};

class CCmdContinue : public CCommandBase, public MControlStatement
	{
public:
	static CCommandBase* NewLC();
	CCmdContinue() { SetExtension(this); }
private: // From CCommandBase
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	};

class CCmdEndWhile : public CCommandBase, public MControlStatement
	{
public:
	static CCommandBase* NewLC();
	CCmdEndWhile() { SetExtension(this); }
private: // From CCommandBase
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	};

#endif // __WHILE_H__
