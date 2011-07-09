// bsym_v3.cpp
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

#include "bsym_v3.h"
#include "pager.h"

CBsymV3::CBsymV3(QObject *parent) :
	CBsymFile(parent), iLittleEndian(false), iPager(NULL)
	{
	}

CBsymV3::CBsymV3(const QString& aFileName, QObject* aParent)
	: CBsymFile(aFileName, aParent), iLittleEndian(false), iPager(NULL)
	{
	}

CBsymV3::~CBsymV3()
	{
	}

#define CHECK_WITHIN_FILE(x) do { quint32 _offset = (x); if (_offset > iFileSize) { qWarning("%s (%u) is beyond file size %u", #x, _offset, iFileSize); return false; } } while (0)
#define CHECK_WITHIN_DATA(x) do { quint32 _offset = (x); if (_offset > iUncompressedSize) { qWarning("%s (%u) is beyond uncompressed data size %u", #x, _offset, iUncompressedSize); return false; } } while (0)

bool CBsymV3::Open()
	{
	if (!iFile.open(QIODevice::ReadOnly)) return false;
	QString filename = QDir::toNativeSeparators(iFile.fileName());
	iFileSize = (int)iFile.size();

	iData = iFile.map(0, iFileSize);
	if (!iData)
		{
		qWarning("Couldn't map file %s", qPrintable(filename));
		return false;
		}

	// The basics of the header have already been validated by CBsymFile::New()
	iVersion = Uint(4);
	iLittleEndian = iData[8];
	iCompression = (TCompressionType)iData[9];

	iDbgUnitOffset = Uint(12);
	iDbgUnitCount = Uint(16);
	iSymbolOffset = Uint(20);
	iSymbolCount = Uint(24);
	iStringTableOffset = Uint(28);
	iStringTableSize = Uint(32);
	iCompressedSize = Uint(36);
	iUncompressedSize = Uint(40);
	iCompressInfoOffset = Uint(44);
	CHECK_WITHIN_FILE(iDbgUnitOffset);
	CHECK_WITHIN_FILE(iDbgUnitOffset + iCompressedSize);
	// Don't really have anything to validate iUncompressedSize against in the compressed case
	if (iCompression == ENotCompressed)
		{
		CHECK_WITHIN_FILE(iDbgUnitOffset + iUncompressedSize);
		}
	else
		{
		CHECK_WITHIN_FILE(iCompressInfoOffset);
		}
	CHECK_WITHIN_DATA(iDbgUnitCount * sizeof(TDbgUnitEntry));
	CHECK_WITHIN_DATA(iSymbolOffset);
	CHECK_WITHIN_DATA(iSymbolOffset + iSymbolCount * sizeof(TSymbolEntry));
	CHECK_WITHIN_DATA(iStringTableOffset);
	CHECK_WITHIN_DATA(iStringTableOffset + iStringTableSize);

	if (iCompression == EBytepairCompressed)
		{
		if (iCompressInfoOffset == 0) { qWarning("No Compress Info section!"); return false; }
		// Need to read compression section
		quint32 pageSize = Uint(iCompressInfoOffset);
		if (pageSize != 4096) { qWarning("Unexpected page size %d!", pageSize); return false; }
		uint pageCount = Uint(iCompressInfoOffset + 4);
		CHECK_WITHIN_FILE(iCompressInfoOffset + 8 + pageCount*8);
		QVector<Pager::TPageInfo> pages;
		pages.reserve(pageCount);
		int pageOffset = iCompressInfoOffset + 8;
		while (pageCount--)
			{
			Pager::TPageInfo page;
			page.iPageStartOffset = Uint(pageOffset);
			page.iPageDataSize = Uint(pageOffset + 4);
			pages.append(page);
			pageOffset += 8;
			}
		iPager = new Pager(pages, iData + iDbgUnitOffset, this);
		}
	else if (iCompression != ENotCompressed)
		{
		qWarning("Compression type %d not supported in %s", (int)iCompression, qPrintable(filename));
		return false;
		}

	// Useful for validity of the BSYM file is in question, too costly to run otherwise
	//ConsistancyCheck();

	// Construct the codeseg name hash
	for (uint i = 0; i < iDbgUnitCount; i++)
		{
		quint32 offset = i * sizeof(TDbgUnitEntry);
		const TDbgUnitEntry* entry = GetEntry(offset);
		QString deviceSideName = GetString(iStringTableOffset + ByteSwapIfNeeded(entry->iDevNameOffset)).toLower();
		iCodesegNameHash.insert(deviceSideName, offset);
		ReleaseData();
		}

	return true;
	}

