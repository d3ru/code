// symbolics.cpp
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

// Grr!
#include "qglobal.h"
#ifdef Q_CC_MSVC // Doesn't even have inttypes.h ...
#define PRId64 "I64d"
#else
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif
#include "symbolics.h"
#include "parser.h"
#include "bsym_v2.h"
#ifdef BSYM_SUPPORT_V3
#include "bsym_v3.h"
#endif

#ifndef NO_THREADMODEL
#include "threadmodel.h"
#endif

bool gSymbolLookupDebug = false;

class TLoadedCodeseg
	{
public:
	TLoadedCodeseg(TTimeInterval aCreateTime, TKernelId aId, const QString& aName); // Used by btrace
	TLoadedCodeseg(TOpaqueProcessId aProcess, const QString& aName, int aSize, quint32 aRunAddr); // Used when the time dimension isn't important and you already know all the info about the mapping
	void SetInfo(int aSize, quint32 aRunAddr);
	TLoadedCodeseg(quint32 aAddress) { iRunAddr = aAddress; } // Only for use while sorting
	bool operator==(const TLoadedCodeseg& aOther) const;
	bool operator<(const TLoadedCodeseg& aOther) const;

	bool Contains(quint32 aAddress, TOpaqueProcessId aProcess) const;

	TTimeInterval iCreateTime;
	TTimeInterval iDestroyTime;
	TKernelId iCodesegPtr; // This is only so we can find the codeseg again in CSymbolics::CodesegDestroyed
	TOpaqueProcessId iProcess;
	QString iName;
	int iSize;
	quint32 iRunAddr;
	};

const TTimeInterval KEndTime = Q_INT64_C(0x7FFFFFFFFFFFFFFF);

bool CSymbolics::Symbol::operator<(const CSymbolics::Symbol& other) const
	{
	return iAddress < other.iAddress;
	}

bool CSymbolics::CodeSeg::operator<(const CSymbolics::CodeSeg& other) const
	{
	return Address() < other.Address();
	}

QString CSymbolics::Symbol::Name() const
	{
	int space = iName.lastIndexOf("  ");
	if (space >= 40)
		{
		return iName.left(space).trimmed();
		}
	else
		{
		return iName;
		}
	}

QString CSymbolics::Symbol::SectionName() const
	{
	int space = iName.lastIndexOf("  ");
	if (space >= 40)
		{
		return iName.mid(space).trimmed();
		}
	else
		{
		return QString();
		}
	}
	
quint32 CSymbolics::CodeSeg::Address() const
	{
	if (iSymbols.count())
		{
		return iSymbols.first().iAddress;
		}
	return 0;
	}

int CSymbolics::CodeSeg::Length() const
	{
	if (iSymbols.count())
		{
		return iSymbols.last().iAddress + iSymbols.last().iLength - Address();
		}
	return 0;
	}

const QString& CSymbolics::Symbol::RawName() const
	{
	return iName;
	}

CSymbolics::CSymbolics(bool aAutoCreateBsym, QObject* aParent)
	: QObject(aParent), iWriteBsym(aAutoCreateBsym), iCurrentTime(KEndTime), iLoadedCodesegsNextIdx(0)
	{
	connect(this, SIGNAL(ParsingComplete(QString,int,QVector<CodeSeg>)), this, SLOT(DoParsingComplete(QString,int,QVector<CodeSeg>)));
	}

CSymbolics::~CSymbolics()
	{
	foreach (TLoadedCodeseg* codeseg, iLoadedCodesegs)
		{
		delete codeseg;
		}
	}

TLookupResult CSymbolics::DoLookupInSymbolList(quint32 aAddress) const
	{
	// Note: This function needs retiring. We should go with BSYM being the native format.

	if (Busy()) return TLookupResult(); // Busy messing with the symbol list
	if (!iCodeSegs.count()) return TLookupResult();
	
	quint32 address = aAddress & 0xFFFFFFFE; // Mask bottom bit as it just indicates thumb mode
	Symbol temp; temp.iAddress = address;
	
	// First find the right code segment
	CodeSeg tempSeg; tempSeg.iSymbols.append(temp);
	QVector<CodeSeg>::const_iterator codesegIter = qUpperBound(iCodeSegs.begin(), iCodeSegs.end(), tempSeg);
	if (codesegIter != iCodeSegs.begin()) codesegIter--;
	const CodeSeg& codeseg = *codesegIter;
	if (codeseg.Address() != 0 && address >= codeseg.Address() && address < codeseg.Address() + codeseg.Length())
		{
		// Now find the right symbol within that segment
		QVector<Symbol>::const_iterator iter = qUpperBound(codeseg.iSymbols.begin(), codeseg.iSymbols.end(), temp);
		if (iter != codeseg.iSymbols.begin()) iter--;
		const Symbol& symbol = *iter;
		if (address >= symbol.iAddress && address < symbol.iAddress + symbol.iLength)
			{
			return TLookupResult(symbol.iAddress, address - symbol.iAddress, symbol.iName, codeseg.iFileName);
			}
		}
	return TLookupResult();
	}

