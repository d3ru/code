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
*
* Description: 
*
*/


#include "byte_pair.h"
#define __BREAKPOINT()

#undef ASSERT
#define ASSERT(c)	if(!(c))	\
{		\
    __BREAKPOINT()	\
}

#include <ctime>
clock_t ClockCompress = 0;

//#define DEBUG_ASSERT
#ifdef DEBUG_ASSERT
void myassert(int c) {
    if (!(c)) {
        cout <<"myassertion failed" << endl;
    }
}
#endif

void CBytePair::SortN(TUint16 *a, TInt n)
{
    //bubble sort
    TInt i,j;
    TUint16 tmp;
    for (i=0;i<n-1;i++){
        for (j=i+1;j<=n-1;j++){
            if (a[j]<a[i]) {
                tmp = a[j];
                a[j]=a[i];
                a[i]=tmp;
            }
        }
    }
}
void CBytePair::InsertN(TUint16 *a, TInt n, TUint16 v)
{
    TInt i;
    for (i=n-1;i>=0;i--){
        if (a[i] > v)
            a[i+1]=a[i];
        else{
            break;
        }
    }
    a[i+1] = v;
}


TInt CBytePair::PosN(TUint16 index, TInt n){
    myassert(n<=PairCount[PairBuffer[index].Pair].Count);
    TUint16 FirstN[1+3+0x1000/256+1];
    TInt i = 0;
    TUint16 posindex;
    posindex = PairBuffer[index].Pos;

    while (i<n) {
        FirstN[i] = PairPos[posindex].Pos;
        posindex = PairPos[posindex].Next;
        i++;
    }
    SortN(FirstN, n);
    while (posindex!=PosEnd){
        InsertN(FirstN, n, PairPos[posindex].Pos);
        posindex=PairPos[posindex].Next;
    }
    return FirstN[n-1];
}

void CBytePair::Initialize(TUint8* data, TInt size)
{
#if defined(WIN32)
    TUint32 *p = reinterpret_cast<TUint32*>(PairCount);
    TUint32 *end = p + 0x10000;
    while(p < end) {
        *p++ = 0xffff0000;
    }
#else
    for(int i = 0 ; i < 0x10000 ; i ++){
	PairCount[i].Count = 0 ;
	PairCount[i].Index = 0xffff;
    }	
#endif
    memset(reinterpret_cast<char*>(PairBuffer),0xff, sizeof(PairBuffer)); 
    PairBufferNext = 0;
    memset(reinterpret_cast<char*>(PairPos),0xff, sizeof(PairPos)); 
    PairPosNext = 0;
    memset(reinterpret_cast<char*>(PairLists),0xff, sizeof(PairLists)); 
    PairListHigh = 0;
    
    CountBytes(data,size);
    marker = -1;
    markerCount = LeastCommonByte(marker);
    ByteUsed(marker);

    TUint8 *pData, *pMask;
    TUint16 pair;
    pData=data, pMask=Mask; 
    if (*pData == marker)
        *pMask = ByteMarked;
    else if (*(pData+1) == marker)
        *pMask = ByteTail;
    else { 
        *pMask = ByteHead;
        pair = (TUint16)(*pData | *(pData+1) << 8);
        InsertPair(pair, 0);
    }
    
    for (pData++, pMask++; pData < data+size-1; pData++, pMask++) {
        if (*pData == marker){
            *pMask = ByteMarked;
            continue;
        }
        if (*(pData+1) == marker){
            *pMask = ByteTail;
            continue;
        }
        if ((*pData == *(pData+1)) && (*pData == *(pData-1))&& (*(pMask-1) == ByteHead)){
            *pMask = ByteTail;
            continue;
        }
        *pMask = ByteHead;
        pair = (TUint16)(*pData | *(pData+1) << 8);
        InsertPair(pair,(TUint16)(pData-data));
    }
    if (*pData == marker)
        *pMask = ByteMarked;
    else 
        *pMask = ByteTail;
}

