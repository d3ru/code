// if_command.cpp
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

#include "if_command.h"
#include "command_factory.h"
#include "fshell.h" // For gShell

_LIT(KIf, "if");
_LIT(KEndIf, "endif");
_LIT(KElse, "else");

CCommandBase* CCmdIf::NewLC()
	{
	CCmdIf* self = new(ELeave) CCmdIf();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdIf::~CCmdIf()
	{
	// Note that this potentially runs in a different thread (although with the same heap) as DoRunL().
	if (iBlockObserver) iBlockObserver->BlockFinished(this);
	delete iCondition;
	if (iHasRun) delete &Env(); // Normally thread commands don't own their environment, but we do because of our extended lifespan
	}

CCmdIf::CCmdIf()
	: CCommandBase(ESharableIoSession), iNestedIf(1)
	{
	iBlockObserver = NULL;
	iParentBlock = NULL;
	SetExtension(this);
	}

const TDesC& CCmdIf::Name() const
	{
	return KIf;
	}

void CCmdIf::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iCondition, _L("condition"));
	}

void CCmdIf::OptionsL(RCommandOptionList& /*aOptions*/)
	{
	}

void CCmdIf::DoRunL()
	{
	Deque(); // This is needed because our active scheduler will get destroyed when the thread cleans up but we don't get deleted until after that
	FsL().Close();

	ExecuteConditionalL(this, *iCondition);

	iHasRun = ETrue;
	// Magic completion code to indicate we're not totally finished, and shouldn't be deleted. Only has an effect for thread commands that are also MConditionalBlocks
	SetErrorReported(ETrue);
	Complete(KRequestPending);
	}

void CCmdIf::ExecuteConditionalL(CCommandBase* aCommand, const TDesC& aCondition)
	{
	iState = EExecutingConditional;
	CParser* conditionalParser = CParser::NewL(CParser::ENormal, aCondition, IoSession(), Stdin(), Stdout(), Stderr(), Env(), gShell->CommandFactory(), this);
	TError err(aCommand->Stderr(), Env());
	iConditionalError = &err;
	conditionalParser->Start();
	CActiveScheduler::Start();
	delete conditionalParser;
	iState = ERunning;

	if (err.Error())
		{
		err.Report();
		aCommand->PrintError(err.Error(), _L("Aborted \"%S\" at line %d"), &err.ScriptFileName(), err.ScriptLineNumber());
		User::Leave(err.Error());
		}
	// HandleParserComplete will set iShouldExecute as appropriate
	}

TBool CCmdIf::ElseL(CCommandBase* aElseCommand, const TDesC* aCondition)
	{
	if (iHasSeenUnconditionalElse)
		{
		CommandLeaveIfErr(*aElseCommand, KErrArgument, _L("Cannot have any else clauses after an 'else' with no condition"));
		}
	else if (aCondition && !iHasExecutedSomeClause)
		{
		ExecuteConditionalL(aElseCommand, *aCondition);
		}
	else
		{
		iShouldExecute = !iHasExecutedSomeClause;
		}
	iHasSeenUnconditionalElse = (aCondition == NULL);
	return ETrue;
	}

TBool CCmdIf::AboutToExecutePipeLineStage(const TDesC& aOriginalLine, const TDesC& /*aExpandedLine*/, const TDesC& /*aPipelineCondition*/)
	{
	if (iState == ERunning)
		{
		TLex lex(aOriginalLine);
		TPtrC token = lex.NextToken();
		if (token == KIf)
			{
			iNestedIf++;
			}
		else if (token == KEndIf)
			{
			iNestedIf--;
			if (iNestedIf <= 0) return ETrue; // Returning ETrue means CParser should execute the endif, which will terminate the block by calling our EndBlockL function
			}
		else if (token == KElse)
			{
			if (iNestedIf <= 1) return ETrue; // If we're not nested, we should execute the else
			}
		return iShouldExecute;
		}
	else
		{
		return ETrue;
		}
	}

void CCmdIf::EndBlockL(CCommandBase* aEndCommand)
	{

	if (aEndCommand && aEndCommand->Name() != KEndIf)
		{
		CleanupStack::PushL(this);
		CommandLeaveIfErr(*aEndCommand, KErrArgument, _L("Expected endif statement, found %S instead"), &aEndCommand->Name());
		// Deliberately no pop - we always Leave
		}

	delete this; // This will call BlockFinished()
	}

void CCmdIf::HandleParserComplete(CParser& /*aParser*/, const TError& aError)
	{
	switch (iState)
		{
	case EExecutingConditional:
		iShouldExecute = (aError.Error() == KErrNone);
		if (iShouldExecute && Env().IsInt(KChildError))
			{
			// Also need to check $? because a condition of the form "x && y" doesn't actually return an error to the parser if x is false, because of the &&
			iShouldExecute = Env().GetAsInt(KChildError) == KErrNone;
			}
		// We don't report errors in evaluating the condition, unless it's a syntax error
		if (aError.Error() && aError.Reason() != TError::ECommandError)
			{
			iConditionalError->Set(aError);
			}
		else if (iShouldExecute)
			{
			iHasExecutedSomeClause = ETrue;
			}
		CActiveScheduler::Stop();
		break;
	case ERunning:
	default:
		ASSERT(EFalse); // Don't think this should ever happen
		break;
		}
	}

//

CCommandBase* CCmdElse::NewLC()
	{
	CCmdElse* self = new(ELeave) CCmdElse;
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

const TDesC& CCmdElse::Name() const
	{
	return KElse;
	}

void CCmdElse::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendEnumL(iDummyIf, _L("if"));
	aArguments.AppendStringL(iCondition, _L("condition"));
	}

CCmdElse::~CCmdElse()
	{
	delete iCondition;
	}

void CCmdElse::DoRunL()
	{
	if (iArguments.IsPresent(&iDummyIf) && iCondition == NULL)
		{
		LeaveIfErr(KErrArgument, _L("'else if' without condition - did you mean just 'else'?"));
		}

	TBool foundIf = EFalse;
	while (!foundIf && iConditionalBlock)
		{
		foundIf = iConditionalBlock->ElseL(this, iCondition);
		if (!foundIf) iConditionalBlock = iConditionalBlock->ParentBlock();
		}
	if (iConditionalBlock == NULL)
		{
		LeaveIfErr(KErrArgument, _L("No 'if' statement found for 'else'."));
		}
	}

//

CCommandBase* CCmdEndIf::NewLC()
	{
	CCmdEndIf* self = new(ELeave) CCmdEndIf;
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

const TDesC& CCmdEndIf::Name() const
	{
	return KEndIf;
	}

void CCmdEndIf::DoRunL()
	{
	if (iConditionalBlock == NULL)
		{
		LeaveIfErr(KErrArgument, _L("No matching 'if' statement found for 'endif'"));
		}
	iConditionalBlock->EndBlockL(this);
	}