TLookupResult CSymbolics::Lookup(quint32 aAddress, TOpaqueProcessId aProcess, TTimeInterval aTime) const
	{
	if (gSymbolLookupDebug) qDebug("Lookup(0x%08x)", aAddress);
	//if (aAddress == 0x00402664)
	//	{
	//	(++aAddress)--;
	//	}

	TLookupResult symbol = DoLookupInSymbolList(aAddress);
	if (!symbol.Valid())
		{
		if (gSymbolLookupDebug) qDebug("Not found in symbol list");
		// Try bsym files
		foreach(CBsymFile* bsym, iBsymFiles)
			{
			symbol = bsym->LookupRomSymbol(aAddress);
			if (symbol.Valid()) break;
			}
		}

	if (!symbol.Valid() || symbol.Accuracy() == TLookupResult::EFoundCodeseg)
		{
		// Try looking up in codesegs, in case it's RAM-loaded code
		const TLoadedCodeseg* codeseg = LookupCodesegForAddress(aAddress, aProcess, aTime);
		if (codeseg)
			{
			// See if we have a codeseg loaded directly (Eg from a map file)
			foreach (const CodeSeg& c, iCodeSegs)
				{
				// We don't attempt to be clever about rename matching here. c.iFileName is of form P:\epoc32\release\armv5\urel\whatever.dll, codeseg.iName is C:\sys\bin\whatever.dll
				if (c.iFileName.mid(c.iFileName.lastIndexOf('\\') + 1).compare(codeseg->iName.mid(codeseg->iName.lastIndexOf('\\') + 1), Qt::CaseInsensitive) == 0)
					{
					if (gSymbolLookupDebug) qDebug("Found codeseg %s in CSymbolics::CodeSeg list", qPrintable(codeseg->iName));
					// Now find the right symbol within that segment
					Symbol temp; temp.iAddress = (aAddress & 0xFFFFFFFE) - codeseg->iRunAddr;
					QVector<Symbol>::const_iterator iter = qUpperBound(c.iSymbols.begin(), c.iSymbols.end(), temp);
					if (iter != c.iSymbols.begin()) iter--;
					const Symbol& s = *iter;
					if (temp.iAddress >= s.iAddress && temp.iAddress < s.iAddress + s.iLength)
						{
						symbol = TLookupResult(codeseg->iRunAddr + s.iAddress, temp.iAddress - s.iAddress, s.iName, c.iFileName);
						}
					break;
					}
				}

			if (!symbol.Valid())
				{
				foreach(CBsymFile* bsym, iBsymFiles)
					{
					symbol = bsym->Lookup(codeseg->iName, aAddress - codeseg->iRunAddr);
					if (symbol.Valid())
						{
						if (gSymbolLookupDebug) qDebug("BSYM %s lookup ftw", qPrintable(bsym->Filename()));
						symbol.iSymbolStartAddress += codeseg->iRunAddr; // Make sure the TLookupResult symbol address is updated for the run address (which CBsymFile doesn't know anything about)
						break;
						}
					}
				}

			if (!symbol.Valid())
				{
				// We still have the codeseg name
				if (gSymbolLookupDebug) qDebug("Not found anywhere else, falling back to just the codeseg");
				symbol = TLookupResult(codeseg->iRunAddr, (aAddress&0xFFFFFFFE) - codeseg->iRunAddr, QString(), codeseg->iName, TLookupResult::EFoundCodeseg);
				}
			}
		}

	if (gSymbolLookupDebug) qDebug("Lookup(0x%08x) returning %s", aAddress, qPrintable(symbol.Description()));
	return symbol;
	}

