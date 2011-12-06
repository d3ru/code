// extrabtrace.h
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
#ifndef EXTRABTRACE_H
#define EXTRABTRACE_H

#include <fshell/common.mmh>

#ifdef __KERNEL_MODE__
#include <e32def.h>
#else
#include <e32std.h>
#endif

namespace ExtraBTrace
	{

	enum TCategory
		{
		/**
		Supported by memoryaccess-fshell.ldd
		*/
		EPubSub = 213,

		/**
		Filesystem tracing from ffstracer (fshell addition).
		*/
		EFfsTrace = 214,

		};

	enum TPubSub
		{
		EPubSubIntPropertyChanged = 0,
		EPubSubDataPropertyChanged = 1,
		};

	// As used by category EFfsTrace. All filenames are 8-bit Collapse()'d.
	enum TFfsTrace
		{
		EFfsDelete = 0, // threadid, filename
		EFfsRename = 1, // threadid, filename, \0, [\0 for alignment], newname
		EUnusedWasEFfsReplace = 2, // Not used
		EFfsEntry = 3, // threadid, filename

		// These use handles
		EFfsFileSubClose = 4, // threadid, handle
		EFfsFileOpen = 5, // threadid, handleid, filename
		EFfsFileCreate = 6, // threadid, handleid, filename
		EFfsFileReplace = 7, // threadid, handleid, filename
		EFfsFileTemp = 8, // threadid, handleid, filename
		EFfsFileRename = 9, // threadid, handleid, newname
		EFfsFileRead = 10, // threadid, handleid, pos64, len
		EFfsFileWrite = 11, // threadid, handleid, pos64, len
		EFfsFilePriming = 12, // handleid, name

		EFfsPost = 128,
		};
	}

#ifndef __KERNEL_MODE__

class RExtraBtrace
	{
public:
	IMPORT_C RExtraBtrace();
	IMPORT_C TInt Connect();
	IMPORT_C void Close();

	IMPORT_C TInt EnableProfiling(TInt aSamplingPeriod); // in milliseconds. Pass aRate of zero to disable

public:
	IMPORT_C TInt Handle() const;

private:
	TInt32 iImpl[5];
	};

#endif // __KERNEL_MODE__

// Include the actual btrace header - do this after RExtraBtrace so that in principle someone could subclass it in their custom header
#ifdef FSHELL_CUSTOM_BTRACE_HEADER
#include FSHELL_CUSTOM_BTRACE_HEADER
#else
#include <e32btrace.h>
#endif

#endif // EXTRABTRACE_H
