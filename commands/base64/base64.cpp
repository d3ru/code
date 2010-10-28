// base64.cpp
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

#include <fshell/ioutils.h>
#include <fshell/common.mmh>

using namespace IoUtils;

const TInt KBlockSize = 512;
const TInt KLineLength = 76;
const TUint8 KBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const TUint8 KPadCharacter = '=';

const TUint8 KInvBase64[] =
	{
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x3e,
	0x0,
	0x0,
	0x0,
	0x3f,
	0x34,
	0x35,
	0x36,
	0x37,
	0x38,
	0x39,
	0x3a,
	0x3b,
	0x3c,
	0x3d,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x1,
	0x2,
	0x3,
	0x4,
	0x5,
	0x6,
	0x7,
	0x8,
	0x9,
	0xa,
	0xb,
	0xc,
	0xd,
	0xe,
	0xf,
	0x10,
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x16,
	0x17,
	0x18,
	0x19,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x1a,
	0x1b,
	0x1c,
	0x1d,
	0x1e,
	0x1f,
	0x20,
	0x21,
	0x22,
	0x23,
	0x24,
	0x25,
	0x26,
	0x27,
	0x28,
	0x29,
	0x2a,
	0x2b,
	0x2c,
	0x2d,
	0x2e,
	0x2f,
	0x30,
	0x31,
	0x32,
	0x33
	};

_LIT(KNewLine, "\r\n");
_LIT(KCr, "\r");
_LIT(KLf, "\n");


class CCmdBase64 : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdBase64();
private:
	CCmdBase64();
	void DecodeL();
	void EncodeL();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	enum 
		{
		EDecode,
		EEncode
		} iOperation;
	TFileName2 iFileName;
	TBool iVerbose;
	TBool iOverwrite;
	};

EXE_BOILER_PLATE(CCmdBase64)

