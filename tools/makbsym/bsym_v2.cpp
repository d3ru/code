// bsym_v2.cpp
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

#include "bsym_v2.h"

//#define BSYM_ENTITY_TRACKING
// This macro controls whether we track the offsets where things get written in the BSYM file, for debugging
// purposes. RECORD_ENTITY is used whererever an offset to something is put into the file,
// the CHECK_ENTITY macro is then used to check that when that thing actually gets written into the file,
// it is at the offset that was expected when RECORD_ENTITY was called.
#ifdef BSYM_ENTITY_TRACKING
#define DECLARE_ENTITIES QHash<const void*, quint32> bsymEntities
void RecordEntity(QHash<const void*, quint32>& bsymEntities, const void* entity, quint32 addr)
	{
	bsymEntities.insert(entity, addr);
	}
#define RECORD_ENTITY(entity, addr) RecordEntity(bsymEntities, entity, addr);
void CheckEntity(QHash<const void*, quint32>& bsymEntities, const void* entity, quint32 position, const char* description, ...)
	{
	quint32 addr = bsymEntities.value(entity);
	if (addr != position)
		{
		//qDebug(description);
		// Have to unwind qDebug cos there's no va_args version
		QString buf;
		va_list ap;
		va_start(ap, description);
		buf.vsprintf(description, ap);
		va_end(ap);
		qt_message_output(QtDebugMsg, buf.toLocal8Bit().constData());

		ASSERT_EQ(addr, position);
		}
	}
#define CHECK_ENTITY(entity, position, description...) CheckEntity(bsymEntities, entity, position, description);
#else
#define DECLARE_ENTITIES
#define RECORD_ENTITY(entity, addr)
#define CHECK_ENTITY(entity, position, ...)
#endif

char const*const*const Tokens();
extern const int KTokenCount;

extern bool gSymbolLookupDebug;

CBsymV2::CBsymV2(QObject *parent)
	: CBsymFile(parent), iCodesegOffset(0), iSymbolsOffset(0), iTokensOffset(0)
	{
	}

CBsymV2::CBsymV2(const QString& aFileName, QObject* aParent)
	: CBsymFile(aFileName, aParent), iCodesegOffset(0), iSymbolsOffset(0), iTokensOffset(0)
	{
	}