void CSymbolics::DoAddSymbolFile(QFile& aFile)
	{
	emit Progress(0);
	emit SetProgressMaximum(aFile.size());
	QTextStream stream(&aFile);

	static const QString KFrom("From ");
	//static const QString KFromRegex("From +(.*)");
	//static const QString KSymbolRegex("([0-9a-fA-F]+)\\s+([0-9a-fA-F]+)\\s(.*)");
	
	//QVector<Symbol*> romSymbols;
	//QVector<Symbol> codeSegs;
	QVector<CodeSeg> codeSegs;
	//QVector<Symbol>* symbols = NULL;
	//bool rofsSeg = false;
	int symbolCount = 0;
	
	while (!WasCancelled() && !stream.atEnd())
		{
		int pos = (int)aFile.pos();
		if ((pos & 0xFFFFF) == 0) emit Progress(pos); // Don't call too often, it is inefficient
			
		QString line = stream.readLine();
		if (line.length() == 0) continue;
		else if (line.startsWith(KFrom))
			{
			// New code seg
			CodeSeg codeseg;
			codeseg.iFileName = line.mid(KFrom.length()).trimmed();
			codeSegs.append(codeseg);
			}
		else
			{
			if (line.length() > 22)
				{
				if (line.endsWith("(.data)") || line.endsWith("(.bss)") || line.contains(".o(.data") || line.contains(".o(.bss"))
					{
					// For now (with bsym v2 and all) we counstruct a new "codeseg" to contain the data. Fortunately all the .data and .bss appears at the end of the codeseg stuff
					// But only do this for stuff in ROM - for ROFS we need to also account for data load addresses which there's no provision for in the format or any of the tools
					if (codeSegs.last().Address() == 0) continue;
					CodeSeg dataSeg;
					dataSeg.iFileName = codeSegs.last().iFileName;
					codeSegs.append(dataSeg);
					}
				Symbol symbol;
				bool ok = false;
				symbol.iAddress = (line.left(8).toUInt(&ok, 16)) & 0xFFFFFFFE; // Remove thumb bit
				if (!ok) continue;
				symbol.iLength = line.mid(12, 4).toUInt(&ok, 16);
				if (!ok) continue;
				if (symbol.iLength == 0 && codeSegs.last().iSymbols.count()) continue; // You get these in symbol files and they mess things up if you pay attention to them (because they appear in the middle of other symbols). Don't ignore the first symbol of the file though, we sort them out later because in some circumstances they're useful to keep
				symbol.iName = line.mid(20);
				codeSegs.last().iSymbols.append(symbol);
				symbolCount++;
				}
			}
		}

	// Go through and fix up codesegs with zero-length first symbols - they really mess things up if left unmodified because a codeseg with no symbols is considered to have address zero, and therefore appears out-of-order. Generally this is caused by files that aren't actually executables - they appear to have a single, zero-length symbol that is their file name. We will represent that by saying they have one symbol that fills the extent to the next codeseg, because that's really what it is, and we'll rely on the user being smart enough to figure this isn't a code symbol
	for (int i = codeSegs.count()-1; i >= 0; i--)
		{
		QVector<Symbol>& symbols = codeSegs[i].iSymbols;
		if (symbols.count() && symbols.first().iLength == 0)
			{
			// If there are no other symbols, make this symbol span the whole extent. Otherwise delete it.
			Symbol& sym = symbols.first();
			if (symbols.count() > 1)
				{
				symbols.remove(0);
				symbolCount--;
				}
			else if (i+1 < codeSegs.count())
				{
				int codeseglen = codeSegs[i+1].Address() - sym.iAddress;
				sym.iLength = codeseglen;
				if (codeseglen == 0)
					{
					// In a ROFS file the next codeseg will also be a zero address so there's no way of doing anything clever with it - just bin it
					symbolCount--;
					codeSegs.remove(i);
					}
				}
			else
				{
				// If it's the last codeseg, we don't know how big it is, so nothing we can do about it - bin the whole codeseg
				symbolCount--;
				codeSegs.remove(i);
				}
			}
		else if (symbols.count() == 0)
			{
			// Don't think this ever happens but just to be safe we need to nuke it
			codeSegs.remove(i);
			}
		}

	qStableSort(codeSegs);
	emit ParsingComplete(aFile.fileName(), symbolCount, codeSegs); // This will call DoParsingComplete on the main thread
	emit SymbolicsChanged();
	}

void CSymbolics::DoParsingComplete(const QString& aFile, int aSymbolCount, const QVector<CodeSeg>& aCodeSegs)
	{
	bool bsymSuccessfullyWritten = false;
	if (iWriteBsym)
		{
		emit SetProgressMaximum((aSymbolCount + aCodeSegs.count())*2);
		CBsymFile* file = WriteBsym(aFile, aCodeSegs, aSymbolCount, QHash<QString,QString>());
		if (file)
			{
			bsymSuccessfullyWritten = true;
			iBsymFiles.append(file);
			}
		else
			{
			qWarning("Couldn't create bsym file file for %s", qPrintable(aFile));
			}
		}
	
	if (!bsymSuccessfullyWritten)
		{
		iCodeSegs += aCodeSegs;
		qStableSort(iCodeSegs);
		}
	}

bool CSymbolics::Busy() const
	{
	return false; // Subclasses override this
	}

bool CSymbolics::WasCancelled()
	{
	return false; // Subclasses override this
	}

bool CSymbolics::AddBsymFile(const QString& aFilePath)
	{
	CBsymFile* file = CBsymFile::New(aFilePath, this);
	if (file)
		{
		iBsymFiles.append(file);
		emit SymbolicsChanged();
		return true;
		}
	else
		{
		return false;
		}
	}

int CSymbolics::CheckForBsym(const QString& aFilePath)
	{
	QFileInfo inf(aFilePath);
	QString bsymName = inf.completeBaseName().append(".bsym");
	inf.setFile(inf.dir(), bsymName);
	if (!inf.exists()) return 0;
	
	bool ok = AddBsymFile(inf.filePath());
	if (ok)
		{
		return 1;
		}
	else
		{
		return -1;
		}
	}