TInt CBytePair::MostCommonPair(TInt& pair, TInt minFrequency)
{
    TUint16 index = PairLists[PairListHigh];
    TUint16 tmpindex = index; 
    TUint16 p = PairBuffer[index].Pair;
    TInt tieBreak, bestTieBreak;
    bestTieBreak = -ByteCount[p&0xff] - ByteCount[p>>8];
    while(PairBuffer[tmpindex].Next != PosEnd) {
            tmpindex = PairBuffer[tmpindex].Next;
            p = PairBuffer[tmpindex].Pair;
            tieBreak = -ByteCount[p&0xff]-ByteCount[p>>8];
            if(tieBreak>bestTieBreak)
            {
                index = tmpindex;
                bestTieBreak = tieBreak;
            }
            else if(tieBreak==bestTieBreak){
                if (minFrequency > PairListHigh)
                    break;
                if (PosN(tmpindex, minFrequency) > PosN(index,minFrequency)){
                    index = tmpindex;
                }
            }
    }
    pair = PairBuffer[index].Pair;
    return PairListHigh;
}

TInt CBytePair::LeastCommonByte(TInt& byte)
{
    TInt bestCount = 0xffff;
    TInt bestByte = -1;
    TInt b;
    for(b=0; b<0x100; b++)
    {
        TInt f = ByteCount[b];
        if(f<bestCount)
        {
            bestCount = f;
            bestByte = b;
        }
    }
    byte = bestByte;
    return bestCount;
}