quint32 CBsymV3::Uint(int aOffset) const
	{
	Q_ASSERT((aOffset & 3) == 0);
	ASSERT_LE((uint)aOffset+4, iFileSize);
	quint32 data = ((const quint32*)iData)[aOffset/4];
	return ByteSwapIfNeeded(data);
	}

quint32 CBsymV3::ByteSwapIfNeeded(quint32 aData) const
	{
	if (iLittleEndian)
		{
		return qToLittleEndian(aData);
		}
	else
		{
		return qToBigEndian(aData);
		}
	}

QString CBsymV3::GetString(quint32 aLocation) const
	{
	const uchar* strPtr = GetData(aLocation, 1);
	quint16 strLen = (quint16)*strPtr;
	aLocation++;
	if (strLen == 255)
		{
		ReleaseData();
		strPtr = GetData(aLocation, 2);
		strLen = iLittleEndian ? qFromLittleEndian<quint16>(strPtr) : qFromBigEndian<quint16>(strPtr);
		aLocation += 2;
		}
	ReleaseData();
	strPtr = GetData(aLocation, strLen);
	QString result = QString::fromLatin1((const char*)(strPtr), strLen);
	ReleaseData();
	return result;
	}

QString CBsymV3::SymbolName(const TSymbolEntry* aSymbol) const
	{
	quint32 nameOffset = iStringTableOffset + ByteSwapIfNeeded(aSymbol->iNameOffset);

	QString result;
	quint32 scopeOffset = iStringTableOffset + ByteSwapIfNeeded(aSymbol->iScopeNameOffset);
	if (scopeOffset)
		{
		result.append(GetString(scopeOffset)).append("::");
		}
	result.append(GetString(nameOffset));
	return result;
	}

QString CBsymV3::EntryName(const TDbgUnitEntry* aEntry) const
	{
	// This is the PC name ie \epoc32\release\....
	return GetString(iStringTableOffset + ByteSwapIfNeeded(aEntry->iPcNameOffset));
	}

const uchar* CBsymV3::GetData(quint32 aOffset, int aLength) const
	{
	if (iPager) return iPager->GetData(aOffset, aLength);
	else return iData + iDbgUnitOffset + aOffset;
	}

void CBsymV3::ReleaseData(int aNumRequests) const
	{
	if (iPager) iPager->ReleaseData(aNumRequests);
	}

// This is a rather crazy function - we use it to run the standard qLowerBound function over our TDbgUnitEntry array
// even though the TDbgUnitEntrys are potentially not decompressed yet and in the wrong endianness to boot
// We're not dealing with real TDbgUnitEntry references (or pointers) even though qLowerBound (via
// QVector<TDbgUnitEntry>::const_iterator) thinks we are.
bool CBsymV3::PagedDbgUnitEntryComparator::operator()(TDbgUnitEntry const& t1, TDbgUnitEntry const& t2) const
	{
	quint32 idx1 = (quint32)&t1;
	quint32 idx2 = (quint32)&t2;
	if (idx1 == 0xFFFFFFFF)
		{
		idx1 = iTargetAddress;
		}
	else
		{
		const TDbgUnitEntry* entry = iParent->GetEntry(idx1);
		idx1 = entry->iCodeAddress;
		if (idx1 == 0) idx1 = entry->iDataAddress; // v3 puts non-code files in place but puts their data address in iDataAddress. Don't really like this...
		idx1 = iParent->ByteSwapIfNeeded(idx1);
		Q_ASSERT(idx1);
		iParent->ReleaseData();
		}

	if (idx2 == 0xFFFFFFFF)
		{
		idx2 = iTargetAddress;
		}
	else
		{
		const TDbgUnitEntry* entry = iParent->GetEntry(idx2);
		idx2 = entry->iCodeAddress;
		if (idx2 == 0) idx2 = entry->iDataAddress; // v3 puts non-code files in place but puts their data address in iDataAddress. Don't really like this...
		idx2 = iParent->ByteSwapIfNeeded(idx2);
		Q_ASSERT(idx2);
		iParent->ReleaseData();
		}

	return idx1 < idx2;
	}