bool CSymbolics::AddSymbolFile(const QString& aFilePath)
	{
	if (Busy()) return false;

	QFile file(aFilePath);
	if (!file.open(QIODevice::ReadOnly)) return false;

	DoAddSymbolFile(file);
	return true;
	}

int CSymbolics::WriteBsymFile(const QString& aOutput)
	{
	// Need to count symbols, because for performance reasons CBsymFile::WriteBsym expects us to already have this info
	int symbolCount = 0;
	foreach (const CodeSeg& codeseg, iCodeSegs)
		{
		symbolCount += codeseg.iSymbols.count();
		}
	CBsymFile* result = WriteBsym(aOutput, iCodeSegs, symbolCount, iRomNameMappings);
	if (result)
		{
		iBsymFiles.append(result);
		return symbolCount;
		}
	else
		{
		return 0;
		}
	}

CBsymFile* CSymbolics::WriteBsym(const QString& aFileName, const QVector<CSymbolics::CodeSeg>& aCodeSegs, int aSymbolCount, const QHash<QString,QString>& aRenamedBinaries)
	{
	// TODO v3 support
	CBsymV2* b = new CBsymV2(this);
	connect(b, SIGNAL(Progress(int)), this, SIGNAL(Progress(int)));
	connect(b, SIGNAL(SetProgressMaximum(int)), this, SIGNAL(SetProgressMaximum(int)));
	bool ok = b->WriteBsym(aFileName, aCodeSegs, aSymbolCount, aRenamedBinaries, iLastSeenChecksum);
	if (!ok)
		{
		delete b;
		b = NULL;
		}
	else
		{
		b->disconnect(this);
		}
	return b;
	}

void CSymbolics::AddCodeseg(const QString& aName, int aSize, TOpaqueProcessId aProcess, quint32 aRunAddr)
	{
	TLoadedCodeseg* codeseg = new TLoadedCodeseg(aProcess, aName.toLower(), aSize, aRunAddr);

	// Check if we've already seen this
	for (int i = 0; i < iLoadedCodesegs.count(); i++)
		{
		if (*iLoadedCodesegs[i] == *codeseg)
			{
			delete iLoadedCodesegs[i];
			iLoadedCodesegs[i] = NULL; // CodeScanner...
			iLoadedCodesegs.replace(i, codeseg);
			return;
			}
		}

	iLoadedCodesegs.append(codeseg);
	iCurrentTime = KEndTime; // Invalidates iCurrentDllCodesegs and iCurrentExeCodesegs
	}

TLoadedCodeseg::TLoadedCodeseg(TOpaqueProcessId aProcess, const QString& aName, int aSize, quint32 aRunAddr)
	: iCreateTime(0), iDestroyTime(KEndTime), iCodesegPtr(0), iProcess(aProcess), iName(aName), iSize(aSize), iRunAddr(aRunAddr)
	{
	}

TLoadedCodeseg::TLoadedCodeseg(TTimeInterval aCreateTime, TKernelId aId, const QString& aName)
	: iCreateTime(aCreateTime), iDestroyTime(KEndTime), iCodesegPtr(aId), iProcess(), iName(aName), iSize(0), iRunAddr(0)
	{
	}

bool TLoadedCodeseg::Contains(quint32 aAddress, TOpaqueProcessId aProcess) const
	{
	// The definition below nicely handles kernel-side RAM loaded code - it is passed to AddCodeseg with a TProcessId of zero meaning valid in any process
	if (iProcess != NULL && aProcess != NULL && aProcess != iProcess) return false; // If aAddress must be in a specific process, and we are in a different process, we don't have this address
	return aAddress >= iRunAddr && aAddress < iRunAddr + iSize;
	}

bool TLoadedCodeseg::operator==(const TLoadedCodeseg& aOther) const
	{
	return iProcess == aOther.iProcess && iRunAddr == aOther.iRunAddr && iName == aOther.iName && iCreateTime == aOther.iCreateTime && iSize == aOther.iSize;
	}

bool TLoadedCodeseg::operator<(const TLoadedCodeseg& t2) const
	{
	return (iRunAddr < t2.iRunAddr);
	}

void TLoadedCodeseg::SetInfo(int aSize, quint32 aRunAddr)
	{
	iSize = aSize;
	iRunAddr = aRunAddr;
	}

void CSymbolics::CodesegCreated(TTimeInterval aCreateTime, TKernelId aId, const QString& aName)
	{
	iLoadedCodesegs.append(new TLoadedCodeseg(aCreateTime, aId, aName.toLower())); // Tracing seems inconsistant about things like whether the drive letter is capitalised...
	}

void CSymbolics::CodesegDestroyed(TTimeInterval aDestroyTime, TKernelId aId)
	{
	TLoadedCodeseg* codeseg = FindLoadedCodesegById(aId, aDestroyTime);
	if (!codeseg)
		{
		qWarning("Failed to find codeseg %x at time %"PRId64, aId, aDestroyTime);
		return;
		}
	if (codeseg->iDestroyTime != KEndTime)
		{
		qWarning("Duplicate CodesegDestroyed for codeseg %x %s at %"PRId64" (previously was %"PRId64")", aId, qPrintable(codeseg->iName), aDestroyTime, codeseg->iDestroyTime);
		return;
		}
	codeseg->iDestroyTime = aDestroyTime;
	}

