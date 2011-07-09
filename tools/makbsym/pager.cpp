// pager.cpp
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

#include "pager.h"
#include "byte_pair.h"

Pager::Pager(const QVector<TPageInfo>& aPageList, const uchar* aCompressedData, QObject* parent)
	: QObject(parent), iCompressedData(aCompressedData), iPageInfo(aPageList)
	{
	}

const uchar* Pager::GetData(quint32 aOffset, int aLength)
	{
	Q_ASSERT(aLength != 0);
	quint32 pageAddr = aOffset & ~0xFFF;
	quint32 endPageAddr = (aOffset + aLength + 0xFFF) & ~0xFFF;
	int numPages = (endPageAddr - pageAddr) >> 12;
	int startPageIndex = pageAddr >> 12;

	SPageRange pageRange = iMappedPages.value(pageAddr);
	if (pageRange.Contains(aOffset, aLength))
		{
		// Request fits inside an already existing contiguous range of mapped pages
		iPinnedRanges.append(pageRange.iFirstPage);
		return (pageRange.PtrForAddress(aOffset));
		}

	// else need to map in this region, potentially duplicating some pages if other stuff in the mapping is pinned

	Q_ASSERT(startPageIndex + numPages <= iPageInfo.count());
	uchar* region = (uchar*)malloc(numPages * 4096);
	SPageRange range;
	range.iFirstPage = pageAddr;
	range.iNumContiguousPages = numPages;
	range.iData = region;
	for (int i = 0; i < numPages; i++)
		{
		const TPageInfo& pageinfo = iPageInfo[startPageIndex + i];
		//qDebug("Decompressing page %d at compressed offset %x of size %d", startPageIndex + i, pageinfo.iPageStartOffset, pageinfo.iPageDataSize);
		DecompressPage(region + i*4096, iCompressedData + pageinfo.iPageStartOffset, pageinfo.iPageDataSize);

		// Check for any old mappings that we're obsoleting
		SPageRange oldMapping = iMappedPages.value(pageAddr + i*4096);
		if (oldMapping.iData && oldMapping.iFirstPage == pageAddr + i*4096)
			{
			iOldMappingsToRemove.insert(oldMapping.iFirstPage, oldMapping);
			}

		iMappedPages.insert(pageAddr + i*4096, range);
		}
	iPinnedRanges.append(range.iFirstPage);
	return range.PtrForAddress(aOffset);
	}

void Pager::ReleaseData(int aNumRequests)
	{
	while (aNumRequests--)
		{
		iPinnedRanges.removeLast();
		}
	CleanupCache();
	}

void Pager::CleanupCache()
	{
	//TODO
	}

bool Pager::SPageRange::Contains(quint32 aAddress, int aLength) const
	{
	return iData != NULL && aAddress >= iFirstPage && aAddress + aLength <= iFirstPage + iNumContiguousPages*4096;
	}

uchar* Pager::SPageRange::PtrForAddress(quint32 aAddress) const
	{
	Q_ASSERT(Contains(aAddress, 0));
	return iData + (aAddress - iFirstPage);
	}

void Pager::DecompressPage(uchar* aDestinationBuf, const uchar* aSource, int aSourceSize)
	{
	if (!iBytePair) iBytePair = new CBytePair;
	/*int decompressedSize =*/ BytePairDecompress(aDestinationBuf, (uchar*)aSource, aSourceSize, iBytePair); // Another API that doesn't understand const...
	//qDebug("Decompressing page at %x of size %d, result size is %d", (uint)aSource, aSourceSize, decompressedSize);
	//Q_ASSERT(decompressedSize == 4096);
	}