bool CBsymV3::PagedSymbolComparator::operator()(TSymbolEntry const& t1, TSymbolEntry const& t2) const
	{
	quint32 idx1 = (quint32)&t1;
	quint32 idx2 = (quint32)&t2;
	if (idx1 == 0xFFFFFFFF)
		{
		idx1 = iTargetAddress;
		}
	else
		{
		idx1 = iParent->ByteSwapIfNeeded(iParent->GetSymbol(iParent->iSymbolOffset + idx1)->iAddress);
		iParent->ReleaseData();
		}

	if (idx2 == 0xFFFFFFFF)
		{
		idx2 = iTargetAddress;
		}
	else
		{
		idx2 = iParent->ByteSwapIfNeeded(iParent->GetSymbol(iParent->iSymbolOffset + idx2)->iAddress);
		iParent->ReleaseData();
		}

	return idx1 < idx2;
	};

quint32 CBsymV3::CodeLength(const TDbgUnitEntry* aEntry) const
	{
	quint32 symbolCount = ByteSwapIfNeeded(aEntry->iCodeSymbolCount);
	if (symbolCount)
		{
		quint32 startAddress = ByteSwapIfNeeded(aEntry->iCodeAddress);
		quint32 lastSymbolOffset = iSymbolOffset + (ByteSwapIfNeeded(aEntry->iStartSymbolIndex) + symbolCount - 1) * sizeof(TSymbolEntry);
		const TSymbolEntry* lastSymbol = GetSymbol(lastSymbolOffset);
		quint32 len = ByteSwapIfNeeded(lastSymbol->iAddress) + ByteSwapIfNeeded(lastSymbol->iLength) - startAddress;
		ReleaseData(); // lastSymbol
		return len;
		}
	else
		{
		return 0;
		}
	}

TLookupResult CBsymV3::LookupRomSymbol(quint32 aAddress) const
	{
	// In terms of cache efficiency it's probably better to find codeseg first then search within that.
	// Equally, if this is a rofs bsym or a hybrid then the symbols aren't globally sorted so we have
	// to do it this way anyway

	quint32 address = aAddress & 0xFFFFFFFE; // Mask bottom bit as it just indicates thumb mode

	// This is ever so slightly crazy. The 'pointers' passed to qUpperBound are acting as poor man's iterator
	// into the uncompressed TDbgUnitEntry array. The fact that they aren't actual valid pointers is ok because
	// PagedDbgUnitEntryComparator knows how to decode them, as indexes into the decompressed data.
	// We just have to be careful never to dereference them directly
	// ... At some point I really need to write a proper iterator and stop fighting the language...

	static const TDbgUnitEntry* KMagicTargetKey = (const TDbgUnitEntry*)0xFFFFFFFF;
	TPagedDbgUnitEntry entry = NULL;
	TPagedDbgUnitEntry endEntry = entry + iDbgUnitCount;

	entry = qUpperBound(entry, endEntry, *KMagicTargetKey, PagedDbgUnitEntryComparator(this, address));
	if (entry != NULL) entry--;

	const TDbgUnitEntry* realEntry = GetEntry(entry);
	quint32 codeAddress = ByteSwapIfNeeded(realEntry->iCodeAddress);
	TLookupResult result;
	if (address >= codeAddress && address < codeAddress + CodeLength(realEntry))
		{
		result = DoCodesegLookup(realEntry, address - codeAddress);
		}

	ReleaseData(); // realEntry
	return result;
	}

TLookupResult CBsymV3::Lookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const
	{
	// Find codeseg
	if (!aCodesegName.startsWith("z:\\", Qt::CaseInsensitive))
		{
		return TLookupResult(); // Currently we don't handle having bsyms for non ROM/ROFS code
		}

	QString codeseg = aCodesegName.mid(3).toLower();
	if (!iCodesegNameHash.contains(codeseg))
		{
		// We don't know what it is
		return TLookupResult();
		}

	const TDbgUnitEntry* entry = GetEntry(iCodesegNameHash.value(codeseg));
	TLookupResult result = DoCodesegLookup(entry, aCodesegRelativeOffset);
	ReleaseData(); // entry
	return result;
	}