CCommandBase* CCmdBase64::NewLC()
	{
	CCmdBase64* self = new(ELeave) CCmdBase64();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdBase64::~CCmdBase64()
	{
	}

CCmdBase64::CCmdBase64()
	{
	}

void CCmdBase64::DecodeL()
	{
	if (!iOverwrite)
		{
		LeaveIfFileExists(iFileName);
		}

	User::LeaveIfError(Stdin().CaptureAllKeys()); // To iosrv buffering incoming data if we're not keeping up.
	Stdin().SetReadModeL(RIoReadHandle::ELine);

	RFile file;
	LeaveIfErr(file.Replace(FsL(), iFileName, EFileWrite | EFileStream), _L("Unabled to open '%S' for writing"), &iFileName);
	CleanupClosePushL(file);

	TBuf<KLineLength + 2> lineBuf;
	TBuf8<(KLineLength / 4) * 3> outputBuf;
	TBool finished(EFalse);
	TBool started(EFalse);
	while (!finished)
		{
		TInt err = Stdin().Read(lineBuf);
		if (err == KErrNone)
			{
			if (iVerbose)
				{
				Printf(_L("Read %d chars:\r\n'%S'\r\n"), lineBuf.Length(), &lineBuf);
				}
			if ((lineBuf == KNewLine) || (lineBuf == KCr) || (lineBuf == KLf))
				{
				if (started)
					{
					finished = ETrue;
					}
				}
			else
				{
				if (lineBuf.Right(2) == KNewLine)
					{
					lineBuf.SetLength(lineBuf.Length() - 2);
					}
				if ((lineBuf.Right(1) == KCr) || (lineBuf.Right(1) == KLf))
					{
					lineBuf.SetLength(lineBuf.Length() - 1);
					}
				const TInt lineLength = lineBuf.Length();
				if ((lineLength % 4) > 0)
					{
					LeaveIfErr(KErrArgument, _L("Invalid base 64 encoded line (not a multiple of 4 characters in length):\r\n%S\r\n"), &lineBuf);
					}

				started = ETrue;
				outputBuf.Zero();

				for (TInt i = 0; i < lineLength; i += 4)
					{
					TInt n = ((TInt)KInvBase64[lineBuf[i]] << 18) + ((TInt)KInvBase64[lineBuf[i + 1]] << 12) + ((TInt)KInvBase64[lineBuf[i + 2]] << 6) + (TInt)KInvBase64[lineBuf[i + 3]];

					if (lineBuf[i + 2] == KPadCharacter)
						{
						// Two pad characters
						outputBuf.Append((n >> 16) & 0x000000FF);
						}
					else if (lineBuf[i + 3] == KPadCharacter)
						{
						// One pad character
						outputBuf.Append((n >> 16) & 0x000000FF);
						outputBuf.Append((n >> 8) & 0x000000FF);
						}
					else
						{
						outputBuf.Append((n >> 16) & 0x000000FF);
						outputBuf.Append((n >> 8) & 0x000000FF);
						outputBuf.Append(n & 0x000000FF);
						}
					}

				LeaveIfErr(file.Write(outputBuf), _L("Failed to write to '%S'"), &iFileName);
				if (iVerbose)
					{
					Printf(_L("Wrote %d bytes to '%S'\r\n"), outputBuf.Length(), &iFileName);
					}
				}
			}
		else if (err == KErrEof)
			{
			finished = ETrue;
			}
		else
			{
			LeaveIfErr(err, _L("Couldn't read STDIN"));
			}
		}

	CleanupStack::PopAndDestroy(&file);
	}

void CCmdBase64::EncodeL()
	{
	LeaveIfFileNotFound(iFileName);

	RFile file;
	User::LeaveIfError(file.Open(FsL(), iFileName, EFileRead | EFileStream));
	CleanupClosePushL(file);

	TBuf8<KBlockSize> inputBuf;
	TBuf<KLineLength + 2> outputBuf;
	TBool finished(EFalse);
	while (!finished)
		{
		TPtr8 ptr((TUint8*)inputBuf.Ptr() + inputBuf.Length(), 0, inputBuf.MaxLength() - inputBuf.Length());
		LeaveIfErr(file.Read(ptr), _L("Couldn't read from '%S'"), &iFileName);

		if (ptr.Length() > 0)
			{
			inputBuf.SetLength(inputBuf.Length() + ptr.Length());
			const TInt inputBufLength = inputBuf.Length();
			const TInt excess = inputBufLength % 3;
			const TInt bytesToProcess = inputBufLength - excess;

			for (TInt i = 0; i < bytesToProcess; i += 3)
				{
				// Combine the next three bytes into a 24 bit number.
				TInt n = ((TInt)inputBuf[i] << 16) + ((TInt)inputBuf[i + 1] << 8) + (TInt)inputBuf[i + 2];

				// Split the 24-bit number into four 6-bit numbers.
				TUint8 n0 = (TUint8)(n >> 18) & 0x3F;
				TUint8 n1 = (TUint8)(n >> 12) & 0x3F;
				TUint8 n2 = (TUint8)(n >> 6) & 0x3F;
				TUint8 n3 = (TUint8)n & 0x3F;

				// Buffer the base64 encoded equivalent.
				outputBuf.Append(KBase64Chars[n0]);
				outputBuf.Append(KBase64Chars[n1]);
				outputBuf.Append(KBase64Chars[n2]);
				outputBuf.Append(KBase64Chars[n3]);

				// Flush output buffer if it's full.
				if (outputBuf.Length() == KLineLength)
					{
					outputBuf.Append(KNewLine);
					Write(outputBuf);
					outputBuf.Zero();
					}
				}

			inputBuf.Delete(0, inputBufLength - excess);
			}
		else
			{
			// Process what's left over in inputBuf from the previous successful read, padding as required.
			const TInt inputBufLength = inputBuf.Length();
			if (inputBufLength > 0)
				{
				TInt n = (TInt)inputBuf[0] << 16;
				if (inputBufLength > 1)
					{
					n += (TInt)inputBuf[1] << 8;
					if (inputBufLength > 2)
						{
						n += (TInt)inputBuf[2];
						}
					}

				TUint8 n0 = (TUint8)(n >> 18) & 0x3F;
				TUint8 n1 = (TUint8)(n >> 12) & 0x3F;
				TUint8 n2 = (TUint8)(n >> 6) & 0x3F;
				TUint8 n3 = (TUint8)n & 0x3F;

				outputBuf.Append(KBase64Chars[n0]);
				outputBuf.Append(KBase64Chars[n1]);
				if (inputBufLength > 1)
					{
					outputBuf.Append(KBase64Chars[n2]);
					if (inputBufLength > 2)
						{
						outputBuf.Append(KBase64Chars[n3]);
						}
					}

				for (TInt i = inputBufLength; i < 3; ++i)
					{
					outputBuf.Append('=');
					}
				}

			if (outputBuf.Length() > 0)
				{
				outputBuf.Append(KNewLine);
				Write(outputBuf);
				}

			finished = ETrue;
			}
		}

	CleanupStack::PopAndDestroy(&file);
	}

const TDesC& CCmdBase64::Name() const
	{
	_LIT(KName, "base64");	
	return KName;
	}

void CCmdBase64::ArgumentsL(RCommandArgumentList& aArguments)
	{
	_LIT(KArgOperation, "operation");
	aArguments.AppendEnumL((TInt&)iOperation, KArgOperation);

	_LIT(KArgFilename, "filename");
	aArguments.AppendFileNameL(iFileName, KArgFilename);
	}

void CCmdBase64::OptionsL(RCommandOptionList& aOptions)
	{
	_LIT(KOptVerbose, "verbose");
	aOptions.AppendBoolL(iVerbose, KOptVerbose);

	_LIT(KOptOverwrite, "overwrite");
	aOptions.AppendBoolL(iOverwrite, KOptOverwrite);
	}

void CCmdBase64::DoRunL()
	{
	switch (iOperation)
		{
		case EDecode:
			DecodeL();
			break;
		case EEncode:
			EncodeL();
			break;
		default:
			ASSERT(EFalse);
		}
	}
