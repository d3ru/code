// makbsym.h
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

#ifndef MAKBSYM_H
#define MAKBSYM_H

#include <QtCore>

class TProgressPrinter : public QObject
	{
	Q_OBJECT;

public slots:
	void Progress(int aProgress);
	void SetProgressMaximum(int aMax);

private:
	int iMax;
	};

#endif // MAKBSYM_H
