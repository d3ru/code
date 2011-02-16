// mapfile.cpp
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
#include <fshell/common.mmh>
#include <fshell/bsym.h>
#include <fshell/ltkutils.h>
#include "bsymtree.h"
#include <fshell/descriptorutils.h>

#include <fshell/iocli.h>
using namespace LtkUtils;
using namespace IoUtils;

const TInt KSymbolGranularity = 256; // I have no idea...

struct SSymbol
	{
	TUint iAddress;
	TUint iLength;
	HBufC8* iName;
	};

NONSHARABLE_CLASS(CMapFileImpl) : public CMapFile
	{
public:
	CMapFileImpl();
	~CMapFileImpl();
	void ConstructL(RFs& aFs, const TDesC& aFileName);
	void DoLookup(TUint32 aOffsetInCodeSeg, TDes& aResult);
	RNode* DoCreateCompletionTreeL();
	TBool GetNextLine(TPtrC8& aPtr);

private:
	RArray<SSymbol> iSymbols;
	};

EXPORT_C CMapFile* CMapFile::NewL(RFs& aFs, const TDesC& aFileName)
	{
	CMapFileImpl* result = new (ELeave) CMapFileImpl;
	CleanupStack::PushL(result);
	//CleanupStack::PushL((CBase*)1); //DEBUG
	result->ConstructL(aFs, aFileName);
	//CleanupStack::Pop(); //DEBUG
	CleanupStack::Pop(result);
	return result;
	}

CMapFile::CMapFile()
	{
	}

CMapFileImpl::CMapFileImpl()
	: iSymbols(KSymbolGranularity, _FOFF(SSymbol, iAddress))
	{
	}

CMapFileImpl::~CMapFileImpl()
	{
	const TInt count = iSymbols.Count();
	for (TInt i = 0; i < count; i++)
		{
		delete iSymbols[i].iName;
		}
	iSymbols.Close();
	}

EXPORT_C CMapFile::~CMapFile()
	{
	iReadBuf.Close();
	delete iFileName;
	}

#undef LeaveIfErr
#define LeaveIfErr(args...) StaticLeaveIfErr(args)

void CMapFileImpl::ConstructL(RFs& aFs, const TDesC& aFileName)
	{
	iFileName = aFileName.AllocL();
	iReadBuf.CreateL(4096);
	LeaveIfErr(iFile.Open(aFs, aFileName, EFileShareReadersOnly), _L("Couldn't open file %S"), &aFileName);

	TPtrC8 line;
	TBool rvct = EFalse;
	_LIT8(KRvct, "ARM Linker, RVCT");
	if (GetNextLine(line) && HasPrefix(line, KRvct)) rvct = ETrue;
	TBool foundOffset = EFalse;
	TInt linenum = 0; // for debugging

	if (rvct)
		{
		while (GetNextLine(line))
			{
			linenum++;
			if (line.Length() == 0) continue;
			SSymbol symbol;

			// This is rather awkward, because there's no proper delimiting between symbol name and the next stuff
			_LIT8(KSpaces, "   ");
			if (line.Length() < 55) continue;
			TInt foundSpaces = line.Mid(55).Find(KSpaces);
			if (foundSpaces == KErrNotFound) continue;
			TInt spp = 55 + foundSpaces - 10;
			symbol.iName = line.Left(spp).AllocLC();
			symbol.iName->Des().Trim();
				
			TLex8 lex(line);
			lex.Inc(spp);
			TInt err = HexLex(lex, symbol.iAddress);
			if (err)
				{
				// Header line perhaps
				CleanupStack::PopAndDestroy(symbol.iName);
				continue;
				}
			symbol.iAddress -= iTextOffset;
			lex.SkipSpace();
			TBuf8<8> symbolType = line.Mid(lex.Offset(), 8);
			symbolType.Trim();


			lex.Inc(10); // Far enough to get over "ARM Code" or "Thumb Code"
			lex.SkipSpace();
			symbol.iLength = 0;
			lex.Val(symbol.iLength);
			if (!foundOffset && symbol.iLength != 0)
				{
				// The text offset is the address of the first non-zero length symbol (at least, that's the most reliable way of figuring it out)
				foundOffset = ETrue;
				iTextOffset = symbol.iAddress;
				symbol.iAddress = 0; // Manually relocate this symbol - by definition of it being the text offset, it's zero
				}
			_LIT8(KSection, "section");
			_LIT8(KUndef, "Undefined Reference");
			if (symbolType == KSection || line.Right(KUndef().Length()) == KUndef || symbol.iLength == 0)
				{
				// Sections aren't symbols as such - they can contain multiple symbols thus we don't want them confusing matters
				// And thunks lie about their size
				CleanupStack::PopAndDestroy(symbol.iName);
				continue;
				}

			User::LeaveIfError(iSymbols.Append(symbol));
			CleanupStack::Pop(symbol.iName);
			}
		iSymbols.SortUnsigned(); // For all I moaned about GCC not sorting them, neither does RVCT (well it does, but it separates local and global symbols)
		}
	else
		{
		while (GetNextLine(line))
			{
			linenum++;
			_LIT8(KGccTextOffsetLine, "Address of section .text set to ");
			if (HasPrefix(line, KGccTextOffsetLine))
				{
				// GCCE style
				TLex8 lex(line.Mid(KGccTextOffsetLine().Length()));
				iTextOffset = HexLexL(lex);
				continue;
				}

			_LIT8(KFin, ".fini");
			if (line == KFin) break;
			if (line.Length() < 16) continue;
			TLex8 lex(line);
			lex.Inc(16);
			SSymbol symbol;
			symbol.iLength = 4; // unless otherwise updated by the next symbol addr
			TInt err = HexLex(lex, symbol.iAddress);
			if (err) continue;
			symbol.iAddress -= iTextOffset;
			lex.SkipSpace();
			if (lex.Offset() < 42) continue; // Code symbols have space up to column 42
			symbol.iName = lex.Remainder().AllocLC();

			User::LeaveIfError(iSymbols.Append(symbol));
			CleanupStack::Pop(symbol.iName);
			}
		// GCCE doesn't even sort the symbols in its map file. Unbelievable! Or possibly even inconceivable!
		iSymbols.SortUnsigned();
		for (TInt i = 1; i < iSymbols.Count(); i++)
			{
			SSymbol& prevSymbol = iSymbols[i - 1];
			prevSymbol.iLength = iSymbols[i].iAddress - prevSymbol.iAddress;
			}
		}
	iFile.Close();
	iReadBuf.Close();
	}

