// lexer.cpp
// 
// Copyright (c) 2006 - 2010 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include "lexer.h"

const TInt KMaxReadLength = 512;


//
// TToken
//

TToken::TToken()
	: iToken(NULL, 0), iPos(0)
	{
	}

TToken::TToken(TType aType, const TDesC& aToken, TInt aPos)
	: iType(aType), iToken(aToken), iPos(aPos)
	{
	}

TToken& TToken::operator=(const TToken& aToken)
	{
	iType = aToken.iType;
	iToken.Set(aToken.iToken);
	iPos = aToken.iPos;
	return *this;
	}

TToken::TType TToken::Type() const
	{
	return iType;
	}

TInt TToken::Position() const
	{
	return iPos;
	}

const TDesC& TToken::String() const
	{
	return iToken;
	}


//
// CLex - A cut down version of the TLex API that supports reading data incrementally from an RIoReadHandle.
//

class CLex : public CBase
	{
public:
	static CLex* NewL();
	~CLex();
	void Set(const TDesC& aDes);
	void Set(RIoReadHandle& aHandle);
	void Purge();
	void SkipToEnd();
	TBool EosL();
	void Mark();
	void IncL(TInt aNumber);
	TChar GetL();
	TChar Peek() const;
	TPtrC RemainderL(TInt aMinLength);
	void UnGet();
	TInt Offset() const;
	TInt MarkedOffset() const;
	TPtrC MarkedToken() const;
	const TUint16* Ptr() const;
private:
	CLex();
	void DoReadL();
private:
	TLex iLex;
	TInt iMarkedOffset;
	TPtrC iLexPtr;
	RBuf iLexBuf;
	TBuf<KMaxReadLength> iReadBuf;
	RIoReadHandle iHandle;
	TBool iUsingHandle;
	TBool iEos;
	};

CLex* CLex::NewL()
	{
	return new(ELeave) CLex();
	}

CLex::CLex() : iLexPtr(NULL, 0)
	{
	}

CLex::~CLex()
	{
	iLexBuf.Close();
	}

void CLex::Set(const TDesC& aDes)
	{
	iEos = EFalse;
	iUsingHandle = EFalse;
	iMarkedOffset = 0;
	iLexPtr.Set(aDes);
	iLex = iLexPtr;
	}

void CLex::Set(RIoReadHandle& aHandle)
	{
	iEos = EFalse;
	iUsingHandle = ETrue;
	iMarkedOffset = 0;
	iHandle = aHandle;
	iHandle.SetReadMode(RIoReadHandle::EOneOrMore);
	iLexBuf.Zero();
	iLex = iLexBuf;
	}

void CLex::Purge()
	{
	if (iUsingHandle)
		{
		iLexBuf.Delete(0, iLex.Offset());
		iLex = iLexBuf;
		}

	iMarkedOffset = 0;
	}

void CLex::SkipToEnd()
	{
	iEos = ETrue;
	}

TBool CLex::EosL()
	{
	if (!iEos)
		{
		if (!iLex.Eos())
			{
			// If iLex still has data, then we're definately not at the end of the string.
			// Do nothing. This test avoids us doing I/O handle reads before draining using the existing data.
			}
		else if (iUsingHandle)
			{
			DoReadL();
			}
		else
			{
			iEos = ETrue;
			}
		}
	return iEos;
	}

void CLex::Mark()
	{
	iMarkedOffset = iLex.Offset();
	}

void CLex::IncL(TInt aNumber)
	{
	if (iUsingHandle)
		{
		while (!iEos && (iLex.Remainder().Length() < aNumber))
			{
			DoReadL();
			}
		}

	iLex.Inc(aNumber);
	}

TChar CLex::GetL()
	{
	if (iUsingHandle && !iEos && (iLex.Remainder().Length() < 1))
		{
		DoReadL();
		}
	return iLex.Get();
	}

TChar CLex::Peek() const
	{
	return iLex.Peek();
	}

TPtrC CLex::RemainderL(TInt aMinLength)
	{
	if (iUsingHandle)
		{
		while (!iEos && (iLex.Remainder().Length() < aMinLength))
			{
			DoReadL();
			}
		}
	return iLex.Remainder();
	}

void CLex::UnGet()
	{
	iLex.UnGet();
	}

TInt CLex::Offset() const
	{
	return iLex.Offset();
	}

TInt CLex::MarkedOffset() const
	{
	return iMarkedOffset;
	}

