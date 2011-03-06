// file.cpp
// 
// Copyright (c) 2006 - 2010 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include "file.h"
#include "log.h"
#include <fshell/ltkutils.h>
using LtkUtils::RLtkBuf8;
using LtkUtils::RLtkBuf;

CIoFile* CIoFile::NewLC(RFs& aFs, const TDesC& aName, RIoFile::TMode aMode)
	{
	CIoFile* self = new(ELeave) CIoFile();
	CleanupClosePushL(*self);
	self->ConstructL(aFs, aName, aMode);
	return self;
	}

CIoFile::~CIoFile()
	{
	iFile.Close();
	iTempReadBuf.Close();
	}

TBool CIoFile::IsType(RIoHandle::TType aType) const
	{
	return ((aType == RIoHandle::EEndPoint) || (aType == RIoHandle::EFile));
	}

void CIoFile::IorepReadL(MIoReader& aReader)
	{
	TInt fileSize;
	User::LeaveIfError(iFile.Size(fileSize));
	if (iPos < fileSize)
		{
		TDes8* readBuf8 = aReader.IorReadBuf8();

		if (readBuf8)
			{
			User::LeaveIfError(iFile.Read(*readBuf8));
			iPos += readBuf8->Length();
			aReader.IorDataBuffered(readBuf8->Length());
			}
		else
			{
			TDes& readBuf = aReader.IorReadBuf();
			iTempReadBuf.Zero();
			HBufC8* narrowBuf = HBufC8::NewLC(readBuf.MaxLength());
			TPtr8 narrowBufPtr(narrowBuf->Des());
			User::LeaveIfError(iFile.Read(narrowBufPtr, readBuf.MaxLength()));
			iPos += narrowBuf->Length();
			if (aReader.IorwMode() == RIoReadWriteHandle::EText)
				{
				iTempReadBuf.AppendUtf8L(narrowBufPtr);
				if (iPos >= fileSize)
					{
					iTempReadBuf.FinalizeUtf8();
					}
				readBuf.Copy(iTempReadBuf);
				}
			else
				{
				readBuf.Copy(narrowBufPtr);
				}

			aReader.IorDataBuffered(readBuf.Length());
			CleanupStack::PopAndDestroy(narrowBuf);
			}
		if (iPos >= fileSize)
			{
			aReader.IorReadComplete(KErrNone);
			}
		}
	else
		{
		aReader.IorReadComplete(KErrEof);
		}
	}

void CIoFile::IowepWriteL(MIoWriter& aWriter)
	{
	TInt err = KErrNone;
	if (aWriter.IowNarrowWrite())
		{
		RLtkBuf8 buf;
		buf.CreateLC(aWriter.IowWriteLength());
		aWriter.IowWrite(buf);
		err = iFile.Write(buf);
		CleanupStack::PopAndDestroy(&buf);
		}
	else
		{
		RLtkBuf buf;
		buf.CreateLC(aWriter.IowWriteLength());
		aWriter.IowWrite(buf);
		if (aWriter.IorwMode() == RIoReadWriteHandle::EText)
			{
			// Convert to UTF-8
			HBufC8* narrowBuf = LtkUtils::Utf8L(buf);
			err = iFile.Write(*narrowBuf);
			delete narrowBuf;
			}
		else
			{
			// Just collapse it
			err = iFile.Write(buf.Collapse());
			}
		CleanupStack::PopAndDestroy(&buf);
		}
	aWriter.IowComplete(err);
	}

void CIoFile::IowepWriteCancel(MIoWriter&)
	{
	}

CIoFile::CIoFile()
	{
	}

void CIoFile::ConstructL(RFs& aFs, const TDesC& aName, RIoFile::TMode aMode)
	{
	LOG(CIoLog::Printf(_L("Attempting to create file 0x%08x (\"%S\")"), this, &aName));
	switch (aMode)
		{
		case RIoFile::ERead:
			{
			User::LeaveIfError(iFile.Open(aFs, aName, EFileRead | EFileShareReadersOnly));
			break;
			}
		case RIoFile::EOverwrite:
			{
			User::LeaveIfError(iFile.Replace(aFs, aName, EFileWrite));
			break;
			}
		case RIoFile::EAppend:
			{
			TInt err = iFile.Open(aFs, aName, EFileWrite);
			if (err == KErrNotFound)
				{
				err = iFile.Create(aFs, aName, EFileWrite);
				}
			User::LeaveIfError(err);
			TInt pos = 0;
			User::LeaveIfError(iFile.Seek(ESeekEnd, pos));
			break;
			}
		default:
			{
			User::Leave(KErrNotSupported);
			}
		}
	}

