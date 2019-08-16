/****************************************************************************
 *                                                                          *
 * Copyright (C) 2019 by Edward Hannigan                                    *
 *                                                                          *
 * This file is free software; you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as published *
 * by the Free Software Foundation; either version 3, or (at your option)   *
 * any later version.                                                       *
 *                                                                          *
 * This program is distributed in the hope that it will be useful,          *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the              *
 * GNU Lesser General Public License for more details.                      *
 * See http ://www.gnu.org/licenses/lgpl.html .                             *
 ****************************************************************************/

#include <stdio.h>
#include <assert.h>

#include <thread>
#include <mutex>
#include <atomic>

//#include "portable_timer.h"

#include "Windows.h"

#define PACKET_MAX_SIZE 1536
#define PRINT_EXTERNAL
#include "lockfreequeue.h"


#define CAPTURE_MAX_SIZE 0x10000

#define Q_SIZE 32768

// a 100 Mbps connection needs 100,000,000 / (8 * 1024 * 1024) = 11.92 MB for 1 second of buffering
#define RING_BUFFER_SIZE (32 * 1024 * 1024) // 32 MB


#ifdef WINDOWS
#define PRINT_OUTPUT(txt) { OutputDebugStringA(txt); printf("%s", txt); fflush(stdout); }
#else
#define PRINT_OUTPUT(txt) { printf("%s", txt); fflush(stdout); }
#endif


// Timer
#define TIMER_TO_US(x) (((x) * 1000000ULL) / freq.QuadPart)
#define US_TO_TIMER(x) (((x) * freq.QuadPart) / 1000000ULL)

#define READ_TIMER(t) QueryPerformanceCounter((LARGE_INTEGER*)&(t))
#define TIMER_SUBTRACTION(t1, t2) (((t1).QuadPart) - ((t2).QuadPart))


typedef ULARGE_INTEGER TIMER_VAR;
typedef ULONGLONG TIMER_DIF;


ULARGE_INTEGER freq;

HANDLE eventObj; // Windows event


ULARGE_INTEGER getTimerFrequency()
{
	QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
	return freq;
}


void myprint(const char *str, ...)
{
	va_list arglist;
	va_start(arglist, str);
	char txt[4096];

	vsnprintf(txt, 4000, str, arglist);
	PRINT_OUTPUT(txt);

	va_end(arglist);
}

void recvPacket(uint64_t *pSize, uint8_t *pData)
{
	static unsigned char cWrite = 0;

	// fill size
	uint32_t size = rand() % (PACKET_MAX_SIZE - 20);
	*pSize = size;

	// fill data
	for (uint32_t i = 0; i < size; i++)
		pData[i] = cWrite++;

	// Sometimes there is a large Windows delay
	// so delay a bit once in a while
	if ((rand() & 127) == 0)
		Sleep(1);
}


void captureTaskLockFree(void *param)
{
	LockFreeQueue *ringBuffer = (LockFreeQueue*)param;
	TIMER_VAR lastTimePacketWasReceived;
	uint64_t *pSize;
	uint8_t *pData;

	READ_TIMER(lastTimePacketWasReceived);

	while (1)
	{
		if (ringBuffer->full())
		{
			Sleep(1);
			continue;
		}

		// limit total number of bytes in buffer
		uint32_t bytesInBuffer = ringBuffer->getBytesInBuffer();
		if (bytesInBuffer >= (RING_BUFFER_SIZE - 2 * CAPTURE_MAX_SIZE))
		{
			Sleep(1);
			continue;
		}

		// limit how fast packets are received (25 to 125 uS maximum)
		/*TIMER_VAR now;
		unsigned int duration = 25 + (rand() % 101);
		if (rand() & 1)
		{
			do
			{
				READ_TIMER(now);
			} while (TIMER_TO_US(TIMER_SUBTRACTION(now, lastTimePacketWasReceived)) < duration);
		}*/

		// get pointers
		ringBuffer->getNext(&pSize, (uint64_t**)&pData);

		// capture packet
		recvPacket(pSize, pData);

		READ_TIMER(lastTimePacketWasReceived);

		// add packet to ring buffer
		ringBuffer->push(pSize);

		//if ((rand() & 127) == 0)
			SetEvent(eventObj);
	}
}

int main()
{
	unsigned char cRead = 0;
	TIMER_VAR now, last;
	uint64_t count = 0;
	LockFreeQueue ringBuffer;
	uint64_t *dest;

	getTimerFrequency();

	ringBuffer.init(Q_SIZE, RING_BUFFER_SIZE, CAPTURE_MAX_SIZE);

	eventObj = CreateEvent(NULL, false, false, NULL);

	std::thread captureThread(captureTaskLockFree, &ringBuffer);

	myprint("Testing lock-free capture\n");

	READ_TIMER(last);

	while (1)
	{
		if (WaitForSingleObject(eventObj, (DWORD)1) != WAIT_OBJECT_0)
			continue;

		int popped = 0;

		while (!ringBuffer.empty())
		{
			popped++;

			dest = ringBuffer.pop();

			uint32_t len = (uint32_t)dest[0];
			uint8_t *data = (uint8_t*)&dest[1];

			for (uint32_t i = 0; i < len; i++)
				assert(cRead++ == data[i]);

			if ((++count % 1000) == 0)
			{
				READ_TIMER(now);
				uint32_t uS = (uint32_t)TIMER_TO_US(TIMER_SUBTRACTION(now, last));
				if (uS == 0)
					uS = 1;
				//if ((++count % 100) == 0)
				printf("%llu %1.2f %1.3f %d\n", count, 1000.0f / (float)(uS), ((float)ringBuffer.getBytesInBuffer()) / (1024 * 1024), popped);
				READ_TIMER(last);
			}
			
		}
	}

	captureThread.join();

	CloseHandle(eventObj);

	return 0;
}