TLookupResult CBsymV3::DoCodesegLookup(const TDbgUnitEntry* aEntry, quint32 aCodesegRelativeOffset) const
	{
	quint32 codesegAddress = ByteSwapIfNeeded(aEntry->iCodeAddress);
	quint32 addressToLookFor = codesegAddress + aCodesegRelativeOffset;
	TPagedSymbolEntry firstSymbol = NULL; firstSymbol += ByteSwapIfNeeded(aEntry->iStartSymbolIndex);
	TPagedSymbolEntry endSymbol = firstSymbol + ByteSwapIfNeeded(aEntry->iCodeSymbolCount);

	static const TSymbolEntry* KMagicTargetKey = (const TSymbolEntry*)0xFFFFFFFF;
	TPagedSymbolEntry symbol = qUpperBound(firstSymbol, endSymbol, *KMagicTargetKey, PagedSymbolComparator(this, addressToLookFor));
	if (symbol != firstSymbol) symbol--;

	const TSymbolEntry* realSymbol = GetSymbol(symbol);
	while (realSymbol->iLength == 0 && symbol != endSymbol)
		{
		// BSYMv3 doesn't trim empty symbols, sigh
		ReleaseData();
		symbol++;
		realSymbol = GetSymbol(symbol);
		}
	quint32 symbolAddress = ByteSwapIfNeeded(realSymbol->iAddress);
	quint32 symbolLength = ByteSwapIfNeeded(realSymbol->iLength);

	quint32 symbolCodesegOffset = symbolAddress - codesegAddress;
	TLookupResult result;
	if (aCodesegRelativeOffset >= symbolCodesegOffset && aCodesegRelativeOffset < symbolCodesegOffset + symbolLength)
		{
		result = TLookupResult(symbolAddress, addressToLookFor - symbolAddress, SymbolName(realSymbol), EntryName(aEntry));
		}
	else
		{
		result = TLookupResult(codesegAddress, addressToLookFor - codesegAddress, QString(), EntryName(aEntry));
		}

	ReleaseData(); // realSymbol

	return result;
	}

quint32 CBsymV3::RomChecksum() const
	{
	return Uint(48);
	}

void CBsymV3::ConsistancyCheck() const
	{
	// Do some tests that are too costly to run every time, but absolutely must be true for the file to be used sensibly

	TPagedDbgUnitEntry entry = NULL;

	// Check the dbgunits are strictly ordered (actually they can be <=, since all rofs codesegs have codeaddr zero. Ignoring for now!
	PagedDbgUnitEntryComparator lessThan(this, 0);
	for (uint i = 1; i < iDbgUnitCount; i++)
		{
		bool ok = lessThan(entry[i-1], entry[i]);
		if (!ok)
			{
			const TDbgUnitEntry* e1 = GetEntry(entry + i - 1);
			const TDbgUnitEntry* e2 = GetEntry(entry + i - 1);
			qCritical("codeseg %d: codeaddr=0x%08x dataaddr=0x%08x", i-1, ByteSwapIfNeeded(e1->iCodeAddress), ByteSwapIfNeeded(e1->iDataAddress));
			qCritical("codeseg %d: codeaddr=0x%08x dataaddr=0x%08x", i, ByteSwapIfNeeded(e2->iCodeAddress), ByteSwapIfNeeded(e2->iDataAddress));
			qCritical("codeseg %d is not less than codeseg %d!", i-1, i);
			ReleaseData(2);
			//return;
			}
		}

	// do the same thing for symbols. They must be strictly ordered within their parent codeseg
	for (uint i = 0; i < iDbgUnitCount; i++)
		{
		const TDbgUnitEntry* entry = GetEntry(i * sizeof(TDbgUnitEntry));
		uint startSymbolIdx = ByteSwapIfNeeded(entry->iStartSymbolIndex);
		uint symbolcount = ByteSwapIfNeeded(entry->iCodeSymbolCount);
		for (uint j = 1; j < symbolcount; j++)
			{
			const TSymbolEntry* e1 = GetSymbol(iSymbolOffset + (startSymbolIdx + j - 1) * sizeof(TSymbolEntry));
			const TSymbolEntry* e2 = GetSymbol(iSymbolOffset + (startSymbolIdx + j) * sizeof(TSymbolEntry));
			quint32 a1 = ByteSwapIfNeeded(e1->iAddress);
			quint32 a2 = ByteSwapIfNeeded(e2->iAddress);
			if (a1 > a2)
				{
				qCritical("symbol %d: addr=0x%08x", j-1, a1);
				qCritical("symbol %d: addr=0x%08x", j, a2);
				qCritical("symbol %d is not less than symbol %d in codeseg %d %s!", j-1, j, i, qPrintable(EntryName(entry)));
				}
			ReleaseData(2);
			}
		}
	}