bool CBsymV2::Open()
	{
	if (!iFile.open(QIODevice::ReadOnly)) return false;
	iFileSize = (int)iFile.size();
	QString filename = QDir::toNativeSeparators(iFile.fileName());

	iData = iFile.map(0, iFileSize);
	if (!iData)
		{
		qWarning("Couldn't map file %s", qPrintable(filename));
		return false;
		}

	// The basics of the header have already been validated by CBsymFile::New()
	iVersion = Uint(4);
	iCodesegOffset = Uint(8);
	iSymbolsOffset = Uint(0x0C);

	quint32 majorVersion = (iVersion&KMajorVersion);
	if (majorVersion != EVersion1_0 && majorVersion != EVersion2_0)
		{
		qWarning("Unsupported bsym version %d in %s", majorVersion>>16, qPrintable(filename));
		return false;
		}

	if (iVersion >= EVersion2_0)
		{
		iTokensOffset = Uint(0x10);
		if (iTokensOffset + 4 > iFileSize || TokenCount() > 128 || iTokensOffset + 4 + TokenCount()*4 > iFileSize)
			{
			qWarning("Token offset %x or count %x bad", iTokensOffset, TokenCount());
			return false;
			}
		}

	if (iCodesegOffset+4 > iFileSize || iCodesegOffset + CodesegSectionLength() > iFileSize)
		{
		qWarning("Code offset %x or len %x bad", iCodesegOffset, CodesegSectionLength());
		return false;
		}
	if (iSymbolsOffset+4 > iFileSize || iSymbolsOffset + SymbolsSectionLength() > iFileSize)
		{
		qWarning("Symbols offset %x or len %x bad", iSymbolsOffset, SymbolsSectionLength());
		return false;
		}

	// Finally, construct the codeseg hash

	if (iVersion >= EVersion2_1)
		{
		// ... making use of the renames section
		quint32 renamesSection = Uint(20);
		int renameCount = (renamesSection == 0) ? 0 : Uint(renamesSection);
		for (int i = 0; i < renameCount; i++)
			{
			quint32 codesegIdx = Uint(renamesSection + 4 + i*8);
			quint32 nameOffset = Uint(renamesSection + 4 + i*8 + 4);
			QString name = GetString(nameOffset);
			if (iVersion < EVersion2_3) name.prepend("z:\\sys\\bin\\"); // As per spec prior to v2.3 the name didn't include the path
			const TCodeSeg* codeseg = CodeSegs() + codesegIdx;
			iFileNameToCodeSeg.insert(name, codeseg);
			}
		}

	//Dump(); // DEBUG

	// If we didn't have a renames section, or it didn't have every binary in it, scan the codesegs too
	const TCodeSeg* c = CodeSegs();
	const TCodeSeg* end = c + CodeSegCount();
	while (c < end)
		{
		QString name = SymbolName(c);
		int lastSlash = name.lastIndexOf('\\'); // Might get forward or backward slashes, depending on what platform the bsym was created on
		if (lastSlash == -1) lastSlash = name.lastIndexOf('/');
		name = name.mid(lastSlash + 1); // Remove the \epoc32\release\armv5\urel
		// Don't prepend z:\sys\bin\ - without rename info we've no idea if this ended up in ROM or on the C drive
		if (!iFileNameToCodeSeg.contains(name))
			{
			iFileNameToCodeSeg.insert(name, c);
			}
		c++;
		}

	//DumpCodeSeg(iFileNameToCodeSeg.value("z:\\sys\\bin\\phoneinfoserver.exe"));
	return true;
	}

quint32 CBsymV2::Uint(int aOffset) const
	{
	Q_ASSERT((aOffset & 3) == 0);
	ASSERT_LE((uint)aOffset+4, iFileSize);
	return qFromBigEndian(((const quint32*)iData)[aOffset/4]);
	}

const CBsymV2::TCodeSeg* CBsymV2::CodeSegs() const
	{
	return reinterpret_cast<const TCodeSeg*>(iData + iCodesegOffset + 4);
	}

const CBsymV2::TSymbol* CBsymV2::Symbols() const
	{
	return reinterpret_cast<const TSymbol*>(iData + iSymbolsOffset + 4);
	}

quint32 CBsymV2::CodesegSectionLength() const
	{
	return 4 + CodeSegCount() * sizeof(TCodeSeg);
	}

quint32 CBsymV2::SymbolsSectionLength() const
	{
	return 4 + SymbolCount() * sizeof(TSymbol);
	}

int CBsymV2::SymbolCount() const
	{
	return Uint(iSymbolsOffset);
	}

int CBsymV2::CodeSegCount() const
	{
	return Uint(iCodesegOffset);
	}

quint32 CBsymV2::CodeSegLength(const TCodeSeg* aCodeSeg) const
	{
	if (aCodeSeg->SymbolCount())
		{
		const TSymbol& lastSymbol = Symbols()[aCodeSeg->SymbolStart() + aCodeSeg->SymbolCount()-1];
		return lastSymbol.Address() + lastSymbol.Length() - aCodeSeg->Address();
		}
	else
		{
		return 0;
		}
	}

