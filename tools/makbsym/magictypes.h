// magictypes.h
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

#ifndef MAGICTYPES_H
#define MAGICTYPES_H

#include <QtCore>

typedef qint64 TTimeInterval; // Number of microseconds
typedef quint32 TKernelId; // as per what btrace uses - NThread* or DProcess* usually
typedef quint32 TProcessId; // For when we want to be very specific we're talking about a process ID

#define DEBUG_XY(x, y) qDebug(#x " = %d " #y " = %d", (int)(x), (int)(y))
#define ASSERT_GE(x, y) if (!((x) >= (y))) { DEBUG_XY(x, y); Q_ASSERT(x >= y); }
#define ASSERT_GT(x, y) if (!((x) >  (y))) { DEBUG_XY(x, y); Q_ASSERT(x >  y); }
#define ASSERT_LE(x, y) if (!((x) <= (y))) { DEBUG_XY(x, y); Q_ASSERT(x <= y); }
#define ASSERT_LT(x, y) if (!((x) <  (y))) { DEBUG_XY(x, y); Q_ASSERT(x <  y); }
#define ASSERT_EQ(x, y) if (!((x) == (y))) { DEBUG_XY(x, y); Q_ASSERT(x == y); }
#define ASSERT_NE(x, y) if (!((x) != (y))) { DEBUG_XY(x, y); Q_ASSERT(x != y); }

#endif