#if 0

void Write(QDataStream& aOut, QByteArray& aInput, QByteArray& aTempOutputBuf)
{
}

bool CBsymV3::WriteBsym(CBsymFile::TVersion aVersion, const QString& aFileName, const QVector<CSymbolics::CodeSeg>& aCodeSegs, int aSymbolCount, const QHash<QString,QString>& aRenamedBinaries, quint32 aRomChecksum)
	{
	int progress = 0;
	const int maxProgress = 2*aCodeSegs.count() + 2*aSymbolCount;
	emit SetProgressMaximum(maxProgress);
//	DECLARE_ENTITIES;

	// Figure out string table
	quint32 stringTableLen = 0;

	QSet<QString> prefixes;
	QSet<QString> sectionNames;
	foreach (const CSymbolics::CodeSeg& codeseg, aCodeSegs)
		{
		stringTableLen += StringTableLen(codeseg.iFileName);
		stringTableLen += String
		foreach (const CSymbolics::Symbol& symbol, codeseg.iSymbols)
			{
			QString name = symbol.Name();
			int colon = name.lastIndexOf("::");
			if (colon != -1)
				{
				QString prefix = name.left(colon);
				if (!prefixes.contains())
					{
					stringTableLen += StringTableLen(prefix.length());
					prefixes.insert(prefix);
					}
				}
			QString sectionName = symbol.SectionName();
			if (!sectionNames.contains(sectionName))
				{
				stringTableLen += StringTableLen(sectionName);
				sectionNames.insert(sectionName);
				}
			}
		}




	QString fileName(aFileName);
	if (fileName.endsWith(".symbol", Qt::CaseInsensitive)) fileName.chop(7);
	if (!fileName.endsWith(".bsym", Qt::CaseInsensitive)) fileName.append(".bsym");

	QFile file(fileName);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
		return false;
		}


	QDataStream stream(&file);
	stream.setByteOrder(QDataStream::LittleEndian);

	static const QString KColons("::");
	quint32 unitOffset = 13*4; // Need to add size of compress section too once we've calculated it
	quint32 unitLen = aCodeSegs.count() * sizeof(TDbgUnitEntry);
	quint32 symbolLen = aSymbolCount * sizeof(TSymbolEntry);



	quint32 symbolOffset = unitOffset + unitLen;
	quint32 stringOffset = symbolOffset + symbolLen;


	const quint32 codesegLen = 4 + aCodeSegs.count() * sizeof(TCodeSeg);
	const quint32 symbolOffset = codesegOffset + codesegLen;
	const quint32 symbolLen = 4 + aSymbolCount * sizeof(TSymbol);
	const quint32 tokenOffset = symbolOffset + symbolLen;
	quint32 tokenLen = 0;
	if (version >= EVersion2_0) tokenLen = 4 + KTokenCount * 4;
	const quint32 renameOffset = tokenOffset + tokenLen;
	quint32 renameLen = 0;
	if (version >= EVersion2_1) renameLen = 4 + renames.count() * 8;
	const quint32 stringOffset = renameOffset + renameLen;
	quint32 currentSymbolIndex = 0;
	quint32 currentStringOffset = stringOffset;

	// Header
	stream << KMagic;
	stream << version;
	stream << codesegOffset;
	stream << symbolOffset;
	if (version >= EVersion2_0) stream << tokenOffset;
	if (version >= EVersion2_1) stream << renameOffset;
	if (version >= EVersion2_2) stream << aRomChecksum;

	// Codeseg section
	stream << aCodeSegs.count();
	foreach(const CSymbolics::CodeSeg& codeseg, aCodeSegs)
		{
		if ((progress & 0xFFFF) == 0) emit Progress(progress);
		stream << codeseg.Address();
		stream << codeseg.iSymbols.count();
		stream << currentStringOffset;
		RECORD_ENTITY(codeseg.iFileName.constData(), currentStringOffset);
		currentStringOffset += StringTableLen(version, codeseg.iFileName.toLower());
		stream << currentSymbolIndex;
		currentSymbolIndex += codeseg.iSymbols.count();

		// Calculate any prefixes
		QStringList prefixes;
		foreach(const CSymbolics::Symbol& symbol, codeseg.iSymbols)
			{
			int pos = symbol.Name().lastIndexOf(KColons);
			if (pos != -1)
				{
				QString prefix = symbol.Name().left(pos);
				int idx = prefixes.indexOf(prefix);
				if (idx == -1)
					{
					idx = prefixes.count();
					prefixes.append(prefix);
					}
				symbol.iSpare = idx+1;
				}
			if (prefixes.count() == 65534) break; // This is the max number of prefixes we can have per code segment, because we only have 16 bits (less the zero) spare in TSymbol::iData[1]
			}
		if (prefixes.count())
			{
			//qDebug("codeseg %s's prefix table is at %d", qPrintable(codeseg.iFileName), currentStringOffset);
			codeseg.iSpare = new QStringList(prefixes);
			RECORD_ENTITY(codeseg.iSpare, currentStringOffset);
			stream << currentStringOffset;
			currentStringOffset += prefixes.count() * sizeof(quint32);
			// Prefix table strings now go immediately after the prefix table, for sanity of implementation
			foreach (const QString& prefix, *static_cast<QStringList*>(codeseg.iSpare))
				{
				RECORD_ENTITY(prefix.constData(), currentStringOffset);
				currentStringOffset += StringTableLen(version, prefix);
				}
			}
		else
			{
			stream << 0;
			}
		progress++;
		}

	// Symbols section
	ASSERT_EQ(file.pos(), symbolOffset); // Otherwise we've miscalculated
	ASSERT_EQ(currentSymbolIndex, (quint32)aSymbolCount); // Otherwise we've miscalculated
	stream << aSymbolCount;
	foreach(const CSymbolics::CodeSeg& codeseg, aCodeSegs)
		{
		foreach(const CSymbolics::Symbol& symbol, codeseg.iSymbols)
			{
			if ((progress & 0xFFFF) == 0) emit Progress(progress);
			stream << symbol.iAddress;
			quint16 prefixIdx = symbol.iSpare;
			stream << (symbol.iLength | (prefixIdx << 16));
			stream << currentStringOffset;
			RECORD_ENTITY(&symbol, currentStringOffset);
			quint32 nameLen = StringTableLen(version, symbol.Name());
			if (prefixIdx)
				{
				const QString& prefix = static_cast<QStringList*>(codeseg.iSpare)->at(prefixIdx-1);
				nameLen = StringTableLen(version, symbol.Name().mid(prefix.length() + KColons.length()));
				}
			currentStringOffset += nameLen;
			progress++;
			}
		}

	// token section
	if (version >= EVersion2_0)
		{
		ASSERT_EQ(file.pos(), tokenOffset);
		stream << KTokenCount;
		for (int i = 0; i < KTokenCount; i++)
			{
			const char* token = Tokens()[i];
			RECORD_ENTITY(token, currentStringOffset);
			stream << currentStringOffset;
			int len = qstrlen(token);
			currentStringOffset += StringTableLen(len);
			}
		}

	// renames section
	if (version >= EVersion2_1)
		{
		ASSERT_EQ(file.pos(), renameOffset);
		stream << renames.count();
		QMapIterator<int, QString> iter(renames);
		while (iter.hasNext())
			{
			//RECORD_ENTITY(key.constData(), currentStringOffset);
			stream << iter.key();
			const QString& val = iter.value();
			RECORD_ENTITY(val.constData(), currentStringOffset);
			stream << currentStringOffset;
			currentStringOffset += StringTableLen(version, val);
			}
		}

	ASSERT_EQ(file.pos(), stringOffset); // Otherwise we've miscalculated
	const quint32 finalStringOffset = currentStringOffset;

	// Strings section...
	foreach(const CSymbolics::CodeSeg& codeseg, aCodeSegs)
		{
		if ((progress & 0xFFFF) == 0) emit Progress(progress);
		CHECK_ENTITY(codeseg.iFileName.constData(), file.pos(), "codeseg name %s", qPrintable(codeseg.iFileName));
		WriteString(stream, version, codeseg.iFileName.toLower());
		// ...and prefix tables
		if (codeseg.iSpare)
			{
			QStringList* prefixes = static_cast<QStringList*>(codeseg.iSpare);
			CHECK_ENTITY(prefixes, file.pos(), "prefix table for %s", qPrintable(codeseg.iFileName));
			quint32 prefixTableStart = file.pos();
			quint32 prefixStringOffset = prefixTableStart + prefixes->count() * sizeof(quint32);
			// Prefix strings go straight after this table
			foreach (const QString& string, *prefixes)
				{
				stream << prefixStringOffset;
				prefixStringOffset += StringTableLen(version, string);
				}
			// Now the prefixes themselves
			int idx = 0;
			foreach (const QString& prefix, *prefixes)
				{
				CHECK_ENTITY(prefix.constData(), file.pos(), "prefix %d '%s' for codeseg %s", idx, qPrintable(prefix), qPrintable(codeseg.iFileName));
				WriteString(stream, version, prefix);
				idx++;
				}
			}
		progress++;
		}
	foreach(const CSymbolics::CodeSeg& codeseg, aCodeSegs)
		{
		foreach(const CSymbolics::Symbol& symbol, codeseg.iSymbols)
			{
			if ((progress & 0xFFFF) == 0) emit Progress(progress);
			if (symbol.iSpare)
				{
				const QString& prefix = static_cast<QStringList*>(codeseg.iSpare)->at(symbol.iSpare-1);
				CHECK_ENTITY(&symbol, file.pos(), "symbol name %s", qPrintable(symbol.Name()));
				WriteString(stream, version, symbol.Name().mid(prefix.length() + KColons.length()));
				}
			else
				{
				WriteString(stream, version, symbol.Name());
				}
			progress++;
			}
		}

	// Now we're done with the codeseg prefix tables, clean em up
	foreach(const CSymbolics::CodeSeg& codeseg, aCodeSegs)
		{
		QStringList* prefixes = static_cast<QStringList*>(codeseg.iSpare);
		delete prefixes;
		codeseg.iSpare = NULL;
		}

	// Finally, the tokens
	if (version >= EVersion2_0)
		{
		for (int i = 0; i < KTokenCount; i++)
			{
			const char* token = Tokens()[i];
			CHECK_ENTITY(token, file.pos(), "token %d '%s'", i, token);
			QString tokenString(token);
			// Make sure we convert the tokenString to a QByteArray ourselves - otherwise WriteString will replace the whole thing with its own token!
			WriteString(stream, tokenString.toLatin1());
			}
		}

	// Finally finally, the renames
	if (version >= EVersion2_1)
		{
		QMapIterator<int, QString> iter(renames);
		while (iter.hasNext())
			{
			const QString& val = iter.value();
			CHECK_ENTITY(val.constData(), file.pos(), "rename %s -> %s", qPrintable(aCodeSegs[iter.key()].iFileName), qPrintable(val));
			WriteString(stream, version, val);
			}
		}

	ASSERT_EQ(finalStringOffset, currentStringOffset); // Otherwise something in the strings section changed the currentStringOffset, which is a big no-no
	ASSERT_EQ(file.pos(), currentStringOffset); // Otherwise we've miscalculated
	file.close();

	emit Progress(progress); // One last progress notification
	iFile.setFileName(fileName);
	return Open();
	}

#endif