TInt CBytePair::Compress(TUint8* dst, TUint8* src, TInt size)
{
    clock_t ClockStart = clock();
    TUint8 tokens[0x100*3];
    TInt tokenCount = 0;

    TUint8 * dst2 = dst + size*2;
    memcpy (dst2, src, size);
    Initialize (dst2, size);
    TInt overhead = 1+3+markerCount;
    for(TInt r=256; r>0; --r)
    {   
        TInt byte;
        TInt byteCount = LeastCommonByte(byte);
        TInt pair;
        TInt pairCount = MostCommonPair(pair, overhead+1);
        TInt saving = pairCount-byteCount;
        if(saving<=overhead)
            break;

        overhead = 3;
        if(tokenCount>=32)
            overhead = 2;

        TUint8* d=tokens+3*tokenCount;
        ++tokenCount;
        *d++ = (TUint8)byte;
        ByteUsed(byte);
        *d++ = (TUint8)pair;
        ByteUsed(pair&0xff);
        *d++ = (TUint8)(pair>>8);
        ByteUsed(pair>>8);

            TUint16 index = PairCount[pair].Index;
            TUint16 count = PairCount[pair].Count;
            TUint16 posindex = PairBuffer[index].Pos;
            TUint16 headpos, tailpos, tmppos, tmpposprev, bytepos;
            TUint16 tmppair;
            // Remove pairs
            while (posindex != PosEnd) {
                headpos = PairPos[posindex].Pos;
                tailpos = (TUint16)(headpos + 1);
                while (Mask[tailpos] == ByteRemoved){
                    tailpos ++;
                    myassert(tailpos < MaxBlockSize);
                }
                GetPairBackward(dst2, headpos, tmppos, tmppair);
                if ((tmppos != PosEnd) && (Mask[tmppos] == ByteHead)) {
                    RemovePair(tmppair, tmppos);
                    Mask[tmppos] = ByteTail;
                }
                if (Mask[tailpos] == ByteHead) {
                    GetPairForward(dst2, tailpos, size, tmppos, tmppair);
                    myassert(tmppos!=PosEnd);
                    RemovePair(tmppair, tmppos);
                    Mask[tmppos] = ByteTail;
                }
                posindex = PairPos[posindex].Next;
            }
            if (byteCount) {
                bytepos = ByteIndex[byte];
                while(bytepos != PosEnd){
                    if (Mask[bytepos] == ByteRemoved) {
                        bytepos = BytePos[bytepos];
                        continue;
                    }
                    GetPairBackward(dst2, bytepos, tmppos, tmppair);
                    if ((tmppos != PosEnd) && (Mask[tmppos] == ByteHead)) {
                        RemovePair(tmppair, tmppos);
                        Mask[tmppos] = ByteTail;
                    }
                    if (Mask[bytepos] == ByteHead) {
                        GetPairForward(dst2, bytepos, size, tmppos, tmppair);
                        myassert(tmppos!=PosEnd);
                        RemovePair(tmppair, tmppos);
                        Mask[tmppos] = ByteTail;
                    }
                    bytepos = BytePos[bytepos];
                }
            }
            
            // Update buffer
            posindex = PairBuffer[index].Pos;
            while (posindex != PosEnd){
                headpos = PairPos[posindex].Pos;
                tailpos = (TUint16)(headpos + 1);
                while (Mask[tailpos] == ByteRemoved){
                    tailpos ++;
                    myassert(tailpos < MaxBlockSize);
                }
                dst2[headpos] = (TUint8)byte;
                dst2[tailpos] = 0xff;
                Mask[headpos] = ByteNew;
                Mask[tailpos] = ByteRemoved;
                posindex = PairPos[posindex].Next;
            }
            if (byteCount) {
                bytepos = ByteIndex[byte];
                while(bytepos != PosEnd) {
                    Mask[bytepos] = ByteMarked;
                    bytepos = BytePos[bytepos];
                }
            }
            
            // Insert new pairs
            posindex = PairBuffer[index].Pos;
            TUint16 firstpos, lastpos;
            while (posindex != PosEnd){
                firstpos = PairPos[posindex].Pos;
                lastpos = firstpos;
                if (Mask[firstpos] == ByteNew) {
                    while ((firstpos > 0) && ((Mask[firstpos] == ByteNew) || (Mask[firstpos] == ByteRemoved)))
                        firstpos --;
                    while (Mask[firstpos] != ByteNew)
                        firstpos ++;
                    while ((lastpos < MaxBlockSize-1) && ((Mask[lastpos] == ByteNew)||(Mask[lastpos] == ByteRemoved)))
                        lastpos ++;
                    while (Mask[lastpos] != ByteNew)
                        lastpos --;

                    GetPairForward(dst2, lastpos, size, tmppos, tmppair);
                    if (tmppos != PosEnd) {
                        Mask[lastpos] = ByteHead;
                        InsertPair(tmppair, tmppos);
                        // Potential new pair after the new one
                        tmppos = (TUint16)(lastpos+1);
                        while(Mask[tmppos]==ByteRemoved) 
                            tmppos ++;
                        if (Mask[tmppos]==ByteTail){
                            tmpposprev = tmppos;
                            tmppos ++;
                            while (tmppos<size){
                                if (Mask[tmppos]==ByteRemoved){
                                    tmppos++;
                                    continue;
                                }
                                if (Mask[tmppos]==ByteMarked)
                                    break;
                                if (dst2[tmppos]!=dst2[tmpposprev])
                                    break;
                                if (Mask[tmpposprev]==ByteTail){
                                    //myassert(Mask[tmppos]==ByteHead);
                                    InsertPair((TUint16)(dst2[tmppos]|dst2[tmppos]<<8), tmpposprev);
                                    Mask[tmpposprev]=ByteHead;
                                }else{
                                    myassert(Mask[tmpposprev]==ByteHead);
                                    RemovePair((TUint16)(dst2[tmppos]|dst2[tmppos]<<8), tmpposprev);
                                    Mask[tmpposprev]=ByteTail;
                                }
                                tmpposprev = tmppos;
                                tmppos ++;
                            }
                        }
                    }else {
                        Mask[lastpos] = ByteTail;
                    }
                    GetPairBackward(dst2, firstpos, tmppos, tmppair);
                    if (tmppos != PosEnd) {
                        Mask[tmppos] = ByteHead;
                        InsertPair(tmppair, tmppos);
                    }
                    
                    while (firstpos < lastpos) {
                        tmppair = (TUint16)(dst2[firstpos] | dst2[firstpos]<<8);
                        InsertPair(tmppair, firstpos);
                        Mask[firstpos] = ByteHead;
                        tmppos = (TUint16)(firstpos + 1);
                        while (Mask[tmppos] == ByteRemoved)
                            tmppos ++;
                        myassert(tmppos <= lastpos);
                        if (tmppos == lastpos)
                            break;
                        Mask[tmppos] = ByteTail;
                        firstpos = (TUint16)(tmppos + 1);
                        while ((firstpos < lastpos) && (Mask[firstpos] == ByteRemoved))
                            firstpos ++;
                    }
                }
                posindex = PairPos[posindex].Next;
            }

            // Remove the pair from PairLists
            if (PairBuffer[index].Prev == PosHead){
                if (PairBuffer[index].Next == PosEnd) {
                    PairLists[count] = PosEnd;
                } else {
                    PairLists[count] = PairBuffer[index].Next;
                    PairBuffer[PairBuffer[index].Next].Prev = PosHead;
                }
            } else {
                if (PairBuffer[index].Next == PosEnd){
                    PairBuffer[PairBuffer[index].Prev].Next = PosEnd;
                } else {
                    PairBuffer[PairBuffer[index].Prev].Next = PairBuffer[index].Next;
                    PairBuffer[PairBuffer[index].Next].Prev = PairBuffer[index].Prev;
                }
            }
            while (PairLists[PairListHigh] == PosEnd)
                PairListHigh --;
            myassert(PairListHigh >= 1);
            PairBuffer[index].Next = PosEnd;
            PairBuffer[index].Prev = PosEnd;
            PairCount[pair].Count = 0;
            PairCount[pair].Index = PosEnd;

    }

    // sort tokens with a bubble sort...
    for(TInt x=0; x<tokenCount-1; x++)
        for(TInt y=x+1; y<tokenCount; y++)
            if(tokens[x*3]>tokens[y*3])
            {
                TInt z = tokens[x*3];
                tokens[x*3] = tokens[y*3];
                tokens[y*3] = (TUint8)z;
                z = tokens[x*3+1];
                tokens[x*3+1] = tokens[y*3+1];
                tokens[y*3+1] = (TUint8)z;
                z = tokens[x*3+2];
                tokens[x*3+2] = tokens[y*3+2];
                tokens[y*3+2] = (TUint8)z;
            }
        
    
    TUint8* originalDst = dst;
    
    *dst++ = (TUint8)tokenCount;
    TInt tmpTokenCount = tokenCount;
    if(tokenCount)
    {
        *dst++ = (TUint8)marker;
        if(tokenCount<32)
        {
            memcpy(dst,tokens,tokenCount*3);
            dst += tokenCount*3;
        }
        else
        {
            TUint8* bitMask = dst;
            memset(bitMask,0,32);
            dst += 32;
            TUint8* d=tokens;
            do
            {
                TInt t=*d++;
                bitMask[t>>3] |= (1<<(t&7));
                *dst++ = *d++;
                *dst++ = *d++;
            }
            while(--tokenCount);
        }
    }
 
    if (tmpTokenCount == 0) {
        memcpy(dst,dst2,size);
        dst += size;
    } else {
        TUint16 pos = 0;
        for (TUint8 *p=dst2; p < dst2+size; p++, pos++) {
            if (Mask[pos] == ByteRemoved)
                continue;
            if (Mask[pos]== ByteMarked){
                *dst++ = (TUint8)marker;
            }
            *dst++ = *p;
        }
    }
    
    ClockCompress += clock() - ClockStart;
    return (dst-originalDst);
}


