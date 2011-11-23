// memmap.h
//
// Copyright (c) 2010 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#ifndef MEMMAP_H
#define MEMMAP_H

#include <fshell/memoryaccesscmd.h>

using namespace IoUtils;

class TMemArea;
class TChunkKernelInfo;

#define MAX(x,y) ((x)>(y)?(x):(y))
#define MAX3(x,y,z) MAX(MAX((x),(y)),(z))

class CCmdMemmap : public CMemoryAccessCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdMemmap();
private:
	CCmdMemmap();
	void ShowMapForProcessL(TUint aPid, TFullName& aProcessName);
	void PrintAreasL(RPointerArray<TMemArea>& aAreas);
	void AddAreaL(RPointerArray<TMemArea>& aAreas, const TMemArea& aArea);
	void AddChunkAreaL(RPointerArray<TMemArea>& aAreas, TLinAddr aAddress, const TChunkKernelInfo& aInfo);
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	TUint iPid;
	HBufC* iMatch;
	TBool iDisplaySize;
	TBool iHumanReadableSizes;
	TBool iShowCommitted;

	TFullName iFullName;
	// Needlessly cunning definitions to save memory. Why can't unions actually work? 
	TUint8 iInfoBuf[MAX3(sizeof(TChunkKernelInfo), sizeof(TCodeSegKernelInfo), sizeof(TThreadKernelInfo))];

	TFindProcess iFindProc;
	TFindThread iFindThread;
	RBuf8 iAddressesBuf;
	};

#endif