TPtrC CLex::MarkedToken() const
	{
	if (iUsingHandle)
		{
		return TPtrC(iReadBuf.Ptr() + iMarkedOffset, iLex.Offset() - iMarkedOffset);
		}
	else
		{
		return TPtrC(iLexPtr.Ptr() + iMarkedOffset, iLex.Offset() - iMarkedOffset);
		}
	}

const TUint16* CLex::Ptr() const
	{
	if (iUsingHandle)
		{
		return iLexBuf.Ptr();
		}
	else
		{
		return iLexPtr.Ptr();
		}

	}

void CLex::DoReadL()
	{
	ASSERT(iUsingHandle);

	if (!iEos)
		{
		if (iReadBuf.Length() == 0) // iReadBuf may contain data if a realloc of iLexBuf failed previously.
			{
			TInt err = iHandle.Read(iReadBuf);
			if (err == KErrEof)
				{
				iEos = ETrue;
				}
			else
				{
				User::LeaveIfError(err);
				}
			}

		TInt offset = iLex.Offset();
		if ((iLexBuf.MaxLength() - iLexBuf.Length()) < iReadBuf.Length())
			{
			iLexBuf.ReAllocL(iLexBuf.Length() + iReadBuf.Length());
			}
		iLexBuf.Append(iReadBuf);
		iReadBuf.Zero();
		iLex = iLexBuf;
		iLex.Inc(offset);
		}
	}


//
// CReservedLookup
//

class CReservedLookup : public CBase
	{
public:
	class TResult
		{
	public:
		enum TType
			{
			ENoMatch,
			EMatch
			};
	public:
		TResult();
	public:
		TType iResultType;
		TToken::TType iTokenType;
		TInt iTokenLength;
		};
	enum TCharPos
		{
		ENotLast,
		ELast
		};
public:
	static CReservedLookup* NewL();
	~CReservedLookup();
	void DefineTokenTypeL(TToken::TType aTokenType, const TDesC& aString);
	void Reset();
	TResult Lookup(const TDesC& aDes);
	TInt Longest() const;
private:
	class TReserved
		{
	public:
		TReserved(TToken::TType aType, const TDesC& aString);
	public:
		TToken::TType iType;
		TPtrC iString;
		};
private:
	RArray<TReserved> iList;
	TInt iLongest;
	};

CReservedLookup* CReservedLookup::NewL()
	{
	return new(ELeave) CReservedLookup();
	}

CReservedLookup::~CReservedLookup()
	{
	iList.Close();
	}

void CReservedLookup::DefineTokenTypeL(TToken::TType aTokenType, const TDesC& aString)
	{
	User::LeaveIfError(iList.Append(TReserved(aTokenType, aString)));
	if (aString.Length() > iLongest)
		{
		iLongest = aString.Length();
		}
	}

CReservedLookup::TResult CReservedLookup::Lookup(const TDesC& aDes)
	{
	// Find the longest reserved word that matches from the beginning of this string.
	TResult result;
	const TInt count = iList.Count();
	for (TInt i = 0; i < count; ++i)
		{
		const TReserved& reserved = iList[i];
		if (aDes.Left(reserved.iString.Length()) == reserved.iString)
			{
			if (result.iTokenLength < reserved.iString.Length())
				{
				result.iTokenLength = reserved.iString.Length();
				result.iResultType = TResult::EMatch;
				result.iTokenType = reserved.iType;
				}
			}
		}
	return result;
	}

TInt CReservedLookup::Longest() const
	{
	return iLongest;
	}

CReservedLookup::TReserved::TReserved(TToken::TType aType, const TDesC& aString)
	: iType(aType), iString(aString)
	{
	}

CReservedLookup::TResult::TResult()
	: iResultType(ENoMatch), iTokenType(TToken::EString), iTokenLength(0)
	{
	}


//
// CLexer
//

CLexer* CLexer::NewL()
	{
	CLexer* self = CLexer::NewLC();
	CleanupStack::Pop(self);
	return self;
	}

CLexer* CLexer::NewL(TUint aBehaviour)
	{
	CLexer* self = CLexer::NewLC(aBehaviour);
	CleanupStack::Pop(self);
	return self;
	}

CLexer* CLexer::NewLC()
	{
	CLexer* self = new(ELeave) CLexer(EHandleSingleQuotes | EHandleDoubleQuotes | EHandleComments);
	CleanupStack::PushL(self);
	self->ConstructL();
	return self;
	}