TInt CBytePair::Decompress(TUint8* dst, TInt dstSize, TUint8* src, TInt srcSize, TUint8*& srcNext)
{

    TUint8* dstStart = dst;
    TUint8* dstEnd = dst+dstSize;
    TUint8* srcEnd = src+srcSize;

    TUint32 LUT[0x100/2];
    TUint8* LUT0 = (TUint8*)LUT;
    TUint8* LUT1 = LUT0+0x100;

    TUint8 stack[0x100];
    TUint8* stackStart = stack+sizeof(stack);
    TUint8* sp = stackStart;

    TUint32 marker = ~0u;
    TInt numTokens;
    TUint32 p1;
    TUint32 p2;

    TUint32* l = (TUint32*)LUT;
    TUint32 b = 0x03020100;
    TUint32 step = 0x04040404;
    do
    {
        *l++ = b;
        b += step;
    }
    while(b>step);

    if(src>=srcEnd)
        goto error;
    numTokens = *src++;
    if(numTokens)
    {
        if(src>=srcEnd)
            goto error;
        marker = *src++;
        LUT0[marker] = (TUint8)~marker;

        if(numTokens<32)
        {
            TUint8* tokenEnd = src+3*numTokens;
            if(tokenEnd>srcEnd)
                goto error;
            do
            {
                TInt b = *src++;
                TInt p1 = *src++;
                TInt p2 = *src++;
                LUT0[b] = (TUint8)p1;
                LUT1[b] = (TUint8)p2;
            }
            while(src<tokenEnd);
        }
        else
        {
            TUint8* bitMask = src;
            src += 32;
            if(src>srcEnd)
                goto error;
            TInt b=0;
            do
            {
                TUint8 mask = bitMask[b>>3];
                if(mask&(1<<(b&7)))
                {
                    if(src>srcEnd)
                        goto error;
                    TInt p1 = *src++;
                    if(src>srcEnd)
                        goto error;
                    TInt p2 = *src++;
                    LUT0[b] = (TUint8)p1;
                    LUT1[b] = (TUint8)p2;
                    --numTokens;
                }
                ++b;
            }
            while(b<0x100);
            if(numTokens)
                goto error;
        }
    }

    if(src>=srcEnd)
        goto error;
    b = *src++;
    if(dst>=dstEnd)
        goto error;
    p1 = LUT0[b];
    if(p1!=b)
        goto not_single;
next:
    if(src>=srcEnd)
        goto done_s;
    b = *src++;
    *dst++ = (TUint8)p1;
    if(dst>=dstEnd)
        goto done_d;
    p1 = LUT0[b];
    if(p1==b)
        goto next;

not_single:
    if(b==marker)
        goto do_marker;

do_pair:
    p2 = LUT1[b];
    b = p1;
    p1 = LUT0[b];
    if(sp<=stack)
        goto error;
    *--sp = (TUint8)p2;

recurse:
    if(b!=p1)
        goto do_pair;

    if(sp==stackStart)
        goto next;
    b = *sp++;
    if(dst>=dstEnd)
        goto error;
    *dst++ = (TUint8)p1;
    p1 = LUT0[b];
    goto recurse;

do_marker:
    if(src>=srcEnd)
        goto error;
    p1 = *src++;
    goto next;

error:
    srcNext = 0;
    return KErrCorrupt;

done_s:
    *dst++ = (TUint8)p1;
    srcNext = src;
    return dst-dstStart;

done_d:
    if(dst>=dstEnd)
        --src;
    srcNext = src;
    return dst-dstStart;
}


