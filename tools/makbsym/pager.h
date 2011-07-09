// pager.h
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

#ifndef PAGER_H
#define PAGER_H

#include <QObject>
#include <QHash>
#include <QVector>

class CBytePair;

// This class is responsible for byte-pair decompressing pages of data when requested
// And efficiently managing a page cache

class Pager : public QObject
	{
    Q_OBJECT
public:
	struct TPageInfo
		{
		quint32 iPageStartOffset;
		quint32 iPageDataSize;
		};

	explicit Pager(const QVector<TPageInfo>& aPageList, const uchar* aCompressedData, QObject *parent = 0);

	const uchar* GetData(quint32 aOffset, int aLength);
	void ReleaseData(int aNumRequests = 1); // Unpin the last n GetData calls

private:
	void CleanupCache();
	void DecompressPage(uchar* aDestinationBuf, const uchar* aSource, int aSourceSize);

private:
	const uchar* iCompressedData;
	struct SPageRange
		{
		SPageRange() : iFirstPage(0), iNumContiguousPages(0), iData(NULL) {}
		bool Contains(quint32 aAddress, int aLength) const;
		uchar* PtrForAddress(quint32 aAddress) const;

		quint32 iFirstPage;
		int iNumContiguousPages;
		uchar* iData;
		};

	QHash<quint32, SPageRange> iMappedPages; // This hash owns the iData pointers, but only for keys where key == value.iFirstPage
	QList<quint32> iPinnedRanges; // The iFirstPages of the given ranges

	QVector<TPageInfo> iPageInfo;
	QHash<quint32, SPageRange> iOldMappingsToRemove; // On next cleanup

	CBytePair* iBytePair;
	};

#endif // PAGER_H