CBsymV2::TCodeAndSym CBsymV2::DoLookup(quint32 aAddress) const
	{
	quint32 address = aAddress & 0xFFFFFFFE; // Mask bottom bit as it just indicates thumb mode

	// Find codeseg (need to know this if the symbol has a name prefix)
	TCodeSeg tempSeg(address);
	QVector<TCodeSeg>::const_iterator codeiter = qUpperBound(CodeSegs(), CodeSegs() + CodeSegCount(), tempSeg);
	if (codeiter != CodeSegs()) codeiter--;
	const TCodeSeg& codeseg = *codeiter;

	// With just an address we should only match ROM symbols, ie ones where the codeseg address is non-zero
	// Therefore don't allow a codeseg with zero address to match - a big enough DLL in ROFS could overlap with a valid range
	if (codeseg.Address() != 0 && address >= codeseg.Address() && address < codeseg.Address() + CodeSegLength(&codeseg))
		{
		return DoCodesegLookup(codeseg, address - codeseg.Address());
		}
	return TCodeAndSym(NULL, NULL);
	}

const CBsymV2::TCodeSeg* CBsymV2::FindCodesegForName(const QString& aCodesegName) const
	{
	// Find the codeseg - aCodesegName is of the form Z:\sys\bin\whatever.dll
	if (gSymbolLookupDebug) qDebug("FindCodesegForName(%s)", qPrintable(aCodesegName));
	const TCodeSeg* codeseg = iFileNameToCodeSeg.value(aCodesegName.toLower());
	if (gSymbolLookupDebug) qDebug("Lookup of %s returned codeseg %s", qPrintable(aCodesegName.toLower()), codeseg ? qPrintable(SymbolName(codeseg)) : "NULL");
	if (codeseg) return codeseg;
	// Also try looking up just "whatever.dll" - depending on how the bsym was created the exact path of the binary might not have been known - we should give it the benefit of the doubt
	QString binName = aCodesegName.mid(aCodesegName.lastIndexOf('\\')+1).toLower();
	codeseg = iFileNameToCodeSeg.value(binName);
	if (gSymbolLookupDebug) qDebug("Lookup of %s returned codeseg %s", qPrintable(binName), codeseg ? qPrintable(SymbolName(codeseg)) : "NULL");
	if (codeseg) return codeseg;

	if (iVersion < EVersion2_1)
		{
		// Try and guess - check if we've already cached the correct name
		codeseg = iFileNameToCodeSeg.value(iCodesegNameToFileNameCache.value(binName));
		if (gSymbolLookupDebug) qDebug("Lookup of %s returned codeseg %s from the guess cache", qPrintable(aCodesegName.toLower()), codeseg ? qPrintable(SymbolName(codeseg)) : "NULL");
		if (codeseg) return codeseg;

		// Finally, hunt for it by looking at all codesegs that look about right
		QStringList possibleNames;
		QString binBase = QFileInfo(binName).baseName().append('_');
		foreach (const QString& str, iFileNameToCodeSeg.keys())
			{
			if (str.startsWith(binBase)) possibleNames.append(str); // For thingy.dll -> thingy_variant.dll
			else if (str.endsWith(binName)) possibleNames.append(str); // For thingy.dll -> variant_thingy.dll
			}
		if (possibleNames.count() > 1)
			{
			qWarning("Multiple possible codesegs found for name %s: %s", qPrintable(binName), qPrintable(possibleNames.join(", ")));
			}
		if (possibleNames.count())
			{
			const QString& name = possibleNames[0];
			iCodesegNameToFileNameCache.insert(aCodesegName.toLower(), name);
			if (gSymbolLookupDebug) qDebug("Lookup of %s guessing this means %s", qPrintable(aCodesegName.toLower()), qPrintable(name));
			return iFileNameToCodeSeg.value(name);
			}
		}
	if (gSymbolLookupDebug) qDebug("BSYM %s couldn't find codeseg for %s", qPrintable(Filename()), qPrintable(aCodesegName));
	return NULL;
	}

CBsymV2::TCodeAndSym CBsymV2::DoLookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const
	{
	const TCodeSeg* codeseg = FindCodesegForName(aCodesegName);
	if (codeseg) return DoCodesegLookup(*codeseg, aCodesegRelativeOffset);
	else return TCodeAndSym(NULL, NULL);
	}

