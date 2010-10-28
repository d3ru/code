// btrace_appstart.cpp
// 
// Copyright (c) 2008 - 2010 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include "btrace_parser.h"
#include <fshell/common.mmh>


//
// CBtraceAppStart
//

EXPORT_C CBtraceAppStart* CBtraceAppStart::NewL(CBtraceReader& aReader, CBtraceContext& aContext)
	{
	CBtraceAppStart* self = new (ELeave) CBtraceAppStart(aReader, aContext);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
	}

CBtraceAppStart::CBtraceAppStart(CBtraceReader& aReader, CBtraceContext& aContext)
	: iReader(aReader), iContext(aContext)
	{
	}

EXPORT_C CBtraceAppStart::~CBtraceAppStart()
	{
	iReader.RemoveObserver(KAmTraceCategory, *this);
	iNotifs.Close();
	}

void CBtraceAppStart::ConstructL()
	{
	iReader.AddObserverL(KAmTraceCategory, *this);
	}

void CBtraceAppStart::HandleBtraceFrameL(const TBtraceFrame& aFrame)
	{
	if (aFrame.iCategory != KAmTraceCategory) return;

	switch (aFrame.iSubCategory)
		{
		case EAmTraceSubCategoryEvCapture:
			{
			const TUint32* data = reinterpret_cast<const TUint32*>(aFrame.iData.Ptr());
			const TAmTraceEventEvCapture event = static_cast<TAmTraceEventEvCapture>(*data++);

			switch (event)
				{
				case EAmTraceEventEvCaptureAppStart:
					{
					const TInt32 wgId = *data++;
					SeenAppStartL(aFrame.iTickCount, wgId);
					}
				break;
				
				default:
					// ignore the event
				break;
				};
			}
		break;

		default:
			// ignore anything we don't know about.
		break;
		};
	}

void CBtraceAppStart::SeenAppStartL(const TBtraceTickCount& aTickCount, TInt aWindowGroupId)
	{
	const TBtraceWindowGroupId* btraceWgId = iContext.FindWindowGroup(aWindowGroupId);
	if (btraceWgId)
		{
		TInt ii = iNotifs.Count();
		while (--ii >= 0)
			{
			TAppStartNotif& nt = iNotifs[ii];
			if (iContext.WindowGroupName(*btraceWgId).MatchF(nt.iWindowGroupNamePattern) != KErrNotFound)
				{
				MBtraceAppStartObserver& observer = nt.iObserver;
				if (nt.iPersistence == ENotificationOneShot)
					{
					iNotifs.Remove(ii);
					}
				observer.HandleAppStartL(aTickCount);
				}
			}	
		}
	}

EXPORT_C void CBtraceAppStart::NotifyAppStartL(MBtraceAppStartObserver& aObserver, const TDesC& aWindowGroupNamePattern)
	{
	NotifyAppStartL(aObserver, aWindowGroupNamePattern, ENotificationOneShot);
	}

EXPORT_C void CBtraceAppStart::NotifyAppStartL(MBtraceAppStartObserver& aObserver, const TDesC& aWindowGroupNamePattern, TBtraceNotificationPersistence aPersistence)
	{
	TAppStartNotif notify(aObserver, aWindowGroupNamePattern, aPersistence);
	User::LeaveIfError(iNotifs.Append(notify));
	}

EXPORT_C void CBtraceAppStart::CancelNotifyAppStart(MBtraceAppStartObserver& aObserver)
	{
	for (TInt i = (iNotifs.Count() - 1); i >= 0; --i)
		{
		if (&iNotifs[i].iObserver == &aObserver)
			{
			iNotifs.Remove(i);
			}
		}
	}


//
// CBtraceAppStart::TAppStartNotif
//

CBtraceAppStart::TAppStartNotif::TAppStartNotif(MBtraceAppStartObserver& aObserver, const TDesC& aWindowGroupNamePattern, TBtraceNotificationPersistence aPersistence)
	: iObserver(aObserver), iWindowGroupNamePattern(aWindowGroupNamePattern), iPersistence(aPersistence)
	{
	}