void CSymbolics::CodesegInfo(TTimeInterval aCurrentTime, TKernelId aId, int aSize, quint32 aRunAddr)
	{
	TLoadedCodeseg* codeseg = FindLoadedCodesegById(aId, aCurrentTime);
	if (!codeseg)
		{
		qWarning("Failed to find codeseg %x at time %"PRId64, aId, aCurrentTime);
		return;
		}
	codeseg->SetInfo(aSize, aRunAddr);
	}

void CSymbolics::CodesegMapped(TTimeInterval aCurrentTime, TKernelId aId, CProcess* aProcess)
	{
	TLoadedCodeseg* codeseg = FindLoadedCodesegById(aId, aCurrentTime);
	if (!codeseg)
		{
		qWarning("Failed to find codeseg %x at time %"PRId64, aId, aCurrentTime);
		return;
		}
	if (codeseg->iName.endsWith(".exe", Qt::CaseInsensitive))
		{
		codeseg->iProcess = aProcess;
		}
	}

TLoadedCodeseg* CSymbolics::FindLoadedCodesegById(TKernelId aId, TTimeInterval aTime)
	{
	const CSymbolics* constThis = this;
	return const_cast<TLoadedCodeseg*>(constThis->FindLoadedCodesegById(aId, aTime));
	}

const TLoadedCodeseg* CSymbolics::FindLoadedCodesegById(TKernelId aId, TTimeInterval aTime) const
	{
	for	(int i = iLoadedCodesegs.count()-1; i >= 0; i--)
		{
		TLoadedCodeseg* c = iLoadedCodesegs[i];
		if (c->iCodesegPtr == aId && aTime >= c->iCreateTime && aTime < c->iDestroyTime)
			{
			return c;
			}
		}
	return NULL;
	}

const TLoadedCodeseg* CSymbolics::LookupCodesegForAddress(quint32 aAddress, TOpaqueProcessId aProcess, TTimeInterval aTime) const
	{
	UpdateCurrentCodesegListToTime(aTime);
	if (iCurrentDllCodesegs.count() == 0) return NULL;
	/*
	if (aAddress == 0x40037f)
		{
		foreach (TLoadedCodeseg* c, iCurrentDllCodesegs)
			{
			qDebug("Dll Codeseg %s in process %s at 0x%x", qPrintable(c->iName), c->iProcess.ProcessPtr() ? qPrintable(c->iProcess.ProcessPtr()->Name()) : "0", c->iRunAddr);
			}
		foreach (TLoadedCodeseg* c, iCurrentExeCodesegs)
			{
			qDebug("Exe Codeseg %s in process %s at 0x%x", qPrintable(c->iName), c->iProcess.ProcessPtr() ? qPrintable(c->iProcess.ProcessPtr()->Name()) : "0", c->iRunAddr);
			}
		}
	*/
	quint32 address = aAddress & 0xFFFFFFFE; // Mask bottom bit as it just indicates thumb mode
	TLoadedCodeseg tempSeg(address);
	QList<TLoadedCodeseg*>::const_iterator begin = iCurrentDllCodesegs.constBegin(), end = iCurrentDllCodesegs.constEnd();
	QList<TLoadedCodeseg*>::const_iterator codesegIter = qUpperBound(begin, end, &tempSeg, PtrLessThan<TLoadedCodeseg>());
	const TLoadedCodeseg* foundMatchingExe = NULL;
	const TLoadedCodeseg* foundMatchingCodeseg = NULL;
	if (codesegIter != begin) codesegIter--;
	if ((*codesegIter)->Contains(address, aProcess)) foundMatchingCodeseg = *codesegIter;
	if (!foundMatchingCodeseg)
		{
		if (gSymbolLookupDebug) qDebug("LookupCodesegForAddress no DLL codeseg found, searching EXE list");
		// Try the exe list - we don't do this unless we couldn't find anything satisfactory in iCurrentDllCodesegs, because it involves a relatively expensive linear search
		// Some special hacks for identifying and matching exe codesegs, because many can have the same address and priming doesn't emit CodesegMapped so they won't necessarily even be associated with a CProcess.
		// Try and do some heuristics matching by name
		QString exeCodesegName;
#ifndef NO_THREADMODEL
		if (aProcess.ProcessPtr())
			{
			int dotexe = aProcess.ProcessPtr()->Name().indexOf(".exe");
			if (dotexe != -1) exeCodesegName = QString("\\%1").arg(aProcess.ProcessPtr()->Name().left(dotexe+4));
			}
#endif

		foreach (const TLoadedCodeseg* c, iCurrentExeCodesegs)
			{
			if (c->Contains(address, aProcess) && (c->iProcess.ProcessPtr() || (exeCodesegName.length() && c->iName.contains(exeCodesegName))))
				{
				if (gSymbolLookupDebug) qDebug("found matching EXE %s", qPrintable(c->iName));
				foundMatchingExe = c;
				break;
				}
			}
		}

	// If we found a matching exe codeseg with the right name, prefer that
	if (foundMatchingExe) return foundMatchingExe;
	if (gSymbolLookupDebug && foundMatchingCodeseg) qDebug("found matching codeseg %s at run addr 0x%08x", qPrintable(foundMatchingCodeseg->iName), foundMatchingCodeseg->iRunAddr);
	return foundMatchingCodeseg;
	}