CBsymV2::TCodeAndSym CBsymV2::DoCodesegLookup(const TCodeSeg& aCodeSeg, quint32 aCodesegRelativeOffset) const
	{
	TSymbol temp(aCodeSeg.Address() + aCodesegRelativeOffset);
	const TSymbol* firstSymbol = &Symbols()[aCodeSeg.SymbolStart()];
	const TSymbol* endSymbol = firstSymbol + aCodeSeg.SymbolCount();
	QVector<TSymbol>::const_iterator iter = qUpperBound(firstSymbol, endSymbol, temp);
	if (iter != firstSymbol) iter--;
	const TSymbol& symbol = *iter;
	quint32 symbolCodesegOffset = symbol.Address() - aCodeSeg.Address();
	if (aCodesegRelativeOffset >= symbolCodesegOffset && aCodesegRelativeOffset < symbolCodesegOffset + symbol.Length())
		return TCodeAndSym(&aCodeSeg, &symbol);
	else if (aCodesegRelativeOffset >= symbolCodesegOffset && iter+1 != endSymbol && aCodesegRelativeOffset < (iter[1].Address() - aCodeSeg.Address()))
		{
		// We also allow addresses outside of the symbol, cos QT symbol files seem to be full of them.
		// ConvertSymbol (below) is responsible for setting the iFuzzy flag.
		//qDebug("DoCodesegLookup with aAddress %08x not within any symbol in aCodeSeg %s", aAddress, qPrintable(SymbolName(&aCodeSeg)));
		return TCodeAndSym(&aCodeSeg, &symbol);
		}
	return TCodeAndSym(&aCodeSeg, NULL);
	}

TLookupResult CBsymV2::LookupRomSymbol(quint32 aAddress) const
	{
	QPair<const TCodeSeg*, const TSymbol*> sym = DoLookup(aAddress);
	if (sym.first == NULL)
		{
		if (gSymbolLookupDebug) qDebug("No ROM codeseg found for 0x%08x", aAddress);
		return TLookupResult();
		}
	else if (sym.first->Address() == 0)
		{
		if (gSymbolLookupDebug) qDebug("Codeseg found for 0x%08x during LookupRomSymbol but it wasn't a ROM codeseg", aAddress);
		return TLookupResult(); // Isn't a rom symbol - if we don't return false here we'll get false positives if someone tries looking up a small number
		}

	quint32 offsetInSymbol = 0;
	if (sym.second != NULL) offsetInSymbol = aAddress - sym.second->Address();
	else offsetInSymbol = aAddress - sym.first->Address();

	if (gSymbolLookupDebug) qDebug("Found in codeseg %s of ROM section of BSYM %s", qPrintable(SymbolName(sym.first)), qPrintable(Filename()));
	return ConvertSymbol(sym, offsetInSymbol);
	}

TLookupResult CBsymV2::ConvertSymbol(const TCodeAndSym& aSymbol, quint32 aOffsetInSymbol) const
	{
	if (aSymbol.first == NULL) return TLookupResult();
	else if (aSymbol.second == NULL)
		{
		return TLookupResult(aSymbol.first->Address(), aOffsetInSymbol&0xFFFFFFFE, QString(), SymbolName(aSymbol.first, NULL), TLookupResult::EFoundCodeseg);
		}
	else
		{
		TLookupResult::TAccuracy accuracy = TLookupResult::EFoundSymbol;
		if (aOffsetInSymbol >= aSymbol.second->Length())
			{
			// Check if the address is actually outside the symbol
			accuracy = TLookupResult::EBeyondSymbol;
			}

		TLookupResult result = TLookupResult(aSymbol.second->Address(), aOffsetInSymbol&0xFFFFFFFE, SymbolName(aSymbol.second, aSymbol.first), SymbolName(aSymbol.first, NULL), accuracy);
		return result;
		}
	}