CLexer* CLexer::NewLC(TUint aBehaviour)
	{
	CLexer* self = new(ELeave) CLexer(aBehaviour);
	CleanupStack::PushL(self);
	self->ConstructL();
	return self;
	}

CLexer::~CLexer()
	{
	delete iLex;
	delete iReservedLookup;
	}

void CLexer::DefineTokenTypeL(TToken::TType aTokenType, const TDesC& aString)
	{
	iReservedLookup->DefineTokenTypeL(aTokenType, aString);
	}

void CLexer::Set(const TDesC& aDes, const TChar& aEscapeChar)
	{
	iLex->Set(aDes);
	iEscapeChar = aEscapeChar;
	}

void CLexer::Set(RIoReadHandle& aHandle, const TChar& aEscapeChar)
	{
	iLex->Set(aHandle);
	iEscapeChar = aEscapeChar;
	}

void CLexer::Purge()
	{
	iLex->Purge();
	}

void CLexer::SkipToEnd()
	{
	iLex->SkipToEnd();
	}

TToken CLexer::NextTokenL()
	{
	SkipWhiteSpaceL();
	iLex->Mark();
	TToken::TType type(TToken::ENull);

	while (!iLex->EosL())
		{
		type = TToken::EString;
		TChar c = iLex->GetL();
		if (c == iEscapeChar)
			{
			iLex->GetL();
			}
		else if ((c == '\'') && (iBehaviour & EHandleSingleQuotes))
			{
			SkipSingleQuotedCharsL();
			}
		else if ((c == '\"') && (iBehaviour & EHandleDoubleQuotes))
			{
			SkipDoubleQuotedCharsL();
			}
		else if ((c == '#') && (iBehaviour & EHandleComments))
			{
			if (iLex->MarkedToken().Length() > 1)
				{
				iLex->UnGet();
				break;
				}
			else
				{
				SkipCommentL();
				if (iLex->EosL())
					{
					type = TToken::ENull;
					break;
					}
				else
					{
					iLex->Mark();
					}
				}
			}
		else if (c.IsSpace() && (c != '\n') && (c != '\r'))
			{
			iLex->UnGet();
			break;
			}
		else
			{
			iLex->UnGet();
			CReservedLookup::TResult result = iReservedLookup->Lookup(iLex->RemainderL(iReservedLookup->Longest()));
			if (result.iResultType == CReservedLookup::TResult::EMatch)
				{
				if (iLex->MarkedToken().Length() > 0)
					{
					break;
					}
				else
					{
					iLex->IncL(result.iTokenLength);
					type = result.iTokenType;
					break;
					}
				}
			iLex->GetL();
			}
		}

	return TToken(type, iLex->MarkedToken(), iLex->MarkedOffset());
	}

TInt CLexer::CurrentOffset() const
	{
	return iLex->Offset();
	}

TBool CLexer::MoreL()
	{
	SkipWhiteSpaceL();
	return !iLex->EosL();
	}

const TUint16* CLexer::Ptr() const
	{
	return iLex->Ptr();
	}

CLexer::CLexer(TUint aBehaviour)
	: iBehaviour(aBehaviour)
	{
	}

void CLexer::ConstructL()
	{
	iLex = CLex::NewL();
	iReservedLookup = CReservedLookup::NewL();
	}

void CLexer::SkipSingleQuotedCharsL()
	{
	while (!iLex->EosL())
		{
		TChar c = iLex->GetL();
		if ((c == iEscapeChar) && !iLex->EosL() && (iLex->Peek() == '\''))
			{
			// Allow quoted single quote characters. Note, the is a departure from Bash behaviour, but is in line with Perl and is generally helpful.
			iLex->GetL();
			}
		else if (c == '\'')
			{
			break;
			}
		}
	}

void CLexer::SkipDoubleQuotedCharsL()
	{
	while (!iLex->EosL())
		{
		TChar c = iLex->GetL();
		if ((c == iEscapeChar) && !iLex->EosL())
			{
			iLex->GetL();
			}
		else if (c == '"')
			{
			break;
			}
		}
	}

void CLexer::SkipCommentL()
	{
	while (!iLex->EosL())
		{
		TChar c = iLex->GetL();
		if ((c == '\n') || (c == '\r'))
			{
			iLex->UnGet();
			break;
			}
		}
	}

void CLexer::SkipWhiteSpaceL()
	{
	while (!iLex->EosL())
		{
		TChar c = iLex->GetL();
		if (!c.IsSpace() || (c == '\n') || (c == '\r'))
			{
			iLex->UnGet();
			break;
			}
		}
	}