TBool CMapFileImpl::GetNextLine(TPtrC8& aPtr)
	{
	_LIT8(KNewline, "\r\n");
	iReadBuf.Delete(0, aPtr.Length());
	if (HasPrefix(iReadBuf, KNewline)) iReadBuf.Delete(0, KNewline().Length());

	TInt newline = iReadBuf.Find(KNewline);
	if (newline != KErrNotFound)
		{
		aPtr.Set(iReadBuf.Left(newline));
		return ETrue;
		}
	// Otherwise need to try reading some more from file
	TPtr8 restOfDesc((TUint8*)iReadBuf.Ptr() + iReadBuf.Size(), 0, iReadBuf.MaxSize() - iReadBuf.Size());
	TInt err = iFile.Read(restOfDesc);
	if (err) restOfDesc.Zero();
	iReadBuf.SetLength(iReadBuf.Length() + restOfDesc.Length());
	
	// Now re-try looking for newline
	newline = iReadBuf.Find(KNewline);
	if (newline != KErrNotFound)
		{
		aPtr.Set(iReadBuf.Left(newline + KNewline().Length()));
		return ETrue;
		}

	// No more newlines, just return whatever's left in the buffer
	aPtr.Set(iReadBuf);
	return aPtr.Length() != 0; // And return whether it's worth having
	}

EXPORT_C void CMapFile::Lookup(TUint32 aOffsetInCodeSeg, TDes& aResult)
	{
	//TUint32 offset = aOffsetInCodeSeg & 0xFFFFFFFE; // Mask bottom bit as it just indicates thumb mode
	CMapFileImpl* impl = static_cast<CMapFileImpl*>(this); // We know we're actually a CMapFileImpl
	impl->DoLookup(aOffsetInCodeSeg, aResult);
	}

void CMapFileImpl::DoLookup(TUint32 aOffsetInCodeSeg, TDes& aResult)
	{
	TInt pos = 0;
	SSymbol dummy; dummy.iAddress = aOffsetInCodeSeg;
	TBool found = iSymbols.FindInUnsignedKeyOrder(dummy, pos) == KErrNone;
	if (!found && pos != 0) pos--;

	aResult.Zero();
	const SSymbol& symbol = iSymbols[pos];
	if (aOffsetInCodeSeg >= symbol.iAddress && aOffsetInCodeSeg < symbol.iAddress + symbol.iLength)
		{
		aResult.Copy(*symbol.iName);
		aResult.AppendFormat(_L(" + 0x%x"), aOffsetInCodeSeg - symbol.iAddress);
		}
	}

EXPORT_C void CMapFile::GetFileNameL(TDes& aFileName) const
	{
	aFileName.Copy(*iFileName);
	}

RNode* CMapFile::CreateCompletionTreeL()
	{
	CMapFileImpl* impl = static_cast<CMapFileImpl*>(this); // We know we're actually a CMapFileImpl
	return impl->DoCreateCompletionTreeL();
	}

RNode* CMapFileImpl::DoCreateCompletionTreeL()
	{
	RNode* result = RNode::NewL();
	CleanupDeletePushL(result);

	RLtkBuf tempBuf;
	tempBuf.CreateLC(256);
	for (TInt i = 0; i < iSymbols.Count(); i++)
		{
		tempBuf.Zero();
		tempBuf.AppendL(*iSymbols[i].iName);
		tempBuf.ReserveExtraL(1);
		result->InsertStringL(tempBuf.PtrZ(), iSymbols[i].iAddress);
		}
	CleanupStack::PopAndDestroy(&tempBuf);
	CleanupStack::Pop(result);
	return result;
	}
