// tpips.cpp
// 
// Copyright (c) 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

//#include <fshell/ioutils.h>
//#include <fshell/ltkutils.h>
#include <stdapis/unistd.h>
#include <stdapis/stdio.h>

int main(int argc, char* argv[])
	{
	(void)argc;
	(void)argv;

	fprintf(stdout, "STDOUT!!!!\r\n");
	fflush(stdout);
	fprintf(stderr, "STDERR!!!!\r\n");
	//RDebug::Printf("Flushed!");

	
	return 0;
	}
