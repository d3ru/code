// symbolics.h
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

#ifndef SYMBOLFILE_H
#define SYMBOLFILE_H

#include <QtCore>
#include "magictypes.h"
class CBsymFile;
class CProcess;

/*
#ifdef NO_THREADMODEL
typedef TProcessId TOpaqueProcessId;
#else
typedef CProcess* TOpaqueProcessId;
#endif
*/

class TOpaqueProcessId
	{
public:
	TOpaqueProcessId() : iPtr(0) {}
	TOpaqueProcessId(CProcess* aProcess) : iPtr((quintptr)aProcess) {}
	explicit TOpaqueProcessId(TProcessId aId)
		{
		if (aId) iPtr = (aId << 1) | 1;
		else iPtr = 0;
		}

	CProcess* ProcessPtr() const
		{
		if (iPtr & 1) return NULL;
		return (CProcess*)iPtr;
		}

	bool operator==(const TOpaqueProcessId& aOther) const { return iPtr == aOther.iPtr; }
	bool operator!=(const TOpaqueProcessId& aOther) const { return iPtr != aOther.iPtr; }
	//operator bool() const { return iPtr != 0; }

private:
	quintptr iPtr;
	};

class TLookupResult
	{
public:
	enum TAccuracy
		{
		ENotFound = 0,
		EFoundCodeseg, // We only figured out codeseg and offset
		EBeyondSymbol, // Appears to be outside of any known symbol. What used to be referred to as 'fuzzy'
		EFoundSymbol, // We located both codeseg and symbol
		};

	TLookupResult() : iSymbolStartAddress(0), iOffsetInSymbol(0), iAccuracy(ENotFound) {}
	TLookupResult(const TLookupResult& aRes) : iSymbolStartAddress(aRes.iSymbolStartAddress), iOffsetInSymbol(aRes.iOffsetInSymbol), iSymbolName(aRes.iSymbolName), iCodesegName(aRes.iCodesegName), iAccuracy(aRes.iAccuracy) {}
	TLookupResult(quint32 aSymbolAddress, quint32 aOffsetInSymbol, const QString& aSymbolName, const QString& aCodesegName=QString(), TAccuracy aAccuracy=EFoundSymbol)
		: iSymbolStartAddress(aSymbolAddress), iOffsetInSymbol(aOffsetInSymbol), iSymbolName(aSymbolName), iCodesegName(aCodesegName), iAccuracy(aAccuracy)
		{}
	bool Valid() const { return iAccuracy != ENotFound && (iSymbolName.length() || iCodesegName.length()); }

	QString Description() const;
	QString RichDescription() const;
	TAccuracy Accuracy() const { return iAccuracy; }
	const QString& SymbolName() const { return iSymbolName; }
	quint32 SymbolOffset() const { return iOffsetInSymbol; }

	QString SymbolDescription() const;
	QString RichSymbolDescription() const;

public:
	quint32 iSymbolStartAddress; // This is the run address of the symbol, which won't equal the bsym/symbol file address if the codeseg is RAM-loaded (ie ROFS)
private:
	quint32 iOffsetInSymbol;
	QString iSymbolName;
	QString iCodesegName;
	TAccuracy iAccuracy;
	};

class TLoadedCodeseg;

