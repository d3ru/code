// if_command.h
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

#ifndef __IF_COMMAND__
#define __IF_COMMAND__

#include <fshell/ioutils.h>
#include "parser.h"

class CCmdIf : public CCommandBase, public MConditionalBlock
	{
public:
	static CCommandBase* NewLC();
	~CCmdIf();
private:
	CCmdIf();
	void ExecuteConditionalL(CCommandBase* aCommand, const TDesC& aCondition);
private: // From CCommandBase
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private: // From MConditionalBlock
	virtual void HandleParserComplete(CParser& aParser, const TError& aError);
	virtual TBool AboutToExecutePipeLineStage(const TDesC& aOriginalLine, const TDesC& aExpandedLine, const TDesC& aPipelineCondition);
	virtual void EndBlockL(CCommandBase* aEndCommand);
	TBool ElseL(CCommandBase* aElseCommand, const TDesC* aCondition);

private:
	HBufC* iCondition;
	TBool iShouldExecute;
	TBool iHasExecutedSomeClause;
	TBool iHasSeenUnconditionalElse;

	TInt iNestedIf; // Used to make sure a nested endif doesn't make us exit prematurely
	TError* iConditionalError;
	enum
		{
		EExecutingConditional,
		ERunning,
		} iState; // This is needed to keep track of what to do in the MParserObserver callbacks
	TBool iHasRun;
	};

class CCmdElse : public CCommandBase, public MControlStatement
	{
public:
	static CCommandBase* NewLC();
	CCmdElse() { SetExtension(this); }
	~CCmdElse();
private: // From CCommandBase
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);

private:
	TInt iDummyIf;
	HBufC* iCondition;
	};


class CCmdEndIf : public CCommandBase, public MControlStatement
	{
public:
	static CCommandBase* NewLC();
	CCmdEndIf() { SetExtension(this); }
private: // From CCommandBase
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	};

#endif // __IF_COMMAND__
