// bsym_v2.h
// 
// Copyright (c) 2010 - 2012 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#ifndef BSYM_V2_H
#define BSYM_V2_H

#include "symbolics.h"

class CBsymV2 : public CBsymFile
	{
    Q_OBJECT
public:
    explicit CBsymV2(QObject *parent = 0);
	explicit CBsymV2(const QString& aFileName, QObject* aParent);
	virtual bool Open();
	bool WriteBsym(const QString& aFileName, const QVector<CSymbolics::CodeSeg>& aCodeSegs, int aSymbolCount, const QHash<QString,QString>& aRenamedBinaries, quint32 aRomChecksum);

	virtual TLookupResult LookupRomSymbol(quint32 aAddress) const;
	virtual TLookupResult Lookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const;

	virtual quint32 RomChecksum() const;

	void Dump(bool aVerbose) const;

private:
	using CBsymFile::WriteString;
	using CBsymFile::StringTableLen;

	class TCodeSeg;
	class TSymbol;
	typedef QPair<const TCodeSeg*, const TSymbol*> TCodeAndSym;
	quint32 Uint(int aOffset) const;
	const TCodeSeg* CodeSegs() const;
	const TSymbol* Symbols() const;

	quint32 CodesegSectionLength() const;
	quint32 SymbolsSectionLength() const;
	int SymbolCount() const;
	int CodeSegCount() const;
	int TokenCount() const;

	TCodeAndSym DoLookup(quint32 aAddress) const; // Only useful for ROM symbols files
	TCodeAndSym DoLookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const;
	TCodeAndSym DoCodesegLookup(const TCodeSeg& aCodeSeg, quint32 aCodesegRelativeOffset) const;
	QString GetString(quint32 aLocation, bool aExpandTokens=true) const;
	QString GetToken(int aToken) const;
	QString SymbolName(const TSymbol* aSymbol, const TCodeSeg* aParentCodeSeg = NULL) const;
	quint32 CodeSegLength(const TCodeSeg* aCodeSeg) const;
	const TCodeSeg* FindCodesegForName(const QString& aCodesegName) const;
	TLookupResult ConvertSymbol(const TCodeAndSym& aSymbol, quint32 aCodesegRelativeOffset) const;

	static void WriteString(QDataStream& stream, int aVersion, const QString& aString);
	static quint32 StringTableLen(int aVersion, const QString& aString);

	void DumpCodeSeg(const TCodeSeg* aCodeSeg) const;

private:
	class TSymbol
		{
	public:
		quint32 Address() const { return qFromBigEndian(iData[0]); }
		quint32 Length() const { return qFromBigEndian(iData[1]) & 0xFFFF; }
		quint32 NameOffset() const { return qFromBigEndian(iData[2]); }
		quint32 NamePrefixIndex() const { return qFromBigEndian(iData[1]) >> 16; }

		bool operator<(const TSymbol& other) const;
		TSymbol(quint32 aAddress) { iData[0] = qToBigEndian(aAddress); }

	protected:
		quint32 iData[3];
		};

	class TCodeSeg : public TSymbol
		{
	public:
		quint32 SymbolStart() const { return qFromBigEndian(iSymbolsStartIndex); }
		quint32 SymbolCount() const { return qFromBigEndian(iData[1]); } // Code segs use the TSymbol 'length' field to store the number of symbols, *not* the length. To get the length of a codeseg, call CBsymFile::CodeSegLen(const TCodeSeg&)
		quint32 PrefixTableOffset() const { return qFromBigEndian(iPrefixTableOffset); }

		TCodeSeg(quint32 aAddress) : TSymbol(aAddress) {}
	private:
		//quint32 iData[4];
		quint32 iSymbolsStartIndex;
		quint32 iPrefixTableOffset;
		};

	quint32 iCodesegOffset;
	quint32 iSymbolsOffset;
	quint32 iTokensOffset;

	QHash<QString, const TCodeSeg*> iFileNameToCodeSeg; // z:\sys\bin\ekern.exe -> TCodeSeg*

	// This is only used for v2.0 or earlier files, that don't have the renames section. Otherwise iFileNameToCodeSeg has all the info we need
	mutable QHash<QString, QString> iCodesegNameToFileNameCache;
	};

#endif // BSYM_V2_H
