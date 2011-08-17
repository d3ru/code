// Grep.cpp
// 
// Copyright (c) 2009 - 2011 Accenture. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Accenture nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
#include <fshell/ioutils.h>
#include <fshell/common.mmh>
#include <fshell/ltkutils.h>
#include <fshell/descriptorutils.h>
#include <fshell/pcre/tregexarg.h>
#include <fshell/pcre/cregex.h>

using namespace IoUtils;
using namespace LtkUtils;

class CCmdGrep : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdGrep();
private:
	CCmdGrep();
	void PrintLineL(const TDesC& aLine);
	void To16L(const TDesC8& aSrc, RLtkBuf16& aDest);
	void To8L(const TDesC16& aSrc, RLtkBuf8& aDest);
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	HBufC* iPattern;
	HBufC* iSubstitution;
	TBool iIgnoreCase;
	TBool iInvertMatch;
	TBool iCount;
	TBool iUnicode;
	TBool iOnlyMatching;
	TBool iDebug;

	RLtkBuf8 iNarrowBuf;
	RLtkBuf8 iNarrowOut;
	RLtkBuf8 iNarrowSubst;
	RLtkBuf16 iWideOut;
	CRegEx* iRegex;
	TBool iFirstLine;
	};

EXE_BOILER_PLATE(CCmdGrep)

CCommandBase* CCmdGrep::NewLC()
	{
	CCmdGrep* self = new(ELeave) CCmdGrep();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdGrep::~CCmdGrep()
	{
	delete iPattern;
	delete iSubstitution;
	delete iRegex;
	iNarrowBuf.Close();
	iNarrowOut.Close();
	iNarrowSubst.Close();
	iWideOut.Close();
	}

CCmdGrep::CCmdGrep()
	: iFirstLine(ETrue)
	{
	}

const TDesC& CCmdGrep::Name() const
	{
	_LIT(KName, "grep");	
	return KName;
	}

void CCmdGrep::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iPattern, _L("pattern"));
	aArguments.AppendStringL(iSubstitution, _L("substitution"));
	}

void CCmdGrep::OptionsL(RCommandOptionList& aOptions)
	{
	_LIT(KCmdGrepOptIgnoreCase, "ignore-case");
	_LIT(KCmdGrepOptInvertMatch, "invert-match");
	_LIT(KCmdGrepOptCount, "count");
	_LIT(KCmdGrepOptUnicode, "unicode");
	_LIT(KCmdGrepOptOnlyMatching, "only-matching");
	_LIT(KCmdGrepOptDebug, "debug");
	aOptions.AppendBoolL(iIgnoreCase, KCmdGrepOptIgnoreCase);
	aOptions.AppendBoolL(iInvertMatch, KCmdGrepOptInvertMatch);
	aOptions.AppendBoolL(iCount, KCmdGrepOptCount);
	aOptions.AppendBoolL(iUnicode, KCmdGrepOptUnicode);
	aOptions.AppendBoolL(iOnlyMatching, KCmdGrepOptOnlyMatching);
	aOptions.AppendBoolL(iDebug, KCmdGrepOptDebug);
	}

void CCmdGrep::DoRunL()
	{
	if (iInvertMatch && iSubstitution) LeaveIfErr(KErrArgument, _L("Cannot use both the --invert-match option with the substution argument"));
	if (iInvertMatch && iOnlyMatching) LeaveIfErr(KErrArgument, _L("Cannot use both --invert-match and --only-matching options"));
	if (iSubstitution) To8L(*iSubstitution, iNarrowSubst);

	TRegExOptions options(EPcreNewlineAny);
	To8L(*iPattern, iNarrowBuf);
	if (iUnicode)
		{
		options.SetUtf8(ETrue);
		}
	if (iIgnoreCase) options.SetCaseless(ETrue);
	iRegex = CRegEx::NewL(iNarrowBuf, options);

	// Man this is a painful interface... remind me to gut it when I've got absolutely nothing better to do, or maybe just use prce directly...
	RPointerArray<TPtrC8> captures;
	CleanupResetAndDestroyPushL(captures);
	RPointerArray<const TRegExArg> caps;
	CleanupResetAndDestroyPushL(caps);
	if (iDebug)
		{
		TInt numCaptures = iRegex->NumberOfCapturingGroups();
		captures.ReserveL(numCaptures);
		caps.ReserveL(numCaptures);
		while (numCaptures--)
			{
			TPtrC8* cap = new(ELeave) TPtrC8();
			captures.Append(cap);
			caps.Append(new(ELeave) TRegExArg(cap));
			}
		}

	Stdin().SetReadMode(RIoReadHandle::ELine);
	TBuf<0x100> line;
	TInt count = 0;
	while (Stdin().Read(line) == KErrNone)
		{
		To8L(line, iNarrowBuf);
		
		//TBool matches = iRegex->PartialMatchL(iNarrowBuf);
		TInt dontCare;
		TBool matches = iRegex->DoMatchL(iNarrowBuf, CRegEx::EUnanchored, dontCare, caps);
		if (iDebug && matches)
			{
			for (TInt i = 0; i < captures.Count(); i++)
				{
				Printf(_L8("Capture %d: %S\r\n"), i+1, captures[i]);
				}
			}

		if (iInvertMatch)
			{
			matches = !matches;
			}
		if (matches)
			{
			if (iCount)
				{
				count++;
				}
			else
				{
				PrintLineL(line);
				}
			}
		else if (iSubstitution && !iOnlyMatching)
			{
			// We should always print all lines in this case
			PrintLineL(line);
			}
		}
	CleanupStack::PopAndDestroy(2, &captures);

	if (iCount)
		{
		Printf(_L("%d"), count);
		}
	}

void CCmdGrep::PrintLineL(const TDesC& aLine)
	{
	_LIT(KCrLf, "\r\n");
	if (!iFirstLine && iWideOut.Right(2) != KCrLf()) Write(KCrLf);
	iFirstLine = EFalse;

	if (iSubstitution || iOnlyMatching)
		{
		TBool ok = EFalse;
		// Yes we have already called DoMatches but it's easier to duplicate effort here rather than try and achieve everything in one call
		if (iOnlyMatching)
			{
			iNarrowOut.Zero();
			iNarrowOut.ReserveExtraL(iNarrowBuf.Length() + 256); // TODO don't just guess...
			_LIT8(KBackZero, "\\0");
			ok = iRegex->ExtractL(iSubstitution ? iNarrowSubst : KBackZero(), iNarrowBuf, iNarrowOut);
			To16L(iNarrowOut, iWideOut);
			}
		else
			{
			iNarrowBuf.ReserveExtraL(256);
			ok = iRegex->ReplaceL(iNarrowSubst, iNarrowBuf);
			To16L(iNarrowBuf, iWideOut);
			}
		/* comment below not true...
		if (!ok)
			{
			// We know we should match because we've already called DoMatchL
			User::Leave(iRegex->Error());
			}
		*/
		Write(iWideOut);
		}
	else
		{
		Write(aLine);
		}
	}

void CCmdGrep::To8L(const TDesC16& aSrc, RLtkBuf8& aDest)
	{
	aDest.Zero();
	if (iUnicode)
		{
		aDest.CopyAsUtf8L(aSrc);
		}
	else
		{
		aDest.AppendL(aSrc);
		}
	}

void CCmdGrep::To16L(const TDesC8& aSrc, RLtkBuf16& aDest)
	{
	aDest.Zero();
	if (iUnicode)
		{
		aDest.CopyFromUtf8L(aSrc);
		}
	else
		{
		aDest.AppendL(aSrc);
		}
	}
