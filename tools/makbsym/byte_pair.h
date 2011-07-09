/*
* Copyright (c) 2005-2009 Nokia Corporation and/or its subsidiary(-ies).
* All rights reserved.
* This component and the accompanying materials are made available
* under the terms of the License "Eclipse Public License v1.0"
* which accompanies this distribution, and is available
* at the URL "http://www.eclipse.org/legal/epl-v10.html".
*
* Initial Contributors:
* Nokia Corporation - initial contribution.
*
* Contributors:
* Minor header include modifications by thomas.sutcliffe@accenture.com
*
* Description: 
*
*/

#ifndef BYTE_PAIR_H
#define BYTE_PAIR_H

 
#ifdef _MSC_VER 
 #if (_MSC_VER > 1200)
  #define __MSVCDOTNET__ 1
  #include <sstream>
  #include <iomanip>
 #else //!__MSVCDOTNET__
  #include <strstrea.h>
  #include <iomanip.h>
 #endif //__MSVCDOTNET__
#else // !__VC32__
#ifdef __TOOLS2__ 
#include <sstream>
#include <iomanip>
#include <iostream>
using namespace std;
#else
 #include <strstream.h>
 #include <iomanip.h>
 #endif
#endif // __VC32__

//#include <myassert.h>
//#define DEBUG_ASSERT
#ifdef DEBUG_ASSERT
void myassert(int c);
#else
#define myassert(c) 
#endif

#include <QtCore/QtCore> // TOMSCI added
#include "symbiandefs.h" //TOMSCI added
//TOMSCI removed #include <e32cmn.h>
typedef struct {
    TUint16 Count;
    TUint16 Index; 
} TPairCountIndex;

typedef struct {
    TUint16 Pair;
    TUint16 Next;
    TUint16 Prev;
    TUint16 Pos;
} TPair;
typedef struct {
    TUint16 Pos;
    TUint16 Next;
} TPos;

const TInt MaxBlockSize = 0x1000;

const TUint16 PosEnd = 0xffff;
const TUint16 PosHead = 0xfffe;
const TUint8 ByteRemoved = 'R';
const TUint8 ByteMarked = 'M';
const TUint8 ByteHead = 'H';
const TUint8 ByteTail = 'T';
const TUint8 ByteNew = 'N';
class CBytePair {
    private:
        TInt    marker;
        TInt    markerCount;
        TUint8  Mask[MaxBlockSize];
        TUint16 ByteCount[0x100];
        TUint16 ByteIndex[0x100];
        TUint16 BytePos[MaxBlockSize];
        TPairCountIndex PairCount[0x10000];
        TPair   PairBuffer[MaxBlockSize*2];
        TInt    PairBufferNext;
        TPos    PairPos[MaxBlockSize*3];
        TUint16 PairPosNext;
        TUint16 PairLists[MaxBlockSize/2+2];
        TInt    PairListHigh;

        void CountBytes(TUint8* data, TInt size) {
	    memset(reinterpret_cast<char*>(ByteCount),0,sizeof(ByteCount));
            memset(reinterpret_cast<char*>(ByteIndex),0xff,sizeof(ByteIndex));
            memset(reinterpret_cast<char*>(BytePos),0xff, sizeof(BytePos));
            TUint8* dataEnd = data + size;
            int pos = 0;
            while(data < dataEnd) {
                BytePos[pos] = ByteIndex[*data];
                ByteIndex[*data] = (TUint16)pos;
                pos ++;
                ++ByteCount[*data++];
            }
        }
        inline void ByteUsed(TInt b) {
            ByteCount[b] = 0xffff;
        }
        int TieBreak(int b1,int b2) {
            return -ByteCount[b1]-ByteCount[b2];
        }
        void SortN(TUint16 *a, TInt n);
        void InsertN(TUint16 *a, TInt n, TUint16 v);
        TInt PosN(TUint16 index, TInt minFrequency);