void CSymbolics::UpdateCurrentCodesegListToTime(TTimeInterval aTime) const
	{
	if (iCurrentTime == aTime) return;

	if (iCurrentTime > aTime)
		{
		// Don't support rewinding. Go back to start, do not pass go.
		iCurrentTime = 0;
		iLoadedCodesegsNextIdx = 0;
		iCurrentDllCodesegs.clear();
		iCurrentExeCodesegs.clear();
		}
	while (iCurrentTime <= aTime && iLoadedCodesegsNextIdx < iLoadedCodesegs.count())
		{
		TLoadedCodeseg* nextCodeseg = iLoadedCodesegs.at(iLoadedCodesegsNextIdx);
		if (nextCodeseg->iCreateTime > aTime) break;
		if (nextCodeseg->iName.endsWith(".exe", Qt::CaseInsensitive)) iCurrentExeCodesegs.append(nextCodeseg);
		else iCurrentDllCodesegs.append(nextCodeseg);
		iCurrentTime = nextCodeseg->iCreateTime;
		iLoadedCodesegsNextIdx++;
		}
	// Need to make sure we remove any expired segments
	for (int i = iCurrentDllCodesegs.count() - 1; i >= 0; i--)
		{
		if (iCurrentDllCodesegs[i]->iDestroyTime <= iCurrentTime) iCurrentDllCodesegs.removeAt(i);
		}
	for (int i = iCurrentExeCodesegs.count() - 1; i >= 0; i--)
		{
		if (iCurrentExeCodesegs[i]->iDestroyTime <= iCurrentTime) iCurrentExeCodesegs.removeAt(i);
		}
	// And finally re-sort the list by address
	qSort(iCurrentDllCodesegs.begin(), iCurrentDllCodesegs.end(), PtrLessThan<TLoadedCodeseg>());
	}

//////////////////////

CBsymFile* CBsymFile::New(const QString& aFileName, QObject* aParent)
	{
	// Check the file header so we know what subclass to instanciate
	QFile f(aFileName);

	if (!f.open(QIODevice::ReadOnly)) return NULL;
	QString filename = QDir::toNativeSeparators(f.fileName());
	if (f.size() >= Q_INT64_C(0xFFFFFFFF))
		{
		qWarning("File %s is > 4GB, too big to be a BSYM", qPrintable(filename));
		return NULL;
		}
	int fileSize = (int)f.size();
	if (fileSize < 5*4)
		{
		qWarning("File %s is too small to be a bsym file", qPrintable(filename));
		return NULL;
		}
	quint32 startOfHeader[2];
	f.read((char*)&startOfHeader, 8);
	f.close();

	// Validate the data
	if (qFromBigEndian(startOfHeader[0]) != KMagic)
		{
		qWarning("%s is not a BSYM file.", qPrintable(filename));
		return false;
		}

	CBsymFile* bsym = NULL;
	quint32 version = qFromBigEndian(startOfHeader[1]);
	quint32 majorVersion = (version&KMajorVersion);
	if (majorVersion == EVersion1_0 || majorVersion == EVersion2_0)
		{
		bsym = new CBsymV2(aFileName, aParent);
		}
#ifdef BSYM_SUPPORT_V3
	else if (majorVersion == EVersion3_0)
		{
		bsym = new CBsymV3(aFileName, aParent);
		}
#endif
	else
		{
		qWarning("Unsupported bsym version %d in %s", majorVersion>>16, qPrintable(filename));
		return false;
		}

	bool ok = bsym->Open();
	if (!ok)
		{
		delete bsym;
		bsym = NULL;
		}
	return bsym;
	}

CBsymFile::CBsymFile(QObject* aParent)
	: QObject(aParent), iData(NULL), iVersion(0)
	{
	}

CBsymFile::CBsymFile(const QString& aString, QObject* aParent)
	: QObject(aParent), iFile(aString), iData(NULL), iVersion(0)
	{
	}

CBsymFile::~CBsymFile()
	{
	if (iData)
		{
		iFile.unmap((uchar*)iData);
		iData = NULL;
		iFile.close();
		}
	}

QString CBsymFile::Filename() const
	{
	return iFile.fileName();
	}

quint32 CBsymFile::StringTableLen(int len)
	{
	if (len >= 255) len += 2; // String longer than 254 require 2 extra bytes to store
	return len + 1;
	}
	
