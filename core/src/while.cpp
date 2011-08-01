// while.cpp
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

#include "while.h"
#include "command_factory.h"
#include "fshell.h" // For gShell

_LIT(KWhile, "while");
_LIT(KEndWhile, "endwhile");
_LIT(KBreak, "break");
_LIT(KContinue, "continue");

CCommandBase* CCmdWhile::NewLC()
	{
	CCmdWhile* self = new(ELeave) CCmdWhile();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdWhile::~CCmdWhile()
	{
	// Note that this potentially runs in a different thread (although with the same heap) as DoRunL().
	if (iBlockObserver) iBlockObserver->BlockFinished(this);
	delete iCondition;
	iWhileBlock.Close();
	if (iHasRun) delete &Env(); // Normally thread commands don't own their environment, but we do because of our extended lifespan
	}

CCmdWhile::CCmdWhile()
	: CCommandBase(ESharableIoSession), iNestedWhile(1)
	{
	iBlockObserver = NULL;
	iParentBlock = NULL;
	SetExtension(this);
	}

const TDesC& CCmdWhile::Name() const
	{
	return KWhile;
	}

void CCmdWhile::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iCondition, _L("condition"));
	}

void CCmdWhile::OptionsL(RCommandOptionList& /*aOptions*/)
	{
	}

void CCmdWhile::DoRunL()
	{
	TRAPD(err, iBlockStartLineNumber = Env().GetAsIntL(KScriptLine) + 1);
	LeaveIfErr(err, _L("No %S defined - using a while statement not inside a script?"), &KScriptLine);
	Deque(); // This is needed because our active scheduler will get destroyed when the thread cleans up but we don't get deleted until after that
	FsL().Close();
	iHasRun = ETrue;
	// Magic completion code to indicate we're not totally finished, and shouldn't be deleted. Only has an effect for thread commands that are also MConditionalBlocks
	SetErrorReported(ETrue);
	Complete(KRequestPending);
	}

TBool CCmdWhile::AboutToExecutePipeLineStageL(const TDesC& aOriginalLine, const TDesC& /*aExpandedLine*/, const TDesC& aPipelineCondition)
	{
	if (iState == EAssemblingWhileBlock)
		{
		// We're still assembling the block by letting our parent parser step over it
		TLex lex(aOriginalLine);
		TPtrC token = lex.NextToken();
		_LIT(KCrLf, "\r\n");
		if (token == KWhile)
			{
			iNestedWhile++;
			}
		else if (token == KEndWhile)
			{
			iNestedWhile--;
			if (iNestedWhile <= 0) return ETrue; // Returning ETrue means CParser should execute the endwhile, which will terminate the block by calling our EndBlockL function
			}

		iWhileBlock.AppendL(aOriginalLine);
		if (aPipelineCondition.Length())
			{
			iWhileBlock.AppendL(_L(" "));
			iWhileBlock.AppendL(aPipelineCondition);
			iWhileBlock.AppendL(_L(" "));
			}
		else
			{
			iWhileBlock.AppendL(KCrLf);
			}
		return EFalse; // While collecting data, we don't execute anything
		}
	return ETrue; // In any other case 
	}

void CCmdWhile::EndBlockL(CCommandBase* aEndCommand)
	{
	if (aEndCommand == NULL)
		{
		delete this;
		return;
		}

	CleanupStack::PushL(this);

	if (aEndCommand->Name() != KEndWhile)
		{
		CommandLeaveIfErr(*aEndCommand, KErrArgument, _L("Expected endwhile statement, found %S instead"), &aEndCommand->Name());
		}

	iEndCommand = aEndCommand;
	iConditionEvaluatedTrue = ETrue;
	while (iConditionEvaluatedTrue)
		{
		// First, evaluate the condition
		TError blockError(aEndCommand->Stderr(), Env());
		iEndBlockError = &blockError;
		if (iCondition)
			{
			iState = EExecutingConditional;
			CParser* conditionalParser = CParser::NewL(CParser::ENormal, *iCondition, IoSession(), Stdin(), Stdout(), Stderr(), Env(), gShell->CommandFactory(), this);
			conditionalParser->Start();
			CActiveScheduler::Start();
			delete conditionalParser;

			if (blockError.Error())
				{
				blockError.Report();
				aEndCommand->PrintError(blockError.Error(), _L("Aborted \"%S\" at line %d"), &blockError.ScriptFileName(), iBlockStartLineNumber-1);
				User::Leave(blockError.Error());
				}
			}

		if (!iConditionEvaluatedTrue) break;

		// Prevent executing the block from messing up the script line count, by creating a shared env and setting KScriptLine as a local var
		CEnvironment* envForBlock = Env().CreateSharedEnvironmentL();
		CleanupStack::PushL(envForBlock);
		envForBlock->SetLocalL(KScriptLine);
		iState = EExecutingBlock;
		TUint mode = CParser::EExportLineNumbers;
		if (Env().IsDefined(KKeepGoing)) mode |= CParser::EKeepGoing;
		iBlockParser = CParser::NewL(mode, iWhileBlock, IoSession(), Stdin(), Stdout(), Stderr(), *envForBlock, gShell->CommandFactory(), this, iBlockStartLineNumber);
		CleanupStack::PushL(iBlockParser);
		iBlockParser->SetParentConditionalBlockL(this); // This is needed so that break commands in the body of the while loop know they're in a while loop
		iBlockParser->Start();
		CActiveScheduler::Start();
		CleanupStack::PopAndDestroy(iBlockParser);
		iBlockParser = NULL;

		if (blockError.Error() < 0)
			{
			blockError.Report();
			aEndCommand->PrintError(blockError.Error(), _L("Aborted \"%S\" at line %d"), &blockError.ScriptFileName(), blockError.ScriptLineNumber());
			User::Leave(blockError.Error());
			}
		CleanupStack::PopAndDestroy(envForBlock);
		}
	CleanupStack::PopAndDestroy(this); // This will call BlockFinished()
	}