TLookupResult CBsymV2::Lookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const
	{
	QPair<const TCodeSeg*, const TSymbol*> sym = DoLookup(aCodesegName, aCodesegRelativeOffset);
	return ConvertSymbol(sym, aCodesegRelativeOffset - (sym.second ? sym.second->Address() - sym.first->Address() : 0));
	}


bool CBsymV2::TSymbol::operator<(const CBsymV2::TSymbol& other) const
	{
	return Address() < other.Address();
	}

QByteArray TokeniseString(const QString& aString)
	{
	QByteArray result(aString.toLatin1());
	for (int i = KTokenCount - 1; i >= 0; i--)
		{
		const char* token = Tokens()[i];
		uchar subst = 128+i;
		result.replace(token, qstrlen(token), (char*)&subst, 1);
		}
	return result;
	}

quint32 CBsymV2::StringTableLen(int aVersion, const QString& aString)
	{
	if (aVersion >= CBsymFile::EVersion2_0)
		{
		return StringTableLen(TokeniseString(aString).size());
		}
	else
		{
		return StringTableLen(aString.length());
		}
	}

bool CBsymV2::WriteBsym(const QString& aFileName, const QVector<CSymbolics::CodeSeg>& aCodeSegs, int aSymbolCount, const QHash<QString,QString>& aRenamedBinaries, quint32 aRomChecksum)
	{
	int progress = 0;
	const int maxProgress = 2*aCodeSegs.count() + 2*aSymbolCount;
	emit SetProgressMaximum(maxProgress);
	DECLARE_ENTITIES;
	const TVersion version = EVersion2_3; // The version to write out - change this to force older format to be used

	QString fileName(aFileName);
	if (fileName.endsWith(".symbol")) fileName.chop(7);
	if (!fileName.endsWith(".bsym")) fileName.append(".bsym");

	QFile file(fileName);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
		return false;
		}

	// We have to calculate renames early on otherwise we won't know how big the renames section will be (because aRenamedBinaries can contain non-executable files so the rename section is not necessarily the same size)
	QList<QPair<int, QString> > renames;// mapping of codeseg index to on-device name
	if (version >= EVersion2_1)
		{
		// Associate the renamed binaries with codesegs, and sort them using the ordering of aCodeSegs
		QMultiHash<QString, QString> reverseRenameHash; // Mapping codeseg name to potentially mulitple on-device names
		foreach (const QString& onDeviceName, aRenamedBinaries.keys())
			{
			reverseRenameHash.insert(aRenamedBinaries[onDeviceName], onDeviceName);
			}

		const int codesegCount = aCodeSegs.count();
		for (int i = 0; i < codesegCount; i++)
			{
			if (reverseRenameHash.contains(aCodeSegs[i].iFileName))
				{
				QList<QString> onDeviceNames = reverseRenameHash.values(aCodeSegs[i].iFileName);
				foreach (QString onDeviceName, onDeviceNames)
					{
					if (version < EVersion2_3) onDeviceName.remove(0, 11); // Drop the z:\sys\bin\ on the floor
					renames.append(QPair<int,QString>(i, onDeviceName));
					}
				}
			}
		}
	qSort(renames);

	QDataStream stream(&file);
	stream.setByteOrder(QDataStream::BigEndian); // I like big endian, easier to debug

	static const QString KColons("::");
	quint32 codesegOffset = 4*4; // magic, version, codesegoffset, symbolsoffset
	if (version >= EVersion2_0) codesegOffset += 4; // tokenoffset
	if (version >= EVersion2_1) codesegOffset += 4; // renameoffset
	if (version >= EVersion2_2) codesegOffset += 4; // checksum
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
		for (int i = 0; i < renames.count(); i++)
			{
			stream << renames[i].first;
			const QString& val = renames[i].second;
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
		for (int i = 0; i < renames.count(); i++)
			{
			const QString& val = renames[i].second;
			CHECK_ENTITY(val.constData(), file.pos(), "rename %s -> %s", qPrintable(aCodeSegs[renames[i].first].iFileName), qPrintable(val));
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

void CBsymV2::WriteString(QDataStream& stream, int aVersion, const QString& aString)
	{
	if (aVersion >= EVersion2_0)
		{
		WriteString(stream, TokeniseString(aString));
		}
	else
		{
		WriteString(stream, aString.toLatin1());
		}
	}

QString CBsymV2::GetString(quint32 aLocation, bool aExpandTokens) const
	{
	quint16 nameLen = (quint16)iData[aLocation];
	aLocation++;
	if (nameLen == 255)
		{
		nameLen = qFromBigEndian<quint16>(iData+aLocation);
		aLocation += 2;
		}
	QString result = QString::fromLatin1((const char*)(iData+aLocation), nameLen);
	if (aExpandTokens && iVersion >= EVersion2_0)
		{
		for (int i = result.length() - 1; i >= 0; i--)
			{
			ushort val = result.at(i).unicode();
			if (val >= 128)
				{
				result.replace(i, 1, GetToken(val-128));
				}
			}
		}
	return result;
	}

QString CBsymV2::SymbolName(const TSymbol* aSymbol, const TCodeSeg* aParentCodeSeg) const
	{
	quint32 nameOffset = aSymbol->NameOffset();
	QString result = GetString(nameOffset);

	quint32 prefixIdx = aSymbol->NamePrefixIndex();
	if (aParentCodeSeg && prefixIdx)
		{
		// Then the symbol name has a prefix that needs looking up separately in the codeseg
		quint32 prefixLocation = aParentCodeSeg->PrefixTableOffset() + ((prefixIdx-1) * sizeof(quint32));
		// This location has the offset of the relevant string
		QString prefix = GetString(qFromBigEndian<quint32>(iData + prefixLocation)).append("::");
		result.prepend(prefix);
		}
	return result;
	}

int CBsymV2::TokenCount() const
	{
	if (iTokensOffset != 0)
		{
		return Uint(iTokensOffset);
		}
	else
		{
		return 0;
		}
	}

QString CBsymV2::GetToken(int aToken) const
	{
	if (aToken >= TokenCount())
		{
		qWarning("Request for token %d >= tokenCount %d", aToken, TokenCount());
		return QString("?");
		}
	quint32 tokenOffset = Uint(iTokensOffset + 4 + 4*aToken);
	return GetString(tokenOffset, false);
	}

quint32 CBsymV2::RomChecksum() const
	{
	if (iVersion >= EVersion2_2)
		{
		return Uint(24);
		}
	return 0;
	}

void CBsymV2::Dump(bool aVerbose) const
	{
	for (int i = 0; i < CodeSegCount(); i++)
		{
		const TCodeSeg& codeseg = CodeSegs()[i];
		qDebug("Codeseg %d: %08x len=%d count=%d %s", i, codeseg.Address(), CodeSegLength(&codeseg), codeseg.SymbolCount(), qPrintable(SymbolName(&codeseg)));
		}
	foreach (const QString& key, iFileNameToCodeSeg.keys())
		{
		qDebug("Device file %s -> codeseg %s", qPrintable(key), qPrintable(SymbolName(iFileNameToCodeSeg[key])));
		}

	if (aVerbose)
		{
		for (int i = 0; i < CodeSegCount(); i++)
			{
			DumpCodeSeg(CodeSegs() + i);
			}
		}
	}

void CBsymV2::DumpCodeSeg(const TCodeSeg* aCodeSeg) const
	{
	if (!aCodeSeg) return;
	qDebug("\n%s count=%d", qPrintable(SymbolName(aCodeSeg)), aCodeSeg->SymbolCount());
	for (int i = 0; i < (int)aCodeSeg->SymbolCount(); i++)
		{
		const TSymbol* sym = Symbols() + aCodeSeg->SymbolStart() + i;
		qDebug("%d\t%08x\t%08x\t%s", i, sym->Address(), sym->Length(), qPrintable(SymbolName(sym, aCodeSeg)));
		}
	}