void CBsymFile::WriteString(QDataStream& stream, const QByteArray& aString)
	{
	int len = aString.size();
	if (len >= 255)
		{
		stream << (quint8)255u;
		stream << (quint16)len;
		}
	else
		{
		stream << (quint8)len;
		}
	stream.writeRawData(aString.constData(), len);
	}

static const QString KUnknown("<Unknown>");

QString TLookupResult::SymbolDescription() const
	{
	if (!Valid()) return KUnknown;

	QString result;
	if (iSymbolName.length() && iCodesegName.length())
		{
		result = QString("%1 (%2)").arg(iSymbolName).arg(iCodesegName.mid(iCodesegName.lastIndexOf('\\')+1));
		}
	else if (iCodesegName.length())
		{
		result.append(iCodesegName);
		}

	if (iAccuracy == EBeyondSymbol)
		{
		result.prepend("possibly ");
		}
	return result;
	}

QString TLookupResult::Description() const
	{
	return QString("%1 + 0x%2").arg(SymbolDescription()).arg(iOffsetInSymbol, 0, 16);
	}

QString TLookupResult::RichSymbolDescription() const
	{
	QString desc = Parser::HtmlEscape(SymbolDescription());
	int brak = desc.indexOf('(');
	if (brak == -1) brak = desc.length();
	QString result = QString("%2<font color=#999999>%3</font>").arg(desc.left(brak)).arg(desc.mid(brak));
	if (iAccuracy == EBeyondSymbol)
		{
		result = QString("<font color=#FF9999>").append(result).append("</font>");
		}
	return result;
	}

QString TLookupResult::RichDescription() const
	{
	return QString("%1 + 0x%2").arg(RichSymbolDescription()).arg(iOffsetInSymbol, 0, 16);
	}

bool CSymbolics::AddMapFile(const QString& aFilePath, quint32 aLoadAddress)
	{
	if (Busy()) return false;

	QFile file(aFilePath);
	if (!file.open(QIODevice::ReadOnly)) return false;
	QTextStream stream(&file);

	bool rvct = false;
	if (stream.readLine().startsWith("ARM Linker, RVCT")) rvct = true;
	bool foundOffset = false;
	int linenum = 0; // for debugging

	CodeSeg codeseg;
	codeseg.iFileName = aFilePath.left(aFilePath.length()-4); // remove the ".map" from the end
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
			if (rawAddress == 0 && textOffset != 0) continue; // Looks like this is yet another way of indicating something that isn't actually important?
			symbol.iAddress = rawAddress + aLoadAddress - textOffset;
			lex.skipWhiteSpace();
			QString symbolType = line.mid(lex.pos(), 8).trimmed();
			lex.seek(lex.pos() + 10); // Far enough to get over "ARM Code" or "Thumb Code"
			lex.skipWhiteSpace();
			lex >> symbol.iLength;
			if (!foundOffset && symbol.iLength != 0)
				{
				if (symbol.iName != ".emb_text" && symbol.iName != ".text")
					{
					qWarning("Failed to identify relocation offset for map file %s", qPrintable(aFilePath));
					return false;
					}
				// The text offset is the address of the first non-zero length symbol (at least, that's the most reliable way of figuring it out)
				foundOffset = true;
				textOffset = symbol.iAddress - aLoadAddress;
				symbol.iAddress = 0; // Manually relocate this symbol - by definition of it being the text offset, it's zero
				}
			if (symbolType == "Section") continue; // Sections aren't symbols as such - they can contain multiple symbols thus we don't want them confusing matters
			if (line.endsWith("Undefined Reference")) continue;
			if (symbolType == "Data" && line.indexOf(QRegExp("\\(\\.constdata.*\\)$")) == -1) continue; // Only allow data in constdata sections - they are the only things within the code section
			if (symbol.iLength == 0)
				{
				continue; // todo thunks lie about their size...
				}
			//if (rawAddress >= 0x00400000)
			//	{
			//	qWarning("Symbol %s of %s possibly in the data section", qPrintable(symbol.iName), qPrintable(aFilePath));
			//	}
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
			if (symbol.iAddress == 0 && textOffset != 0) continue; // Don't know if GCC actually does this but better safe than laminated across the walls
			//TODO fix limit check for gcce
			//if (aCodeSize != 0 && symbol.iAddress > aCodeSize) continue; // Loads of strange data sections appear at odd relocations - maksym doesn't include them so I won't bother either
			//if (symbolType == "Data" && line.indexOf(QRegExp("\\(\\.constdata.*\\)$")) == -1) continue; // Only allow data in constdata sections - they are the only things within the code section
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
	iCodeSegs.append(codeseg);
	return true;
	}