        void Initialize(TUint8* data, TInt size);
        TInt MostCommonPair(TInt& pair, TInt minFrequency);
        TInt LeastCommonByte(TInt& byte);
        inline void InsertPair(const TUint16 pair, const TUint16 pos) {
            //ClockInsert1 = clock();
            //cout << "Pair:" << hex << setw(4) << setfill ('0') << pair << endl;
            TUint16 count = PairCount[pair].Count;
            if (0==count) {
                PairCount[pair].Index = (TUint16)PairBufferNext;
                if (PairLists[1] != PosEnd) {
                    PairBuffer[PairLists[1]].Prev = (TUint16)PairBufferNext;
                }
                PairBuffer[PairBufferNext].Next = PairLists[1];
                PairBuffer[PairBufferNext].Prev = PosHead;
                PairLists[1] = (TUint16)PairBufferNext;
                PairBuffer[PairBufferNext].Pair = pair;
                PairBuffer[PairBufferNext].Pos = PairPosNext;
                PairBufferNext ++;
                myassert(PairBufferNext < MaxBlockSize*2);
                PairPos[PairPosNext].Pos = pos; 
                PairPos[PairPosNext].Next = PosEnd;
            } else {
                TUint16 index = PairCount[pair].Index;
               
                if (PairBuffer[index].Next == PosEnd){
                    if (PairBuffer[index].Prev == PosHead){
                        PairLists[count] = PosEnd;
                    } else {
                        PairBuffer[PairBuffer[index].Prev].Next = PosEnd;
                    }
                } else {
                    if (PairBuffer[index].Prev == PosHead){
                        PairLists[count] = PairBuffer[index].Next;
                        PairBuffer[PairBuffer[index].Next].Prev = PosHead;
                    } else {
                        PairBuffer[PairBuffer[index].Prev].Next = PairBuffer[index].Next;
                        PairBuffer[PairBuffer[index].Next].Prev = PairBuffer[index].Prev;
                    }
                }
                
                if (PairLists[count+1] != PosEnd){
                    PairBuffer[PairLists[count+1]].Prev = index;
                    PairBuffer[index].Next = PairLists[count+1];
                    PairBuffer[index].Prev = PosHead;
                    PairLists[count+1] = index;
                }else{
                    PairLists[count+1] = index;
                    PairBuffer[index].Next = PosEnd;
                    PairBuffer[index].Prev = PosHead;
                }

                PairPos[PairPosNext].Pos = pos;
                PairPos[PairPosNext].Next = PairBuffer[index].Pos;
                PairBuffer[index].Pos = PairPosNext;
            }
            PairPosNext ++;
            
            myassert(PairPosNext < MaxBlockSize*3);
            PairCount[pair].Count ++;
            if (PairCount[pair].Count > PairListHigh) 
                PairListHigh = PairCount[pair].Count; 
        }
        inline void RemovePair(const TUint16 pair, const TUint16 pos){
            //ClockRemove1 = clock();
            TUint16 count = PairCount[pair].Count;
            TUint16 index = PairCount[pair].Index;
            if (count == 1 ) {
                PairCount[pair].Count = 0;
                PairCount[pair].Index = PosEnd;
                return;
            }
            
            myassert(index != PosEnd);
            TUint16 *posnextp = &PairBuffer[index].Pos;
            while (*posnextp != PosEnd){
                if (PairPos[*posnextp].Pos == pos)
                    break;
                posnextp = &PairPos[*posnextp].Next;
            }
            myassert(*posnextp != PosEnd);
            *posnextp = PairPos[*posnextp].Next;
            
            if (PairBuffer[index].Next == PosEnd){
                if (PairBuffer[index].Prev == PosHead){
                    PairLists[count] = PosEnd;
                } else {
                    PairBuffer[PairBuffer[index].Prev].Next = PosEnd;
                }
            } else {
                if (PairBuffer[index].Prev == PosHead){
                    PairLists[count] = PairBuffer[index].Next;
                    PairBuffer[PairBuffer[index].Next].Prev = PosHead;
                } else {
                    PairBuffer[PairBuffer[index].Prev].Next = PairBuffer[index].Next;
                    PairBuffer[PairBuffer[index].Next].Prev = PairBuffer[index].Prev;
                }
            }
            myassert(PairCount[pair].Count > 0);
            PairCount[pair].Count --;
            if (PairCount[pair].Count == 0)
                PairCount[pair].Index = PosEnd;
            
            count = PairCount[pair].Count;
            if (count > 0) {
                if (PairLists[count] != PosEnd){
                    PairBuffer[PairLists[count]].Prev = index;
                    PairBuffer[index].Next = PairLists[count];
                    PairBuffer[index].Prev = PosHead;
                    PairLists[count] = index;
                }else{
                    PairLists[count] = index;
                    PairBuffer[index].Next = PosEnd;
                    PairBuffer[index].Prev = PosHead;
                }
            }            
            while (PairLists[PairListHigh] == PosEnd) {
                PairListHigh --;
            }             
        }
        inline void GetPairBackward (TUint8 *data, TUint16 pos, TUint16 &pairpos, TUint16 &pair) {
            myassert(pos <MaxBlockSize);
            myassert(Mask[pos] != ByteMarked);
            myassert(Mask[pos] != ByteRemoved);
            TUint8 b = data[pos];
            if (pos == 0) {
                pair = 0;
                pairpos = PosEnd;
                return;
            }
            pos --;
            while ((pos>0) && (Mask[pos] == ByteRemoved)){
                pos --;
            }
            if ((Mask[pos] == ByteRemoved) || (Mask[pos] == ByteMarked)) {
                pair = 0;
                pairpos = PosEnd;
            } else {
                pair = (TUint16)((b << 8) | data[pos]);
                pairpos = pos;
            }
        }
        
