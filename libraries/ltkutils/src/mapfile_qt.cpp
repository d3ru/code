// mapfile_qt.cpp
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
// Description:
// ConstructL Based on MagicBriefcase's symbolics.cpp AddMapFile because I can't support 2 versions of the same code...

#include <fshell/bsym.h>
#include <fshell/ltkutils.h>
#include "bsymtree.h"
#include <fshell/descriptorutils.h>
#include <QtCore>

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

struct CodeSeg;

NONSHARABLE_CLASS(CMapFileImpl) : public CMapFile
	{
public:
	CMapFileImpl();
	~CMapFileImpl();
	void ConstructL(RFs& aFs, const TDesC& aFileName);
	void DoLookup(TUint32 aOffsetInCodeSeg, TDes& aResult);
	RNode* DoCreateCompletionTreeL();

private:
	bool DoConstruct(const TDesC& aFileName, int aLoadAddress, int aCodeSize, CodeSeg& codeseg);

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

//#undef LeaveIfErr
//#define LeaveIfErr(args...) StaticLeaveIfErr(args)

// Define these for ease of porting
struct Symbol
	{
	Symbol() : iAddress(0), iLength(0) {}
	bool operator<(const Symbol& other) const { return iAddress < other.iAddress; }

	quint32 iAddress;
	int iLength;
	QString iName;
	};
struct CodeSeg
	{
	QVector<Symbol> iSymbols;
	};

void CMapFileImpl::ConstructL(RFs& /*aFs*/, const TDesC& aFileName)
	{
	//__BREAKPOINT();
	iFileName = aFileName.AllocL();

	CodeSeg codeseg;
	bool ok = false;
	QT_TRYCATCH_LEAVING(ok = DoConstruct(aFileName, 0, 0, codeseg));
	if (!ok) User::Leave(KErrNotFound);

	foreach(const Symbol& symbol, codeseg.iSymbols)
		{
		SSymbol s;
		s.iAddress = symbol.iAddress;
		s.iLength = symbol.iLength;
		s.iName = HBufC8::NewLC(symbol.iName.length());
		s.iName->Des().Copy(TPtrC((const TUint16*)symbol.iName.constData(), symbol.iName.length()));
		iSymbols.AppendL(s);
		CleanupStack::Pop(s.iName);
		}

	}

bool CMapFileImpl::DoConstruct(const TDesC& aFileName, int aLoadAddress, int aCodeSize, CodeSeg& codeseg)
	{
	QString aFilePath = QString((const QChar*)aFileName.Ptr(), aFileName.Length());
	//// BEGIN code from CSymbolics::AddMapFile
	QFile file(aFilePath);
	if (!file.open(QIODevice::ReadOnly)) return false;
	QTextStream stream(&file);

	bool rvct = false;
	if (stream.readLine().startsWith("ARM Linker, RVCT")) rvct = true;
	bool foundOffset = false;
	int linenum = 0; // for debugging
	int textOffset = 0;

	if (rvct)
		{
		while (!stream.atEnd())
			{
			QString line = stream.readLine();
			linenum++;
			if (linenum == 102)
				{
				linenum++;
				}

			if (line.length() == 0) continue;
			Symbol symbol;

			// This is rather awkward, because there's no proper delimiting between symbol name and the next stuff
			const QString KSpaces("   ");
			int spp = 55 + line.mid(55).indexOf(KSpaces) - 10;
			symbol.iName = line.left(spp).trimmed();

			QTextStream lex(&line);
			lex.seek(spp);
			quint32 rawAddress;
			lex >> rawAddress;
			symbol.iAddress = rawAddress + aLoadAddress - textOffset;
			lex.skipWhiteSpace();
			QString symbolType = line.mid(lex.pos(), 8).trimmed();
			lex.seek(lex.pos() + 10); // Far enough to get over "ARM Code" or "Thumb Code"
			lex.skipWhiteSpace();
			lex >> symbol.iLength;
			if (!foundOffset && symbol.iLength != 0)
				{
				// The text offset is the address of the first non-zero length symbol (at least, that's the most reliable way of figuring it out)
				foundOffset = true;
				textOffset = symbol.iAddress - aLoadAddress;
				symbol.iAddress = 0; // Manually relocate this symbol - by definition of it being the text offset, it's zero
				}
			if (symbolType == "Section") continue; // Sections aren't symbols as such - they can contain multiple symbols thus we don't want them confusing matters
			if (line.endsWith("Undefined Reference")) continue;
			if (aCodeSize != 0 && rawAddress - textOffset > aCodeSize) continue; // Loads of strange data sections appear at odd relocations - maksym doesn't include them so I won't bother either
			if (symbol.iLength == 0)
				{
				continue; // todo thunks lie about their size...
				}

			codeseg.iSymbols.append(symbol);
			}
		qSort(codeseg.iSymbols); // For all I moaned about GCC not sorting them, neither does RVCT (well it does, but it separates local and global symbols)
		}
	else
		{
		while (!stream.atEnd())
			{
			QString line = stream.readLine();
			linenum++;
			static const QString KGccTextOffsetLine = "Address of section .text set to ";
			if (line.startsWith(KGccTextOffsetLine))
				{
				// GCCE style
				textOffset = line.mid(KGccTextOffsetLine.length()).toInt(0,0);
				continue;
				}

			static const QString KFin = ".fini";
			if (line == KFin) break;
			if (line.length() < 16) continue;
			QTextStream lex(&line);
			lex.seek(16);
			Symbol symbol;
			symbol.iLength = 4; // unless otherwise updated by the next symbol addr
			bool ok = false;
			QString addrString;
			lex >> addrString;
			symbol.iAddress = addrString.toInt(&ok, 0);
			if (!ok) continue;
			if (aCodeSize != 0 && symbol.iAddress > aCodeSize) continue; // Loads of strange data sections appear at odd relocations - maksym doesn't include them so I won't bother either
			symbol.iAddress = symbol.iAddress + aLoadAddress - textOffset;
			lex.skipWhiteSpace();
			if (lex.pos() < 42) continue; // Code symbols have space up to column 42
			symbol.iName = lex.readAll();

			codeseg.iSymbols.append(symbol);
			}
		qSort(codeseg.iSymbols);
		for (int i = 1; i < codeseg.iSymbols.count(); i++)
			{
			Symbol& prevSymbol = codeseg.iSymbols[i - 1];
			prevSymbol.iLength = codeseg.iSymbols[i].iAddress - prevSymbol.iAddress;
			}
		}

	//END copied code
	return true;
	}

EXPORT_C void CMapFile::Lookup(TUint32 aOffsetInCodeSeg, TDes& aResult)
	{
	//TUint32 offset = aOffsetInCodeSeg & 0xFFFFFFFE; // Mask bottom bit as it just indicates thumb mode
	CMapFileImpl* impl = static_cast<CMapFileImpl*>(this); // We know we're actually a CMapFileImpl
	impl->DoLookup(aOffsetInCodeSeg, aResult);
	}

void CMapFileImpl::DoLookup(TUint32 aOffsetInCodeSeg, TDes& aResult)
	{
	if (iSymbols.Count() == 0) return; // We expect at least one symbol because of the iSymbols[pos] below

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