void CCmdWhile::HandleParserComplete(CParser& /*aParser*/, const TError& aError)
	{
	switch (iState)
		{
	case EExecutingConditional:
		iConditionEvaluatedTrue = (aError.Error() == KErrNone);
		if (iConditionEvaluatedTrue && Env().IsInt(KChildError))
			{
			// Also need to check $? because a condition of the form "x && y" doesn't actually return an error to the parser if x is false, because of the &&
			iConditionEvaluatedTrue = Env().GetAsInt(KChildError) == KErrNone;
			}

		// We don't report errors in evaluating the condition, unless it's a syntax error
		if (aError.Error() && aError.Reason() != TError::ECommandError)
			{
			iEndBlockError->Set(aError);
			}
		CActiveScheduler::Stop();
		break;
	case EExecutingBlock:
		if (aError.Error() == KErrCompletion && aError.Context() && *aError.Context() == KContinue)
			{
			// Not really an error
			}
		else if (aError.Error() == KErrCompletion && aError.Context() && *aError.Context() == KBreak)
			{
			// Also not a error, but need to indicate not to continue loop
			iConditionEvaluatedTrue = EFalse;
			}
		else if (aError.Error())
			{
			iEndBlockError->Set(aError);
			}
		CActiveScheduler::Stop();
		break;
	default:
		break;
		}
	}

TBool CCmdWhile::Break()
	{
	return ETrue;
	}

TBool CCmdWhile::Continue()
	{
	return ETrue;
	}

//

CCommandBase* CCmdBreak::NewLC()
	{
	CCmdBreak* self = new(ELeave) CCmdBreak;
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

const TDesC& CCmdBreak::Name() const
	{
	return KBreak;
	}

void CCmdBreak::DoRunL()
	{
	TBool broke = EFalse;
	while (!broke && iConditionalBlock)
		{
		broke = iConditionalBlock->Break();
		if (!broke) iConditionalBlock = iConditionalBlock->ParentBlock();
		}
	if (iConditionalBlock == NULL)
		{
		LeaveIfErr(KErrArgument, _L("Break statement encountered not in a while block"));
		}
	SetErrorReported(ETrue);
	Complete(KErrCompletion);
	}

//

CCommandBase* CCmdContinue::NewLC()
	{
	CCmdContinue* self = new(ELeave) CCmdContinue;
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

const TDesC& CCmdContinue::Name() const
	{
	return KContinue;
	}

void CCmdContinue::DoRunL()
	{
	TBool continued = EFalse;
	while (!continued && iConditionalBlock)
		{
		continued = iConditionalBlock->Continue();
		if (!continued) iConditionalBlock = iConditionalBlock->ParentBlock();
		}
	if (iConditionalBlock == NULL)
		{
		LeaveIfErr(KErrArgument, _L("Continue statement encountered not in a while block"));
		}
	SetErrorReported(ETrue);
	Complete(KErrCompletion);
	}

//

CCommandBase* CCmdEndWhile::NewLC()
	{
	CCmdEndWhile* self = new(ELeave) CCmdEndWhile;
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

const TDesC& CCmdEndWhile::Name() const
	{
	return KEndWhile;
	}

void CCmdEndWhile::DoRunL()
	{
	if (iConditionalBlock == NULL)
		{
		LeaveIfErr(KErrArgument, _L("No matching while statement found for endwhile"));
		}
	iConditionalBlock->EndBlockL(this);
	}