        inline void GetPairForward (TUint8 *data, TUint16 pos, TInt size, TUint16 &pairpos, TUint16 &pair) {
            myassert(Mask[pos] != ByteMarked);
            myassert(Mask[pos] != ByteRemoved);
            myassert(pos < size);
            TUint8 b = data[pos];
            pairpos = pos;
            pos ++;
            while ((pos < size) && (Mask[pos] == ByteRemoved))
                pos++;
            if ((pos == size) || (Mask[pos] == ByteMarked)) {
                pair = 0;
                pairpos = PosEnd;
            } else {
                pair = (TUint16)(b | (data[pos] << 8));
            }
        }
#ifdef  __TOOLS2__        
        void DumpList(TUint8 *data, TInt size) {
            int i, j;
            cout << "src: " << dec << size << " bytes"<< endl;
            for (i=0;i<size;i+=16){
                cout << endl;
                cout << hex;
                cout << setfill('0') << setw(4) << right << i << " ";
                for (j=0;j<16;j++) {
                    cout << setfill ('0') << setw(2) << right << (unsigned int)data[i+j] << " ";
                }
                cout << "    ";
                for (j=0;j<16;j++) {
                    char c = isgraph(data[i+j]) ? data[i+j]:'.';
                    cout << c;
                }
            }
            cout << endl;
            
            for (i=0;i<256;i+=16){
                cout << endl << hex << setfill('0') << setw(2) <<right <<i << " ";
                for (j=0;j<16;j++) {
                    cout << setfill ('0') << setw(4) << right << (unsigned int)ByteIndex[i+j] << " ";
                }
            }
            cout << endl;
            for (i=0;i<256;i++){
                cout << "Byte: <" << i << "> Count: " << ByteCount[i];
                TUint16 pos = ByteIndex[i];
                int j = 0;
                while (pos != PosEnd){
                    if (j++ % 16 == 0)
                        cout << endl << "    ";
                    cout << hex << setw(4) << setfill('0') << pos << " ";
                    pos = BytePos[pos];
                }
                cout << endl;
            }
            cout << "buffer lists" << endl;
            for (i=PairListHigh; i>=0; i--){
                TUint16 index = PairLists[i];
                if (index == PosEnd)
                    continue;
                cout << dec;
                cout << "List " << i << endl;
                while (index != PosEnd){ 
                    char b0 = (char)PairBuffer[index].Pair;
                    char b1 = (char)(PairBuffer[index].Pair >> 8);
                    cout << "    " << setw(4) << setfill('0') << hex << PairBuffer[index].Pair << " " << "<" << (isgraph(b1)? b1:'.') << (isgraph(b0) ? b0 : '.') << ">";  
                    TUint16 pos;
                    pos = PairBuffer[index].Pos;
                    int k = 0;
                    while (pos != PosEnd){
                        if (k%16 ==0) {
                            cout << endl << "        ";
                        };
                        cout << setw(4) << setfill('0') << PairPos[pos].Pos << " ";
                        k ++;
                        pos = PairPos[pos].Next;
                    }
                    cout << endl;
                    index = PairBuffer[index].Next;
                }
                
            }
        }
#endif
	public:
        TInt Compress(TUint8* dst, TUint8* src, TInt size);
        TInt Decompress(TUint8* dst, TInt dstSize, TUint8* src, TInt srcSize, TUint8*& srcNext);
};
TInt BytePairCompress(TUint8* dst, TUint8* src, TInt size, CBytePair *aBPE);
TInt BytePairDecompress(TUint8* dst, TUint8* src, TInt size, CBytePair *aBPE);

#endif