TInt BytePairCompress(TUint8* dst, TUint8* src, TInt size, CBytePair *aBPE)
{
    TUint8 PakBuffer[MaxBlockSize*4];
    TUint8 UnpakBuffer[MaxBlockSize];
    ASSERT(size<=MaxBlockSize);
    TInt compressedSize = aBPE->Compress(PakBuffer,src,size);
    TUint8* pakEnd;
    TInt us = aBPE->Decompress(UnpakBuffer,MaxBlockSize,PakBuffer,compressedSize,pakEnd);
    ASSERT(us==size)
    ASSERT(pakEnd==PakBuffer+compressedSize)
    ASSERT(!memcmp(src,UnpakBuffer,size))
    if(compressedSize>=size)
        return KErrTooBig;
    memcpy(dst,PakBuffer,compressedSize);
    return compressedSize;
}
TInt BytePairDecompress(TUint8* dst, TUint8* src, TInt size, CBytePair *aBPE)
{
    TUint8 UnpakBuffer[MaxBlockSize];
    ASSERT(size<=MaxBlockSize);
    TUint8* pakEnd;
    TInt us = aBPE->Decompress(UnpakBuffer,MaxBlockSize,src,size,pakEnd);
    memcpy(dst,UnpakBuffer,us);
    return us;
}
