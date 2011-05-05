// muxcons_types.h
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

#ifndef MUXCONS_TYPES_H
#define MUXCONS_TYPES_H

enum TConsoleCommandOpCode
	{
	// Sent from fshell to PC (well, muxserver -> muxcons.exe)
	ENewConsoleCreated = 0, // [id]
	EWriteData = 1,
	ECursorPosRequest = 2,
	ESetAbsCursorPos = 3,
	ESetRelCursorPos = 4,
	ESetCursorHeight = 5,
	EScreenSizeRequest = 6,
	ESetTitle = 7,
	EClearScreen = 8,
	EClearToEndOfLine = 9,
	EConsoleIsDead = 10,
	ESetAttributes = 11,
	EWriteData8 = 12,
	ERequestFile = 13,
	ECancelRequestFile = 14,

	// A few opcodes specific to the symbian client-serv parts (muxserver <-> muxcons.dll)
	EKeypressRequest = 50,
	ECancelReadData = 51,
	ENotifySizeChange = 53,
	ECancelNotifySizeChange = 54,
	EReadData8 = 55,

	// Sent from PC to fshell (ie muxcons.exe -> muxserver)
	EKeypressData = 100,
	ECursorPosResult = 101,
	EScreenSizeResult = 102,
	EScreenSizeChanged = 103,
	};

enum TMuxConsCommand
	{
	EListConsoles = -1,
	EConsoleShouldClose = -2,
	ECreateNewConsole = -3,
	
	EShutdown = -5,
	EPutFile = -6,
	EContinuePutFile = -7,
	ELaunchProcess = -8,
	ELaunchNestedMuxcons = -10, // Like ELaunchProcess("muxserver.exe") but adds the special args needed for nested muxservers to work
	ERequestFileRefused = -11,
	ENotifyVersion = -12, // For muxserver.exe -> PC, we use the EPing response, but I don't want to modify the EPing sent from muxcons.exe as that code is delicate enough...

	ENestedMuxServerCommandBase = -85,
	// We only have room in the protocol for 15 nested mux sessions
	ENestedMuxServerCommandEnd = -99,

	ECustomCommandBase = -100,
	EDrvInfo = ECustomCommandBase,
	ELs = -101,
	EGetFile = -102,
	EScreenshot = -103,
	

	EPing = (int)0xe19a8f1b, // This is a magic number, the important bit being that the first 3 bytes are a valid UTF-8 character and the 1B on the end will cause fshell to clear the line if it's actually fshell we're talking to
	// ^ TODO This comment is no longer true/useful...
	};

enum TMuxVersion
	{
	EVersion1_0 = 0x00010000,
	EVersion1_1 = 0x00010001, // Supports ENotifyVersion and ERequestFile / DataRequester interface
	};

#define KPacketHeader (0x4D580000) // 4D58 is 'MX'
#define KPacketHeaderMask (0xFFFF0000u)
#define KPacketLengthMask (0x00000FFFu)
#define KMaxPacketLen (1024)
#define KMaxCustomCommandResultPayload (KMaxPacketLen - 12) // header+packetlen, commandid, err
#define KDefaultMuxPort (59400)
#define IS_NEST_CHANNEL(_channel) ((_channel) <= ENestedMuxServerCommandBase && (_channel) >= ENestedMuxServerCommandEnd)
#define NEST_ID_FROM_COMMAND_ID(_id) (ENestedMuxServerCommandBase - (_id) + 1)
#define COMMAND_ID_FROM_NEST_ID(_id) (ENestedMuxServerCommandBase + 1 - (_id))
#define COMMAND_ID_FROM_NEST_BYTE(_b) COMMAND_ID_FROM_NEST_ID((_b) >> 4)

/*
PC -> muxserver
===============
Console messages:
	int packetlen
	int muxchannelid // destination id
	TConsoleCommandOpCode command // data read/to write, get/set cursor pos etc
	<payload>

Muxcons commands:
	int packetlen
	int commandid // commands are all negative so can be destinguished from a console message (where this parameter would be a muxchannelid)
	<payload>

muxserver -> PC
===============

Messages from console to PC:
	int packetlen
	int muxchannelid
	TConsoleCommandOpCode opcode
	<payload>

Muxcons command results:
	int packetlen
	TMuxConsCommand commandid // The id of the command this is the result for - means we can't run 2 instances of a commmand concurrently. Should be ok.
	[int error] // included if commandids is {EPutFile, EContinuePutFile, ELaunchProcess} or for anything < ECustomCommandBase
	<payload>

Note: packetlen and muxchannelid/commandid are always BIG-ENDIAN so that the code for handling accidentally sending EPing commands straight to fshell didn't make my head explode
*/

#endif