bool CSymbolics::AddRombuildLogFile(const QString& aFilePath)
	{
	if (Busy()) return false;

	// Got to figure out where the epocroot is
	QDir epocroot(aFilePath);
	while (!epocroot.exists("epoc32")) epocroot.cdUp();

	QFile file(aFilePath);
	if (!file.open(QIODevice::ReadOnly)) return false;
	const int KProgressFactor = 0xF; // Notify every 16th map file
	int progress = 0;
	emit SetProgressMaximum((int)file.size());
	emit Progress(progress);

	QTextStream stream(&file);
	// First find all the map files
	static const QString KProcessing = "Processing file ";
	static const QString KLoadLine = "Code start addr:         ";
	static const QString KReadingStart = "Reading file \\";
	static const QString KReadingEnd = " to image";
	static const QString KChecksum = "Checksum word:           ";
	QHash<QString, int> seenBinaries; // Maps binaries to num times seen - so we can keep track of things added to the rom more than once and avoid creating duplicate codesegs for them
	while (!stream.atEnd())
		{
		QString line = stream.readLine();
		if (line.startsWith(KProcessing))
			{
			// rombuild log
			QString file = line.mid(KProcessing.length()+1).trimmed(); // Plus one to skip the first backslash, so the file name looks relative (which I think it is, relative to epocroot)
			file = QDir::toNativeSeparators(QFileInfo(epocroot.absoluteFilePath(file)).absoluteFilePath().toLower());
			QString loadline = stream.readLine();
			quint32 loadAddress = 0;
			while (loadline.length() && !loadline.startsWith(KLoadLine)) loadline = stream.readLine(); // There can be an extra line between the two (it says "[primary]" for ekern)
			if (loadline.startsWith(KLoadLine))
				{
				loadAddress = loadline.mid(KLoadLine.length()).toUInt(NULL, 16);
				}

			if (seenBinaries[file] == 0) AddMapFile(QString(file).append(".map"), loadAddress);
			seenBinaries[file]++;
			//if (!ok) qWarning("couldn't load map file %s", qPrintable(file));
			// It isn't actually an error for the map file not to be present

			progress++;
			if ((progress & KProgressFactor) == 0) emit Progress(progress);
			}
		else if (line.startsWith(KReadingStart) && line.endsWith(KReadingEnd))
			{
			// rofsbuild log
			QString file = line.mid(KReadingStart.length(), line.length() - KReadingStart.length() - KReadingEnd.length());
			file = QDir::toNativeSeparators(QFileInfo(epocroot.absoluteFilePath(file)).absoluteFilePath().toLower());
			if (seenBinaries[file] == 0) AddMapFile(QString(file).append(".map"));
			seenBinaries[file]++;
			progress++;
			if ((progress & KProgressFactor) == 0) emit Progress(progress);
			}
		else if (line.startsWith(KChecksum))
			{
			iLastSeenChecksum = line.mid(KChecksum.length()).toUInt(NULL, 16);
			}
		else if (line.startsWith("\\"))
			{
			QString from = line.left(line.indexOf('\t'));
			QString to = line.mid(line.lastIndexOf('\t')+1);
			static const QString KSysBin = "sys\\bin\\";
			if (to.startsWith(KSysBin, Qt::CaseInsensitive))
				{
				QString srcPath = QDir::toNativeSeparators(QFileInfo(epocroot.absoluteFilePath(from)).absoluteFilePath()).toLower();
				QString srcName = QFileInfo(srcPath).fileName();
				QString destName = to.mid(KSysBin.length());
				QString destPath = to.toLower().prepend("z:\\");
				// Be sure to always include a file in the rename mappings even if its name is unchanged, if it is added to the image more than once
				// This is because the original name would get hidden by the rename if we didn't also explicitly say the original name was there too
				if (srcName.compare(destName, Qt::CaseInsensitive) != 0 || seenBinaries[srcPath] > 1) iRomNameMappings.insert(destPath, srcPath);
				progress++;
				if ((progress & KProgressFactor) == 0) emit Progress(progress);
				}
			}
		}

	// Finally, make sure codesegs are sorted
	qStableSort(iCodeSegs);

	emit Progress((int)file.size());
	emit SymbolicsChanged();
	return true;
	}

quint32 CSymbolics::RomChecksum() const
	{
	// Go through the bsyms until we find one that returns non-zero. There should only ever be one
	// bsym with ROM symbols as you can only ever have one ROM section
	foreach (const CBsymFile* bsym, iBsymFiles)
		{
		quint32 checksum = bsym->RomChecksum();
		if (checksum) return checksum;
		}
	return 0;
	}

QStringList CSymbolics::LoadedBsyms() const
	{
	QStringList result;
	foreach (const CBsymFile* bsym, iBsymFiles)
		{
		result.append(bsym->Filename());
		}
	return result;
	}

void CSymbolics::DumpBsyms(bool aVerbose)
	{
	foreach (const CBsymFile* bsym, iBsymFiles)
		{
		bsym->Dump(aVerbose);
		}
	}

bool CSymbolics::DebugLookup() const
	{
	return gSymbolLookupDebug;
	}

void CSymbolics::SetDebugLookup(bool aDebug)
	{
	gSymbolLookupDebug = aDebug;
	}
