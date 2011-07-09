// bsym_v3.h
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

#ifndef BSYM_V3_H
#define BSYM_V3_H

#include "symbolics.h"

class Pager;

class CBsymV3 : public CBsymFile
	{
    Q_OBJECT
public:
    explicit CBsymV3(QObject *parent = 0);
	explicit CBsymV3(const QString& aFileName, QObject* aParent);
	virtual bool Open();
	bool WriteBsym(CBsymFile::TVersion aVersion, const QString& aFileName, const QVector<CSymbolics::CodeSeg>& aCodeSegs, int aSymbolCount, const QHash<QString,QString>& aRenamedBinaries, quint32 aRomChecksum);
	~CBsymV3();

	virtual TLookupResult LookupRomSymbol(quint32 aAddress) const;
	virtual TLookupResult Lookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const;

	virtual quint32 RomChecksum() const;

private:
	struct TSymbolEntry;
	struct TDbgUnitEntry;
	quint32 Uint(int aOffset) const;
	QString SymbolName(const TSymbolEntry* aSymbol) const;
	QString EntryName(const TDbgUnitEntry* aEntry) const;
	QString GetString(quint32 aLocation) const;
	quint32 ByteSwapIfNeeded(quint32 aData) const;

	const uchar* GetData(quint32 aOffset, int aLength) const; // Offset relative to iDbgUnitOffset (or start of compressed region, as appropriate)
	void ReleaseData(int aNumRequests = 1) const;

	typedef const TDbgUnitEntry* TPagedDbgUnitEntry; // TPagedDbgUnitEntry *cannot* simply be dereferenced. Even though its definition looks like it should be, it's only defined like this to because I'm too lazy to define a custom iterator from scratch
	typedef const TSymbolEntry* TPagedSymbolEntry; // Ditto
	const TDbgUnitEntry* GetEntry(TPagedDbgUnitEntry aOffset) const { return GetEntry((quint32)aOffset); }
	const TDbgUnitEntry* GetEntry(quint32 aOffset) const { return (const TDbgUnitEntry*)GetData((quint32)aOffset, sizeof(TDbgUnitEntry)); }
	const TSymbolEntry* GetSymbol(quint32 aOffset) const { return (const TSymbolEntry*)GetData((quint32)aOffset, sizeof(TSymbolEntry)); }
	const TSymbolEntry* GetSymbol(TPagedSymbolEntry aOffset) const { return GetSymbol(iSymbolOffset + (quint32)aOffset); }
	void ConsistancyCheck() const;

	quint32 CodeAddress(const TDbgUnitEntry* aEntry) const { return ByteSwapIfNeeded(aEntry->iCodeAddress); }
	quint32 CodeLength(const TDbgUnitEntry* aEntry) const;

	TLookupResult DoCodesegLookup(const TDbgUnitEntry* aEntry, quint32 aCodesegRelativeOffset) const;

private:
	struct TDbgUnitEntry
		{
		quint32 iCodeAddress;
		quint32 iCodeSymbolCount;
		quint32 iDataAddress;
		quint32 iDataSymbolCount;
		quint32 iBssAddress;
		quint32 iBssSymbolCount;
		quint32 iPcNameOffset;
		quint32 iDevNameOffset;
		quint32 iStartSymbolIndex;
		};

	struct TSymbolEntry
		{
		quint32 iAddress;
		quint32 iLength;
		quint32 iScopeNameOffset;
		quint32 iNameOffset;
		quint32 iSecNameOffset;
		};

	struct TPageInfo
		{
		quint32 iPageStartOffset;
		quint32 iPageDataSize;
		};

	class PagedDbgUnitEntryComparator
		{
	public:
		PagedDbgUnitEntryComparator(const CBsymV3* aParent, quint32 aTargetAddress) : iParent(aParent), iTargetAddress(aTargetAddress) {}
		bool operator()(TDbgUnitEntry const& t1, TDbgUnitEntry const& t2) const;
	private:
		const CBsymV3* iParent;
		quint32 iTargetAddress;
		};

	class PagedSymbolComparator
		{
	public:
		PagedSymbolComparator(const CBsymV3* aParent, quint32 aTargetAddress) : iParent(aParent), iTargetAddress(aTargetAddress) {}
		bool operator()(TSymbolEntry const& t1, TSymbolEntry const& t2) const;
	private:
		const CBsymV3* iParent;
		quint32 iTargetAddress;
		};


private:
	bool iLittleEndian;
	enum TCompressionType
		{
		ENotCompressed = 0,
		EBytepairCompressed = 1,
		};
	TCompressionType iCompression;
	// "Debug unit" is v3's name for a codeseg or other file in the rom image
	quint32 iDbgUnitOffset;
	quint32 iDbgUnitCount;
	quint32 iSymbolOffset;
	quint32 iSymbolCount;
	quint32 iStringTableOffset;
	quint32 iStringTableSize;
	quint32 iCompressedSize;
	quint32 iUncompressedSize;
	quint32 iCompressInfoOffset;

	Pager* iPager;
	QHash<QString, quint32> iCodesegNameHash; // Maps device-side names (lowercased, eg sys\bin\ekern_woopvariant.exe) to dbg unit offset
	};

#endif // BSYM_V3_H