class CSymbolics : public QObject
	{
	Q_OBJECT
	Q_PROPERTY(bool debugLookup READ DebugLookup WRITE SetDebugLookup)
	
public:
	explicit CSymbolics(bool aAutoCreateBsym, QObject* aParent=NULL);
	~CSymbolics();
	bool AddSymbolFile(const QString& aFilePath);
	bool AddSymbolFiles(const QStringList& aFilePaths);
	int WriteBsymFile(const QString& aOutput);
	bool AddMapFile(const QString& aMapFile, quint32 aLoadAddress = 0);
	bool AddRombuildLogFile(const QString& aFilePath);
	Q_INVOKABLE bool AddBsymFile(const QString& aFilePath);
	Q_INVOKABLE QStringList LoadedBsyms() const;

	Q_INVOKABLE void DumpBsyms(bool aVerbose);
	bool DebugLookup() const;
	void SetDebugLookup(bool aDebug);

	virtual bool Busy() const;

	TLookupResult Lookup(quint32 aAddress, TOpaqueProcessId aProcess = TOpaqueProcessId(), TTimeInterval aTime=0) const; // In order to figure out dynamically loaded codesegs we need to know the time at which this address was valid, and what process we're looking at (since exe codesegs can share the same load address)

	Q_INVOKABLE quint32 RomChecksum() const;

	void AddCodeseg(const QString& aName, int aSize, TOpaqueProcessId aProcessId, quint32 aRunAddr);

	// For use by CBTraceData - these aren't nice APIs but are limited by what BTrace provides
	void CodesegCreated(TTimeInterval aCreateTime, TKernelId aId, const QString& aName);
	void CodesegDestroyed(TTimeInterval aDestroyTime, TKernelId aId);
	void CodesegInfo(TTimeInterval aCurrentTime, TKernelId aId, int aSize, quint32 aRunAddr);
	void CodesegMapped(TTimeInterval aCurrentTime, TKernelId aCodesegId, CProcess* aProcess);

	struct CodeSeg;
private:
	TLookupResult DoLookupInSymbolList(quint32 aAddress) const;
	const TLoadedCodeseg* FindLoadedCodesegById(TKernelId aId, TTimeInterval aTime) const;
	TLoadedCodeseg* FindLoadedCodesegById(TKernelId aId, TTimeInterval aTime);
	void UpdateCurrentCodesegListToTime(TTimeInterval aTime) const;
	const TLoadedCodeseg* LookupCodesegForAddress(quint32 aAddress, TOpaqueProcessId aProcess, TTimeInterval aTime) const;

protected:
	int CheckForBsym(const QString& aFilePath);
	virtual bool WasCancelled();
	void DoAddSymbolFile(QFile& aFile);

protected slots:
	void DoParsingComplete(const QString& aFile, int aSymbolCount, const QVector<CodeSeg>& aCodeSegs);

signals:
	void Progress(int aProgress);
	void SetProgressMaximum(int aMax);
	void ParsingComplete(const QString& aFile, int aSymbolCount, const QVector<CodeSeg>& aCodeSegs);
	void SymbolicsChanged();
	
public:
	struct Symbol
		{
		Symbol() : iAddress(0), iLength(0), iSpare(0) {}
		bool operator<(const Symbol& other) const;
		QString Name() const;
		QString SectionName() const;
		const QString& RawName() const;

		quint32 iAddress;
		int iLength;
		QString iName;
		mutable int iSpare; // Messy I know, but easiest to have this for CBsymFile::WriteBsym to steal
		};
	struct CodeSeg
		{
		CodeSeg() : iSpare(NULL) {}
		bool operator<(const CodeSeg& other) const;
		quint32 Address() const;
		int Length() const;
		
		QString iFileName; // The name from the symbol file, ie \epoc32\release\armv5\urel\thing.dll
		QString iNameOnDevice; // Allowing for IBY renames - of format \sys\bin\whatever.dll
		QVector<Symbol> iSymbols;
		mutable void* iSpare; // Messy I know, but easiest to have this for CBsymFile::WriteBsym to steal
		};

private:
	CBsymFile* WriteBsym(const QString& aFileName, const QVector<CodeSeg>& aCodeSegs, int aSymbolCount, const QHash<QString,QString>& aRenamedBinaries);

private:
	QVector<CodeSeg> iCodeSegs;
	QList<CBsymFile*> iBsymFiles;
	bool iWriteBsym;

	QList<TLoadedCodeseg*> iLoadedCodesegs;
	mutable TTimeInterval iCurrentTime; // For what's in iCurrentCodesegs
	mutable QList<TLoadedCodeseg*> iCurrentDllCodesegs; // For the time "iCurrentTime". Not owned. These are assumed to all have distinct addresses, and are sorted in address order
	mutable QList<TLoadedCodeseg*> iCurrentExeCodesegs; // For the time "iCurrentTime". Not owned. These are checked manually and may have the same address
	mutable int iLoadedCodesegsNextIdx;
	QHash<QString, QString> iRomNameMappings; // For things that are renamed during rombuild. Keys are on-device binary names, values are on-PC codeseg names
	quint32 iLastSeenChecksum; // Saved between AddRombuildLogFile and WriteBsym
	};

class CBsymFile : public QObject
	{
	Q_OBJECT
	
public:
	static CBsymFile* New(const QString& aString, QObject* aParent = NULL);
	~CBsymFile();
	
	explicit CBsymFile(QObject* aParent = NULL);
	virtual quint32 RomChecksum() const = 0;
	QString Filename() const;
	
	virtual TLookupResult LookupRomSymbol(quint32 aAddress) const = 0;
	virtual TLookupResult Lookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const = 0;

	virtual void Dump(bool) const {}

signals:
	void Progress(int aProgress);
	void SetProgressMaximum(int aMax);

protected:
	CBsymFile(const QString& aString, QObject* aParent);
	virtual bool Open() = 0;
	static void WriteString(QDataStream& stream, const QByteArray& aString);
	static quint32 StringTableLen(int len);

protected:
	enum TVersion
		{
		// See BSYM_file_format.txt for the meanings of these
		EVersion1_0 = 0x00010000,
		EVersion2_0 = 0x00020000,
		EVersion2_1 = 0x00020001,
		EVersion2_2 = 0x00020002,
		EVersion2_3 = 0x00020003,
		EVersion3_0 = 0x00030000,
		};
	static const int KMajorVersion = 0xFFFF0000;
	static const int KMinorVersion = 0x0000FFFF;
	static const quint32 KMagic = ('B'<<24)|('S'<<16)|('Y'<<8)|'M';

	QFile iFile;
	const uchar* iData;
	quint32 iFileSize;
	quint32 iVersion;
	};

// Used to sort a list of pointers
template <typename T>
class PtrLessThan
	{
public:
	inline bool operator()(T* const& t1, T* const& t2) const
		{
		return (*t1) < (*t2);
		}
	};

#endif
