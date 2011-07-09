// symbolics.h
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

#ifndef SYMBOLFILE_H
#define SYMBOLFILE_H

#include <QtCore>
#include "magictypes.h"
class CBsymFile;

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
	TLookupResult(quint32 aSymbolAddress, quint32 aOffsetInSymbol, const QString& aSymbolName, const QString& aCodesegName=QString(), TAccuracy aAccuracy=EFoundSymbol)
		: iSymbolStartAddress(aSymbolAddress), iOffsetInSymbol(aOffsetInSymbol), iSymbolName(aSymbolName), iCodesegName(aCodesegName), iAccuracy(aAccuracy)
		{}
	bool Valid() const { return iAccuracy != ENotFound && (iSymbolName.length() || iCodesegName.length()); }

	QString Description() const;
	QString RichDescription() const;
	TAccuracy Accuracy() const { return iAccuracy; }

private:
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
	Q_OBJECT;
	
public:
	explicit CSymbolics(bool aAutoCreateBsym, QObject* aParent=NULL);
	~CSymbolics();
	bool AddSymbolFile(const QString& aFilePath);
	bool AddSymbolFiles(const QStringList& aFilePaths);
	int WriteBsymFile(const QString& aOutput);
	bool AddMapFile(const QString& aMapFile, quint32 aLoadAddress = 0, quint32 aCodeSize = 0);
	bool AddRombuildLogFile(const QString& aFilePath);
	bool AddBsymFile(const QString& aFilePath);

	virtual bool Busy() const;

	TLookupResult Lookup(quint32 aAddress, TProcessId aProcessId = 0, TTimeInterval aTime=0) const; // In order to figure out dynamically loaded codesegs we need to know the time at which this address was valid, and what process we're looking at (since exe codesegs can share the same load address)

	quint32 RomChecksum() const;

	void AddCodeseg(const QString& aName, int aSize, TProcessId aProcessId, quint32 aRunAddr);

	// For use by CBTraceData - these aren't nice APIs but are limited by what BTrace provides
	void CodesegCreated(TTimeInterval aCreateTime, TKernelId aId, const QString& aName);
	void CodesegDestroyed(TTimeInterval aDestroyTime, TKernelId aId);
	void CodesegInfo(TTimeInterval aCurrentTime, TKernelId aId, int aSize, quint32 aRunAddr);
	void CodesegMapped(TTimeInterval aCurrentTime, TKernelId aCodesegId, TKernelId aProcessPtr); // aProcessPtr is *not* a process ID, it's a DProcess*

private:
	TLookupResult DoLookupInSymbolList(quint32 aAddress) const;
	const TLoadedCodeseg* FindLoadedCodesegById(TKernelId aId, TTimeInterval aTime) const;
	TLoadedCodeseg* FindLoadedCodesegById(TKernelId aId, TTimeInterval aTime);
	void UpdateCurrentCodesegListToTime(TTimeInterval aTime) const;
	const TLoadedCodeseg* LookupCodesegForAddress(quint32 aAddress, TProcessId aProcessId, TTimeInterval aTime) const;

protected:
	int CheckForBsym(const QString& aFilePath);
	virtual bool WasCancelled();
	void DoAddSymbolFile(QFile& aFile);

signals:
	void Progress(int aProgress);
	void SetProgressMaximum(int aMax);
	
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
	mutable QList<TLoadedCodeseg*> iCurrentCodesegs; // For the time "iCurrentTime". Not owned.
	QHash<QString, QString> iRomNameMappings; // For things that are renamed during rombuild
	quint32 iLastSeenChecksum; // Saved between AddRombuildLogFile and WriteBsym
	};

class CBsymFile : public QObject
	{
	Q_OBJECT;
	
public:
	static CBsymFile* New(const QString& aString, QObject* aParent = NULL);
	~CBsymFile();
	
	explicit CBsymFile(QObject* aParent = NULL);
	virtual quint32 RomChecksum() const = 0;
	
	virtual TLookupResult LookupRomSymbol(quint32 aAddress) const = 0;
	virtual TLookupResult Lookup(const QString& aCodesegName, quint32 aCodesegRelativeOffset) const = 0;

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
