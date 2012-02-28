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

#include <QtGlobal>

typedef qint64 TTimeInterval; // Number of microseconds
#define KMaxTimeInterval (Q_INT64_C(0x7FFFFFFFFFFFFFFF))
#define KMaxEventDuration (0xFFFFFFFFu)
typedef quint32 TKernelId; // as per what btrace uses - NThread* or DProcess* usually
typedef quint32 TProcessId; // For when we want to be very specific we're talking about a process ID
typedef quint32 TThreadId;

#ifdef Q_OS_MACX
// Workaround for https://bugreports.qt.nokia.com/browse/QTBUG-22303
#define KGvPenWidth (0.1)
#else
#define KGvPenWidth (0.0)
#endif

#ifdef OVERRIDE_Q_ASSERT
#undef Q_ASSERT
#undef Q_ASSERT_X
void mb_assertionfail(const char *assertion, const char *file, int line);
void mb_assertionfail_x(const char *where, const char *what, const char *file, int line);
#define Q_ASSERT(cond) ((!(cond)) ? mb_assertionfail(#cond,__FILE__,__LINE__) : qt_noop())
#define Q_ASSERT_X(cond, where, what) ((!(cond)) ? mb_assertionfail_x(where, what,__FILE__,__LINE__) : qt_noop())
#endif

#define DEBUG_XY(x, y) qDebug(#x " = %d " #y " = %d", (int)(x), (int)(y))
#define ASSERT_GE(x, y) if (!((x) >= (y))) { DEBUG_XY(x, y); Q_ASSERT(x >= y); }
#define ASSERT_GT(x, y) if (!((x) >  (y))) { DEBUG_XY(x, y); Q_ASSERT(x >  y); }
#define ASSERT_LE(x, y) if (!((x) <= (y))) { DEBUG_XY(x, y); Q_ASSERT(x <= y); }
#define ASSERT_LT(x, y) if (!((x) <  (y))) { DEBUG_XY(x, y); Q_ASSERT(x <  y); }
#define ASSERT_EQ(x, y) if (!((x) == (y))) { DEBUG_XY(x, y); Q_ASSERT(x == y); }
#define ASSERT_NE(x, y) if (!((x) != (y))) { DEBUG_XY(x, y); Q_ASSERT(x != y); }

#define ASSERT_COMPILE(x) void __compile_time_assert(int __check[(x)?1:-1])

#endif
