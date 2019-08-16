/****************************************************************************
 *                                                                          *
 * Original work by Alexander Krizhanovsky (ak@natsys-lab.com)              *
 *                                                                          *
 * Modified by Edward Hannigan and releasing it under the same license      *
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

// This LockFreeQueue class was based off the following code
// https://www.linuxjournal.com/content/lock-free-multi-producer-multi-consumer-queue-ring-buffer
// https://github.com/tempesta-tech/blog/blob/master/lockfree_rb_q.cc

#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H


#include <thread>
#include <mutex>
#include <atomic>

#include <stdio.h>
#include <stdarg.h>


// support printf like output
#ifndef PRINT_EXTERNAL
#define myprint eprint
#else
extern void myprint(const char *str, ...);
#endif


class LockFreeQueue
{
public:

	LockFreeQueue()
	{
		memArray = NULL;
		memPool = NULL;
	}

	~LockFreeQueue()
	{
		free();
	}

	void free()
	{
		if (memArray != NULL)
		{
			delete[] memArray;
			memArray = NULL;
		}
		if (memPool != NULL)
		{
			delete[] memPool;
			memPool = NULL;
		}
	}

	void init(uint32_t qSize, uint32_t mSize, uint32_t captureSize)
	{
		bytesInBuffer = 0;
		head = 0;
		tail = 0;
		poolHead = 0;

		Q_SIZE = qSize;

		Q_MASK = qSize - 1;

		CAPTURE_MAX_SIZE = captureSize;

		pushed = 0;

		memArray = new std::atomic<uint64_t*>[qSize];

		// a 100 Mbps connection needs 100,000,000 bps * (1 s) / (8 * 1024 * 1024) = 11.92 MB for 1 second of buffering
		//mSize = 16 * 1024 * 1024;

		//memSize = qSize * (1536 + 8); // too big
		memSize = mSize; // smaller size

		// add a bit of extra space for 16-bit packet size (for example huge packets)
		memPool = new uint64_t[(memSize + CAPTURE_MAX_SIZE * 4) / 8];
	}

	inline bool empty()
	{
		//return (head & Q_MASK) == (tail & Q_MASK);
		return pushed == 0;
	}

	inline bool full()
	{
		/*unsigned long num = (head - tail) & Q_MASK;
		return num >= (Q_SIZE - 2);*/
		return pushed >= (Q_SIZE - 2);
	}

	inline void push(uint64_t *ptr)
	{
		uint32_t tp_head = head & Q_MASK;

		memArray[tp_head] = ptr;

		//void addNext()
		//{
		uint32_t size = (uint32_t)(*ptr);
		assert(size <= CAPTURE_MAX_SIZE);
		uint32_t longlongAdd = 1 + (size + 7) / 8;
		poolHead += longlongAdd;
		if (poolHead >= (memSize / 8))
			poolHead = 0;
		//}

		bytesInBuffer += longlongAdd * 8;

		head.fetch_add(1);
		pushed++;
	}

	inline uint64_t * pop()
	{
		uint32_t tp_tail = tail.fetch_add(1);
		tp_tail &= Q_MASK;

		uint64_t *ret = memArray[tp_tail];

		uint32_t size = (uint32_t)(*ret);
		uint32_t longlongSubtract = 1 + (size + 7) / 8;

		bytesInBuffer -= longlongSubtract * 8;

		pushed--;

		return ret;
	}

	inline void getNext(uint64_t **pSize, uint64_t **pData)
	{
		// set pointers
		*pSize = &memPool[poolHead];
		*pData = &memPool[poolHead + 1];
	}

	inline uint32_t getBytesInBuffer() { return bytesInBuffer; }

	void dump()
	{
		myprint("poolHead = %u\n", poolHead);
		myprint("memSize = %u\n", memSize);
		myprint("bytesInBuffer = %u\n", bytesInBuffer.load());
		myprint("head = %u\n", head.load());
		myprint("tail = %u\n", tail.load());
		myprint("Q_MASK = %u\n", Q_MASK);
		myprint("Q_SIZE = %u\n", Q_SIZE);
		myprint("poolHead = %u\n", poolHead);
		myprint("CAPTURE_MAX_SIZE = %u\n", CAPTURE_MAX_SIZE);

		for (uint32_t i = 0; i < Q_SIZE; i++)
		{
			myprint("memArray[%u] = %llu\n", i, memArray[i].load());
		}

		uint32_t memPoolSize = (memSize + CAPTURE_MAX_SIZE * 4) / 8;
		for (uint32_t i = 0; i < memPoolSize; i++)
		{
			uint64_t element = memPool[i];
			uint32_t smallerElements[2];
			memcpy(smallerElements, &element, 8);
			myprint("memPool[%u] = %x %x\n", i, smallerElements[0], smallerElements[1]);
		}
	}

private:

	uint32_t poolHead;

	uint32_t memSize;

	std::atomic<uint32_t> pushed;

	std::atomic<uint32_t> bytesInBuffer;

	std::atomic<uint32_t> head; // currently free position (next to insert)

	std::atomic<uint32_t> tail; // current tail, next to pop

	std::atomic<uint64_t*> *memArray;

	uint32_t Q_MASK, Q_SIZE;

	uint32_t CAPTURE_MAX_SIZE;

	uint64_t *memPool;
};


#endif // LOCK_FREE_QUEUE_H
