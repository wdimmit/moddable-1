/*
 * Copyright (c) 2016-2017  Moddable Tech, Inc.
 *
 *   This file is part of the Moddable SDK Runtime.
 * 
 *   The Moddable SDK Runtime is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 * 
 *   The Moddable SDK Runtime is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 * 
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with the Moddable SDK Runtime.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and  
 * permission notice:  
 *
 *       Copyright (C) 2010-2016 Marvell International Ltd.
 *       Copyright (C) 2002-2010 Kinoma, Inc.
 *
 *       Licensed under the Apache License, Version 2.0 (the "License");
 *       you may not use this file except in compliance with the License.
 *       You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *       Unless required by applicable law or agreed to in writing, software
 *       distributed under the License is distributed on an "AS IS" BASIS,
 *       WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *       See the License for the specific language governing permissions and
 *       limitations under the License.
 */

#include "xsAll.h"
#include "xs.h"

#if ESP32
	#define rtc_timeval timeval
	#define rtctime_gettimeofday(a) gettimeofday(a, NULL)
#else
	#include "rtctime.h"
	#include "spi_flash.h"
#endif

#ifdef mxInstrument
	#include "modInstrumentation.h"
	static void espStartInstrumentation(txMachine* the);
#endif

#define FOURCC(c1, c2, c3, c4) (((c1) << 24) | ((c2) << 16) | ((c3) << 8) | (c4))

uint8_t espRead8(const void *addr)
{
	const uint32_t *p = (const uint32_t *)(~3 & (uint32_t)addr);
	return *p >> ((3 & (uint32_t)addr) << 3);
}

uint16_t espRead16(const void *addr)
{
	const uint32_t *p = (const uint32_t *)(~3 & (uint32_t)addr);
	switch (3 & (uint32_t)addr) {
		case 3:	return (uint16_t)((*p >> 24) | (p[1] << 8));
		case 2:	return (uint16_t) (*p >> 16);
		case 1:	return (uint16_t) (*p >>  8);
		case 0:	return (uint16_t) (*p);
	}
}

uint32_t espRead32(const void *addr)
{
	const uint32_t *p = (const uint32_t *)(~3 & (uint32_t)addr);
	switch (3 & (uint32_t)addr) {
		case 0:	return *p;
		case 1:	return (p[0] >>  8) | (p[1] << 24);
		case 2:	return (p[0] >> 16) | (p[1] << 16);
		case 3:	return (p[0] >> 24) | (p[1] <<  8);
	}
}

uint16_t espRead16be(const void *addr)
{
	uint16_t result;
	const uint32_t *p = (const uint32_t *)(~3 & (uint32_t)addr);
	switch (3 & (uint32_t)addr) {
		case 3:	result = (uint16_t)((*p >> 24) | (p[1] << 8)); break;
		case 2:	result = (uint16_t) (*p >> 16); break;
		case 1:	result = (uint16_t) (*p >>  8); break;
		case 0:	result = (uint16_t) (*p); break;
	}

	return (result >> 8) | (result << 8);
}

uint32_t espRead32be(const void *addr)
{
	uint32_t result;
	const uint32_t *p = (const uint32_t *)(~3 & (uint32_t)addr);
	switch (3 & (uint32_t)addr) {
		case 0:	result = *p; break;
		case 1:	result = (p[0] >>  8) | (p[1] << 24); break;
		case 2:	result = (p[0] >> 16) | (p[1] << 16); break;
		case 3:	result = (p[0] >> 24) | (p[1] <<  8); break;
	}
	return (result << 24) | ((result & 0xff00) << 8)  | ((result >> 8) & 0xff00) | (result >> 24);
}

// all hail gary davidian: http://opensource.apple.com//source/xnu/xnu-1456.1.26/libkern/ppc/strlen.s
size_t espStrLen(const void *addr)
{
	static const uint32_t mask[] ICACHE_XS6RO2_ATTR = {0, 0x000000FF, 0x0000FFFF, 0x00FFFFFF};
	int len = 3 & (uint32_t)addr;
	const uint32_t *src = (const uint32_t *)(-len + (uint32_t)addr);
	uint32_t data = *src++ | mask[len];

	len = -len;

	while (true) {
		uint32_t y = data + 0xFEFEFEFF;
		uint32_t z = ~data & 0x80808080;

		if (0 != (y & z))
			break;

		len += 4;
		data = *src++;
	}

	// three more bytes, at most, since there is a 0 somewhere in this long
	if (data & 0x00ff) {
		len += 1;
		if (data & 0x00ff00) {
			len += 1;
			if (data & 0x00ff0000)
				len += 1;
		}
	}

	return (size_t)len;
}

//@@ this could be much faster, especially when both strings are aligned
int espStrCmp(const char *ap, const char *bp)
{
	while (true) {
		uint8_t a = espRead8(ap);
		uint8_t b = espRead8(bp);

		if ((a != b) || !a)
			return a - b;

		ap += 1;
		bp += 1;
	}
}

int espStrNCmp(const char *ap, const char *bp, size_t count)
{
	while (count--) {
		uint8_t a = espRead8(ap);
		uint8_t b = espRead8(bp);

		if ((a != b) || !a)
			return a - b;

		ap += 1;
		bp += 1;
	}

	return 0;
}

void espStrCpy(char *dst, const char *src)
{
	uint8_t c;

	do {
		c = espRead8(src++);
		*dst++ = c;
	} while (c);
}

void espStrNCpy(char *dst, const char *src, size_t count)
{
	char c;

	if (0 == count) return;

	do {
		c = espRead8(src++);
		*dst++ = c;
	} while (--count && c);

	while (count--)
		*dst++ = 0;
}

void espStrCat(char *dst, const char *src)
{
	while (0 != espRead8(dst))
		dst++;

	espStrCpy(dst, src);
}

void espStrNCat(char *dst, const char *src, size_t count)
{
	while (0 != espRead8(dst))
		dst++;

	while (count--) {
		char c = espRead8(src++);
		if (0 == c)
			break;

		*dst++ = c;
	}

	*dst++ = 0;
}

char *espStrChr(const char *str, int c)
{
	do {
		char value = espRead8(str);
		if (!value)
			return NULL;

		if (value == (char)c)
			return (char *)str;

		str++;
	} while (true);
}

char *espStrRChr(const char *str, int c)
{
	const char *result = NULL;

	do {
		str = espStrChr(str, c);
		if (!str)
			break;

		result = str;
		str += 1;
	} while (true);

	return (char *)result;
}

char *espStrStr(const char *src, const char *search)
{
	char searchFirst = espRead8(search++);
	char c;

	if (0 == searchFirst)
		return (char *)src;

	while ((c = espRead8(src++))) {
		const char *ap, *bp;
		uint8_t a, b;

		if (c != searchFirst)
			continue;

		ap = src, bp = search;
		while (true) {
			b = espRead8(bp++);
			if (!b)
				return (char *)src - 1;

			a = espRead8(ap++);
			if ((a != b) || !a)
				break;
		}
	}

	return NULL;
}

void espMemCpy(void *dst, const void *src, size_t count)
{
	const uint8_t *s = src;
	uint8_t *d = dst;

	if (count > 8) {
		// align source
		uint8_t align = 3 & (uintptr_t)s;
		uint32_t data;

		if (align) {
			data = *(uint32_t *)(~3 & (uintptr_t)s);
			if (3 == align) {
				d[0] = (uint8_t)(data >> 24);
				count -= 1;
				s += 1, d += 1;
			}
			else if (2 == align) {
				d[0] = (uint8_t)(data >> 16);
				d[1] = (uint8_t)(data >> 24);
				count -= 2;
				s += 2, d += 2;
			}
			else if (1 == align) {
				d[0] = (uint8_t)(data >>  8);
				d[1] = (uint8_t)(data >> 16);
				d[2] = (uint8_t)(data >> 24);
				count -= 3;
				s += 3, d += 3;
			}
		}

		// read longs and write longs
		align = 3 & (uintptr_t)d;
		if (0 == align) {
			while (count >= 16) {
				*(uint32_t *)&d[0] = *(uint32_t *)&s[0];
				*(uint32_t *)&d[4] = *(uint32_t *)&s[4];
				*(uint32_t *)&d[8] = *(uint32_t *)&s[8];
				*(uint32_t *)&d[12] = *(uint32_t *)&s[12];
				count -= 16;
				s += 16, d += 16;
			}

			while (count >= 4) {
				*(uint32_t *)d = *(uint32_t *)s;
				count -= 4;
				s += 4, d += 4;
			}
		}
		else if (3 == align) {
			data = *(uint32_t *)s;
			*d++ = (uint8_t)(data);
			s += 1;
			count -= 1;
			while (count >= 4) {
				uint32_t next = *(uint32_t *)(3 + (uintptr_t)s);
				*(uint32_t *)d = (data >> 8) | (next << 24);
				count -= 4;
				s += 4, d += 4;
				data = next;
			}
		}
		else if (2 == align) {
			data = *(uint32_t *)s;
			*d++ = (uint8_t)(data);
			*d++ = (uint8_t)(data >>  8);
			s += 2;
			count -= 2;
			while (count >= 4) {
				uint32_t next = *(uint32_t *)(2 + (uintptr_t)s);
				*(uint32_t *)d = (data >> 16) | (next << 16);
				count -= 4;
				s += 4, d += 4;
				data = next;
			}
		}
		else if (1 == align) {
			data = *(uint32_t *)s;
			*d++ = (uint8_t)(data);
			*d++ = (uint8_t)(data >>  8);
			*d++ = (uint8_t)(data >> 16);
			s += 3;
			count -= 3;
			while (count >= 4) {
				uint32_t next = *(uint32_t *)(1 + (uintptr_t)s);
				*(uint32_t *)d = (data >> 24) | (next << 8);
				count -= 4;
				s += 4, d += 4;
				data = next;
			}
		}
	}

	// tail
	while (count--)
		*d++ = espRead8(s++);
}

int espMemCmp(const void *a, const void *b, size_t count)
{
	const uint8_t *a8 = a;
	const uint8_t *b8 = b;

	while (count--) {
		uint8_t av = espRead8(a8++);
		uint8_t bv = espRead8(b8++);
		if (av == bv)
			continue;
		return av - bv;
	}

	return 0;
}

extern const uint32_t *gUnusedInstructionRAM;
static uint32_t *gUint32Memory;

void *espMallocUint32(int byteCount)
{
#if !ESP32
	char *end = gUnusedInstructionRAM[-1] + (char *)gUnusedInstructionRAM[-2];
	uint32_t *result;

	if (byteCount & 3)
		return NULL;		// must be multiple of uint32_t in size

	if (NULL == gUint32Memory)
		gUint32Memory = (uint32_t *)gUnusedInstructionRAM[-2];;

	result = gUint32Memory;
	if ((byteCount + (char *)result) > end)
		return c_malloc(byteCount);

	gUint32Memory += byteCount;

	return result;
#else
	return c_malloc(byteCount);
#endif

}

void espFreeUint32(void *t)
{
#if !ESP32
	void *end = gUnusedInstructionRAM[-1] + (char *)gUnusedInstructionRAM[-2];

	if (!t) return;

	if ((t < (void *)gUnusedInstructionRAM[-2]) || (t >= end)) {
		c_free(t);
		return;
	}

	gUint32Memory = t;		//@@ assumes alloc/free are paired
#else
	c_free(t);
#endif
}

#if !ESP32
#include "cont.h"

int espStackSpace(void)
{
	extern cont_t g_cont __attribute__ ((aligned (16)));
	int free_ = 0;

	return (char *)&free_ - (char *)&g_cont.stack[0];
}
#endif

static int32_t gTimeZoneOffset = -8 * 60 * 60;		// Menlo Park
static int16_t gDaylightSavings = 60 * 60;			// summer time

static modTm gTM;		//@@ eliminate with _r calls

static const uint8_t gDaysInMonth[] ICACHE_XS6RO2_ATTR = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#define isLeapYear(YEAR) (!(YEAR % 4) && ((YEAR % 100) || !(YEAR % 400)))

// algorithm based on arduino version. separate implementation. http://www.pucebaboon.com/ESP8266/

struct modTm *modGmTime(const modTime_t *timep)
{
	uint32_t t = *timep;
	int days = 0;

	gTM.tm_sec = t % 60;
	t /= 60;

	gTM.tm_min = t % 60;
	t /= 60;

	gTM.tm_hour = t % 24;
	t /= 24;

	gTM.tm_wday = (t + 4) % 7;

	gTM.tm_year = 1970;
	while (true) {
		int daysInYear = 365;

		if (isLeapYear(gTM.tm_year))
			daysInYear += 1;

		if ((days + daysInYear) >= t)
			break;

		gTM.tm_year += 1;
		days += daysInYear;
	}

	t -= days;

	gTM.tm_yday = t;

	for (gTM.tm_mon = 0; gTM.tm_mon < 12; gTM.tm_mon++) {
		uint8_t daysInMonth = espRead8(gDaysInMonth + gTM.tm_mon);

		if ((1 == gTM.tm_mon) && isLeapYear(gTM.tm_year))
			daysInMonth = 29;

		if (t < daysInMonth)
			break;

		t -= daysInMonth;
	}
	gTM.tm_mday = t + 1;
	gTM.tm_year -= 1900;

	return &gTM;
}

struct modTm *modLocalTime(const modTime_t *timep)
{
	modTime_t t = *timep + gTimeZoneOffset + gDaylightSavings;
	return modGmTime(&t);
}

modTime_t modMkTime(struct modTm *tm)
{
#if ESP32
	struct tm theTm;
	theTm.tm_year = tm->tm_year + 1900;
	theTm.tm_mon = tm->tm_mon + 1;
	theTm.tm_mday = tm->tm_mday;
	theTm.tm_hour = tm->tm_hour;
	theTm.tm_min = tm->tm_min;
	theTm.tm_sec = tm->tm_sec;

	return (modTime_t)(mktime(&theTm)) - (gTimeZoneOffset + gDaylightSavings);
#else
	return (modTime_t)(system_mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec)) -
			(gTimeZoneOffset + gDaylightSavings);
#endif
}

static const char* gDayName[] ICACHE_XS6RO_ATTR = {
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
	"Sun"
};

static const char* gMonths[] ICACHE_XS6RO_ATTR = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec"
};

void modStrfTime(char *s, size_t max, const char *format, const struct modTm *tm)
{
	while (true) {
		uint8_t extend = 2;
		char buffer[16];
		size_t bufferLength;
		char c = espRead8(format++);
		if (!c) break;

		if ('%' != c) {
			if (!max) break;

			*s++ = c;
			max -= 1;
			continue;
		}

		c = espRead8(format++);
		if (!c) break;

		buffer[0] = 0;
		extend = 0;
		switch (c) {		// N.B. only codes used by XS6 implemented
			case 'a':
				espStrCpy(buffer, gDayName[tm->tm_wday]);
				break;
			case 'b':
				espStrCpy(buffer, gMonths[tm->tm_mon - 1]);
				break;
			case 'd':
				itoa(tm->tm_mday, buffer, 10);
				extend = 2;
				break;
			case 'm':
				itoa(tm->tm_mon, buffer, 10);
				break;
			case 'z': {
				int32_t offset = gTimeZoneOffset + gDaylightSavings;
				uint8_t h, m;
				buffer[0] = (offset >= 0) ? '+' : '-';
				offset = (offset >= 0) ? offset : -offset;

				offset /= 60;			// to minutes
				m = offset % 60;		// minutes
				h = (offset - m) / 60;	// hours

				buffer[1] = (h > 9) ? '1' : '0';
				itoa(h % 10, buffer + 2, 10);

				buffer[3] = ':';

				buffer[4] = (m > 9) ? '1' : '0';
				itoa(m % 10, buffer + 5, 10);

				buffer[6] = 0;
				}
				break;
			case 'H':
				itoa(tm->tm_hour, buffer, 10);
				extend = 2;
				break;
			case 'M':
				itoa(tm->tm_min, buffer, 10);
				extend = 2;
				break;
			case 'S':
				itoa(tm->tm_sec, buffer, 10);
				extend = 2;
				break;
			case 'Y':
				itoa(tm->tm_year + 1900, buffer, 10);
				break;
			case 'Z':
//@@			not outputting time zone name... to do... but apparently it is allowed to output nothing if timezone name isn't known
 				break;
		}

		if ((2 == extend) && (0 == buffer[1])) {
			buffer[2] = buffer[1];
			buffer[1] = buffer[0];
			buffer[0] = '0';
		}

		bufferLength = strlen(buffer);
		if (bufferLength >= max)
			break;

		memcpy(s, buffer, bufferLength);
		s += bufferLength;
		max -= bufferLength;
	}

	*s++ = 0;
}


static uint8_t gRTCInit = false;

void modGetTimeOfDay(struct modTimeVal *tv, struct modTimeZone *tz)
{
#if !ESP32
	if (!gRTCInit) {
		rtctime_early_startup();
		rtctime_late_startup();
		gRTCInit = true;
		modSetTime(819195899);
	}
#endif

	if (tv) {
		struct rtc_timeval rtc_tv;

		rtctime_gettimeofday(&rtc_tv);
		tv->tv_sec = rtc_tv.tv_sec;
		tv->tv_usec = rtc_tv.tv_usec;
	}

	if (tz) {
		tz->tz_minuteswest = gTimeZoneOffset;
		tz->tz_dsttime = gDaylightSavings;
	}
}

void modSetTime(uint32_t seconds)
{
#if !ESP32
	struct rtc_timeval rtc_tv;

	if (!gRTCInit) {
		rtctime_early_startup();
		rtctime_late_startup();
		gRTCInit = true;
	}

	rtc_tv.tv_sec = seconds;
	rtc_tv.tv_usec = 0;

	rtctime_settimeofday(&rtc_tv);
#else
	struct timeval tv;
	struct timezone tz;

	tv.tv_sec = seconds;
	tv.tv_usec = 0;

	settimeofday(&tv, NULL);		//@@ implementation doesn't use timezone yet....
#endif
}

void modSetTimeZone(int32_t timeZoneOffset)
{
	gTimeZoneOffset = timeZoneOffset;
}

int32_t modGetTimeZone(void)
{
	return gTimeZoneOffset;
}

void modSetDaylightSavingsOffset(int32_t daylightSavings)
{
	gDaylightSavings = daylightSavings;
}

int32_t modGetDaylightSavingsOffset(void)
{
	return gDaylightSavings;
}

static void installModules(xsMachine *the);
static char *findAtom(uint32_t atomTypeIn, const uint8_t *xsb, int xsbSize, int *atomSizeOut);

void *ESP_cloneMachine(uint32_t allocation, uint32_t stackCount, uint32_t slotCount, uint8_t disableDebug)
{
	extern txPreparation* xsPreparation();
	void *result;
	txMachine root;
	txPreparation *prep = xsPreparation();
	txCreation creation;
	uint8_t *context[3];

	if ((prep->version[0] != XS_MAJOR_VERSION) || (prep->version[1] != XS_MINOR_VERSION) || (prep->version[2] != XS_PATCH_VERSION))
		modLog("version mismatch");

	creation = prep->creation;

	root.archive = prep;
	root.keyArray = prep->keys;
	root.keyCount = prep->keyCount + prep->creation.keyCount;
	root.keyIndex = prep->keyCount;
	root.nameModulo = prep->nameModulo;
	root.nameTable = prep->names;
	root.symbolModulo = prep->symbolModulo;
	root.symbolTable = prep->symbols;

	root.stack = &prep->stack[0];
	root.stackBottom = &prep->stack[0];
	root.stackTop = &prep->stack[prep->stackCount];

	root.firstHeap = &prep->heap[0];
	root.freeHeap = &prep->heap[prep->heapCount - 1];
	root.aliasCount = prep->aliasCount;

	if (0 == allocation)
		allocation = creation.staticSize;

	if (allocation) {
		if (stackCount)
			creation.stackCount = stackCount;

		if (slotCount)
			creation.initialHeapCount = slotCount;

		context[0] = c_malloc(allocation);
		if (NULL == context[0]) {
			modLog("failed to allocate xs block");
			return NULL;
		}
		context[1] = context[0] + allocation;
		context[2] = (void *)(uintptr_t)disableDebug;

		result = fxCloneMachine(&creation, &root, "modESP", context);
		if (NULL == result) {
			if (context[0])
				c_free(context[0]);
			return NULL;
		}

		((txMachine *)result)->context = NULL;
	}
	else {
		result = fxCloneMachine(&prep->creation, &root, "modESP", NULL);
		if (NULL == result)
			return NULL;
	}

	installModules(result);

#ifdef mxInstrument
	espStartInstrumentation(result);
#endif

	return result;
}

uint8_t xsRunPromiseJobs(txMachine *the)
{
	if (!mxPendingJobs.value.reference->next)
		return 0;

	fxRunPromiseJobs(the);

	return mxPendingJobs.value.reference->next ? 1 : 0;
}

void *mc_xs_chunk_allocator(txMachine* the, size_t size)
{
	if (the->heap_ptr + size <= the->heap_pend) {
		void *ptr = the->heap_ptr;
		the->heap_ptr += size;
		return ptr;
	}

	modLog("!!! xs: failed to allocate chunk !!!\n");
	return NULL;
}

void mc_xs_chunk_disposer(txMachine* the, void *data)
{
	/* @@ too lazy but it should work... */
	if ((uint8_t *)data < the->heap_ptr)
		the->heap_ptr = data;

	if (the->heap_ptr == the->heap) {
		if (the->context) {
			uint8_t **context = the->context;
			context[0] = NULL;
		}
		c_free(the->heap);		// VM is terminated
	}
}

void *mc_xs_slot_allocator(txMachine* the, size_t size)
{
	if (the->heap_pend - size >= the->heap_ptr) {
		void *ptr = the->heap_pend - size;
		the->heap_pend -= size;
		return ptr;
	}

	modLog("!!! xs: failed to allocate slots !!!\n");
	return NULL;
}

void mc_xs_slot_disposer(txMachine *the, void *data)
{
	/* nothing to do */
}

void* fxAllocateChunks(txMachine* the, txSize theSize)
{
	if ((NULL == the->stack) && (NULL == the->heap)) {
		// initialization
		uint8_t **context = the->context;
		if (context) {
			the->heap = the->heap_ptr = context[0];
			the->heap_pend = context[1];
		}
	}

	if (NULL == the->heap)
		return c_malloc(theSize);

	return mc_xs_chunk_allocator(the, theSize);
}

txSlot* fxAllocateSlots(txMachine* the, txSize theCount)
{
	txSlot* result;

	if (NULL == the->heap)
		return (txSlot*)c_malloc(theCount * sizeof(txSlot));

	result = (txSlot *)mc_xs_slot_allocator(the, theCount * sizeof(txSlot));
	if (!result) {
		fxReport(the, "# Slot allocation: failed. trying to make room...\n");
		fxCollect(the, 1);	/* expecting memory from the chunk pool */
		if (the->firstBlock != C_NULL && the->firstBlock->limit == mc_xs_chunk_allocator(the, 0)) {	/* sanity check just in case */
			fxReport(the, "# Slot allocation: %d bytes returned\n", the->firstBlock->limit - the->firstBlock->current);
			the->maximumChunksSize -= the->firstBlock->limit - the->firstBlock->current;
			the->heap_ptr = the->firstBlock->current;
			the->firstBlock->limit = the->firstBlock->current;
		}
		result = (txSlot *)mc_xs_slot_allocator(the, theCount * sizeof(txSlot));
	}

	return result;
}

void fxFreeChunks(txMachine* the, void* theChunks)
{
	if (NULL == the->heap)
		c_free(theChunks);

	mc_xs_chunk_disposer(the, theChunks);
}

void fxFreeSlots(txMachine* the, void* theSlots)
{
	if (NULL == the->heap)
		c_free(theSlots);

	mc_xs_slot_disposer(the, theSlots);
}

void fxBuildKeys(txMachine* the)
{
}

static txBoolean fxFindScript(txMachine* the, txString path, txID* id)
{
	txPreparation* preparation = the->archive;
	txInteger c = preparation->scriptCount;
	txScript* script = preparation->scripts;
	path += preparation->baseLength;
	c_strcat(path, ".xsb");
	while (c > 0) {
		if (!c_strcmp(path, script->path)) {
			path -= preparation->baseLength;
			*id = fxNewNameC(the, path);
			return 1;
		}
		c--;
		script++;
	}
	*id = XS_NO_ID;
	return 0;
}

txID fxFindModule(txMachine* the, txID moduleID, txSlot* slot)
{
	txPreparation* preparation = the->archive;
	char name[PATH_MAX];
	char path[PATH_MAX];
	txBoolean absolute = 0, relative = 0, search = 0;
	txInteger dot = 0;
	txString slash;
	txID id;
	uint8_t *xsb = (uint8_t *)kModulesStart;

	fxToStringBuffer(the, slot, name, sizeof(name));

	if (xsb) {
		char *aName = findAtom(FOURCC('N', 'A', 'M', 'E'), xsb, c_read32be(xsb), NULL);
		if (aName && (0 == c_strcmp(name, aName)))
			return fxNewNameX(the, aName);
	}

	if (!c_strncmp(name, "/", 1)) {
		absolute = 1;
	}	
	else if (!c_strncmp(name, "./", 2)) {
		dot = 1;
		relative = 1;
	}	
	else if (!c_strncmp(name, "../", 3)) {
		dot = 2;
		relative = 1;
	}
	else {
		relative = 1;
		search = 1;
	}
	if (absolute) {
		c_strcpy(path, preparation->base);
		c_strcat(path, name + 1);
		if (fxFindScript(the, path, &id))
			return id;
	}
	if (relative && (moduleID != XS_NO_ID)) {
		c_strcpy(path, fxGetKeyName(the, moduleID));
		slash = c_strrchr(path, '/');
		if (!slash)
			return XS_NO_ID;
		if (dot == 0)
			slash++;
		else if (dot == 2) {
			*slash = 0;
			slash = c_strrchr(path, '/');
			if (!slash)
				return XS_NO_ID;
		}
		if (!c_strncmp(path, preparation->base, preparation->baseLength)) {
			*slash = 0;
			c_strcat(path, name + dot);
			if (fxFindScript(the, path, &id))
				return id;
		}
	}
	if (search) {
		c_strcpy(path, preparation->base);
		c_strcat(path, name);
		if (fxFindScript(the, path, &id))
			return id;
	}
	return XS_NO_ID;
}

void fxLoadModule(txMachine* the, txID moduleID)
{
	txPreparation* preparation = the->archive;
	txString path = fxGetKeyName(the, moduleID) + preparation->baseLength;
	txInteger c = preparation->scriptCount;
	txScript* script = preparation->scripts;
	uint8_t *xsb = (uint8_t *)kModulesStart;

	if (xsb) {
		char *aName = findAtom(FOURCC('N', 'A', 'M', 'E'), xsb, c_read32be(xsb), NULL);
		if (aName && (0 == c_strcmp(path - preparation->baseLength, aName))) {
			txScript aScript;
			uint32_t xsbSize = c_read32be(xsb);

			aScript.codeBuffer = findAtom(FOURCC('C', 'O', 'D', 'E'), xsb, xsbSize, &aScript.codeSize);
			if (!aScript.codeBuffer) return;

			aScript.callback = NULL;
			aScript.symbolsBuffer = NULL;
			aScript.symbolsSize = 0;
	//		aScript.codeBuffer = atom + 8;
	//		aScript.codeSize = c_read32be(atom + 4) - 8;
			aScript.hostsBuffer = NULL;
			aScript.hostsSize = 0;
			aScript.path = path - preparation->baseLength;
			aScript.version[0] = XS_MAJOR_VERSION;
			aScript.version[1] = XS_MINOR_VERSION;
			aScript.version[2] = XS_PATCH_VERSION;
			aScript.version[3] = 0;

			fxResolveModule(the, moduleID, &aScript, C_NULL, C_NULL);
			return;
		}
	}

	while (c > 0) {
		if (!c_strcmp(path, script->path)) {
			fxResolveModule(the, moduleID, script, C_NULL, C_NULL);
			return;
		}
		c--;
		script++;
	}
}

void fxMarkHost(txMachine* the, txMarkRoot markRoot)
{
	extern void modTimersMark(txMachine *the, txMarkRoot markRoot);
	the->host = C_NULL;
	modTimersMark(the, markRoot);
}

txScript* fxParseScript(txMachine* the, void* stream, txGetter getter, txUnsigned flags)
{
	return C_NULL;
}

void fxQueuePromiseJobs(txMachine* the)
{
	// queued jobs serviced from the main loop
}

void fxSweepHost(txMachine* the)
{
}

/*
	Instrumentation
*/

#ifdef mxInstrument

static void espSampleInstrumentation(modTimer timer, void *refcon, uint32_t refconSize);

#define espInstrumentCount kModInstrumentationSystemFreeMemory - kModInstrumentationPixelsDrawn + 1
static char* espInstrumentNames[espInstrumentCount] ICACHE_XS6RO_ATTR = {
	(char *)"Pixels drawn",
	(char *)"Frames drawn",
	(char *)"Network bytes read",
	(char *)"Network bytes written",
	(char *)"Network sockets",
	(char *)"Timers",
	(char *)"Files",
	(char *)"Poco display list used",
	(char *)"Piu command List used",
	(char *)"System bytes free",
};

static char* espInstrumentUnits[espInstrumentCount] ICACHE_XS6RO_ATTR = {
	(char *)" pixels",
	(char *)" frames",
	(char *)" bytes",
	(char *)" bytes",
	(char *)" sockets",
	(char *)" timers",
	(char *)" files",
	(char *)" bytes",
	(char *)" bytes",
	(char *)" bytes",
};

extern txMachine *gThe;

static int32_t modInstrumentationSystemFreeMemory(void)
{
#if ESP32
	return (uint32_t)esp_get_free_heap_size();
#else
	return (int32_t)system_get_free_heap_size();
#endif
}

static int32_t modInstrumentationSlotHeapSize(void)
{
	return gThe->currentHeapCount * sizeof(txSlot);
}

static int32_t modInstrumentationChunkHeapSize(void)
{
	return gThe->currentChunksSize;
}

static int32_t modInstrumentationKeysUsed(void)
{
	return gThe->keyIndex - gThe->keyOffset;
}

static int32_t modInstrumentationGarbageCollectionCount(void)
{
	return gThe->garbageCollectionCount;
}

static int32_t modInstrumentationModulesLoaded(void)
{
	return gThe->loadedModulesCount;
}

static int32_t modInstrumentationStackRemain(void)
{
	if (gThe->stackPeak > gThe->stack)
		gThe->stackPeak = gThe->stack;
	return (gThe->stackTop - gThe->stackPeak) * sizeof(txSlot);
}

static modTimer gInstrumentationTimer;

void espDebugBreak(txMachine* the, uint8_t stop)
{
	if (stop) {
		fxCollectGarbage(the);
		the->garbageCollectionCount -= 1;
		espSampleInstrumentation(NULL, NULL, 0);
	}
	else
		modTimerReschedule(gInstrumentationTimer, 1000, 1000);
}

void espStartInstrumentation(txMachine *the)
{
	modInstrumentationInit();
	modInstrumentationSetCallback(SystemFreeMemory, modInstrumentationSystemFreeMemory);

	modInstrumentationSetCallback(SlotHeapSize, modInstrumentationSlotHeapSize);
	modInstrumentationSetCallback(ChunkHeapSize, modInstrumentationChunkHeapSize);
	modInstrumentationSetCallback(KeysUsed, modInstrumentationKeysUsed);
	modInstrumentationSetCallback(GarbageCollectionCount, modInstrumentationGarbageCollectionCount);
	modInstrumentationSetCallback(ModulesLoaded, modInstrumentationModulesLoaded);
	modInstrumentationSetCallback(StackRemain, modInstrumentationStackRemain);

	fxDescribeInstrumentation(the, espInstrumentCount, espInstrumentNames, espInstrumentUnits);

	gInstrumentationTimer = modTimerAdd(0, 1000, espSampleInstrumentation, NULL, 0);

	the->onBreak = espDebugBreak;
}

void espSampleInstrumentation(modTimer timer, void *refcon, uint32_t refconSize)
{
	txInteger values[espInstrumentCount];
	int what;

	for (what = kModInstrumentationPixelsDrawn; what <= kModInstrumentationSystemFreeMemory; what++)
		values[what - kModInstrumentationPixelsDrawn] = modInstrumentationGet_(what);

	values[kModInstrumentationTimers - kModInstrumentationPixelsDrawn] -= 1;	// remove timer used by instrumentation
	fxSampleInstrumentation(gThe, espInstrumentCount, values);

	modInstrumentationSet(PixelsDrawn, 0);
	modInstrumentationSet(FramesDrawn, 0);
	modInstrumentationSet(PocoDisplayListUsed, 0);
	modInstrumentationSet(PiuCommandListUsed, 0);
	modInstrumentationSet(NetworkBytesRead, 0);
	modInstrumentationSet(NetworkBytesWritten, 0);
	gThe->garbageCollectionCount = 0;
	gThe->stackPeak = gThe->stack;
}
#endif

#if ESP32

uint32_t modMilliseconds(void)
{
	return xTaskGetTickCount();
}

#endif

/*
	messages
*/

typedef struct modMessageRecord modMessageRecord;
typedef modMessageRecord *modMessage;

struct modMessageRecord {
	modMessage	next;
	xsMachine	*target;
	xsSlot		obj;
	uint16_t	length;
	uint8_t		kind;
	char		message[1];
};

static modMessage gMessageQueue;

int modMessagePostToMachine(xsMachine *the, xsSlot *obj, uint8_t *message, uint16_t messageLength, uint8_t messageKind)
{
	modMessage msg = c_malloc(sizeof(modMessageRecord) + messageLength);
	if (!msg) return -1;

	c_memmove(msg->message, message, messageLength);
	msg->message[messageLength] = 0;			// safe because +1 on length from message[1]
	msg->length = messageLength;
	msg->kind = messageKind;

	msg->next = NULL;
	msg->target = the;
	if (obj)
		msg->obj = *obj;
	else
		msg->obj = xsUndefined;

	// append to message queue
	if (NULL == gMessageQueue)
		gMessageQueue= msg;
	else {
		modMessage walker;

		for (walker = gMessageQueue; NULL != walker->next; walker = walker->next)
			;
		walker->next = msg;
	}

	return 0;
}

void modMessageService(void)
{
	modMessage msg = gMessageQueue;
	gMessageQueue = NULL;

	while (msg) {
		modMessage next = msg->next;

		xsBeginHost(msg->target);

		xsVars(3);

		if (0 == msg->kind) {
			xsVar(0) = xsString(msg->message);
			xsVar(1) = xsGet(xsGlobal, mxID(_JSON));
			xsVar(2) = xsCall1(xsVar(1), mxID(_parse), xsVar(0));
		}
		else
			xsVar(2) = xsArrayBuffer(msg->message, msg->length);

		if (xsTest(msg->obj))
			xsCall1(msg->obj, xsID("onmessage"), xsVar(2));	// calling instantiator - through instance
		else {
			xsVar(0) = xsGet(xsGlobal, xsID("self"));
			xsCall1(xsVar(0), xsID("onmessage"), xsVar(2));	// calling worker - through self
		}

		xsEndHost(msg->target);

		c_free(msg);
		msg = next;
	}
}

/*
	 user installable modules
*/

extern void fxRemapIDs(xsMachine* the, uint8_t* codeBuffer, uint32_t codeSize, xsIndex* theIDs);
extern txID fxNewNameX(txMachine* the, txString theString);

static void remapXSB(xsMachine *the, uint8_t *xsbRAM, int xsbSize);


#if ESP32
	const esp_partition_t *gPartition;
	const void *gPartitionAddress;
#else
	static const int FLASH_INT_MASK = ((2 << 8) | 0x3A);

	extern uint32_t _XSMOD_start;
	extern uint32_t _XSMOD_end;

	#define kModulesInstallStart ((uintptr_t)&_XSMOD_start)
	#define kModulesInstallEnd ((uintptr_t)&_XSMOD_end)
#endif

void installModules(xsMachine *the)
{
	char *atom;
	int atomSize, xsbSize, symbolCount;
	uint8_t *xsb;
	char *xsbCopy = NULL;
	uint8_t scratch[128] __attribute__((aligned(4)));

#if ESP32
	spi_flash_mmap_handle_t handle;

	gPartition = esp_partition_find_first(0x40, 1,  NULL);
	if (!gPartition) return;

	if (ESP_OK != esp_partition_read(gPartition, 0, scratch, sizeof(scratch))) {
		gPartition = NULL;
		return;
	}

	atom = findAtom(FOURCC('V', 'E', 'R', 'S'), scratch, sizeof(scratch), &atomSize);
	if (!atom) return;

	xsbSize = c_read32be(scratch);
#else
	const uint8_t *installLocation = kModulesStart;
	SpiFlashOpResult result;

	spi_flash_read((uint32)((uintptr_t)installLocation - (uintptr_t)kFlashStart), (uint32 *)scratch, sizeof(scratch));

	atom = findAtom(FOURCC('V', 'E', 'R', 'S'), scratch, sizeof(scratch), &atomSize);
	if (atom) {
		xsbSize = c_read32be(scratch);
		xsb = (char *)installLocation;
	}
	else {
		// nothing installed. see if an archive is availble to install
		spi_flash_read((uint32)((uintptr_t)kModulesInstallStart - (uintptr_t)kFlashStart), (uint32 *)scratch, sizeof(scratch));

		xsbSize = c_read32be(scratch);

		atom = findAtom(FOURCC('V', 'E', 'R', 'S'), scratch, sizeof(scratch), &atomSize);
		if (!atom)
			return;

		goto installArchive;
	}
#endif

	if (0 /* unused version byte */ != c_read8(atom + 3)) {
		// xsb has been remapped
#if ESP32
		if (ESP_OK != esp_partition_mmap(gPartition, 0, gPartition->size, SPI_FLASH_MMAP_DATA, &gPartitionAddress, &handle)) {
			gPartition = NULL;
			return;
		}
		xsb = (char *)gPartitionAddress;		// kModulesStart
#endif

		atom = findAtom(FOURCC('S', 'Y', 'M', 'B'), xsb, xsbSize, &atomSize);
		if (atom) {
			int symbolCount = c_read16be(atom);
			const char *symbol = atom + 2;
			while (symbolCount--) {
				fxNewNameX(the, (char *)symbol);
				symbol += c_strlen(symbol) + 1;
			}
		}
		return;
	}

installArchive:
	if (xsbSize > SPI_FLASH_SEC_SIZE) {
		modLog("xsb too big to remap");
		return;
	}

	// remap
	xsbCopy = c_malloc(xsbSize);
	if (NULL == xsbCopy) {
		modLog("not enough memory to remap xsb");
		return;
	}

#if ESP32
	if (ESP_OK != esp_partition_read(gPartition, 0, xsbCopy, xsbSize)) {
		modLog("xsb read fail");
		goto bail;
	}
#else
	result = spi_flash_read((uint32)((uintptr_t)kModulesInstallStart - (uintptr_t)kFlashStart), (uint32 *)xsbCopy, xsbSize);
	if (SPI_FLASH_RESULT_OK != result) {
		modLog("xsb read fail");
		goto bail;
	}

	xsb = (char *)installLocation;
#endif
	remapXSB(the, xsbCopy, xsbSize);

	atom = findAtom(FOURCC('V', 'E', 'R', 'S'), xsbCopy, xsbSize, NULL);
	atom[3] = 1;		// make as remapped

#if ESP32
	// erase
	if (ESP_OK != esp_partition_erase_range(gPartition, 0, SPI_FLASH_SEC_SIZE)) {
		modLog("erase fail");
		goto bail;
	}

	// write
	if (ESP_OK != esp_partition_write(gPartition, 0, xsbCopy, xsbSize)) {
		modLog("write fail");
		goto bail;
	}

	if (ESP_OK != esp_partition_mmap(gPartition, 0, gPartition->size, SPI_FLASH_MMAP_DATA, &gPartitionAddress, &handle)) {
		gPartition = NULL;
		return;
	}

	xsb = (char *)gPartitionAddress;		// kModulesStart
#else
	// erase
    ets_isr_mask(FLASH_INT_MASK);
	result = spi_flash_erase_sector(((uintptr_t)xsb - (uintptr_t)kFlashStart) / SPI_FLASH_SEC_SIZE);
    ets_isr_unmask(FLASH_INT_MASK);

	if (SPI_FLASH_RESULT_OK != result) {
		modLog("erase fail");
		goto bail;
	}

	// write
    ets_isr_mask(FLASH_INT_MASK);
	result = spi_flash_write((uintptr_t)xsb - (uintptr_t)kFlashStart, (void *)xsbCopy, xsbSize);
    ets_isr_unmask(FLASH_INT_MASK);

	if (SPI_FLASH_RESULT_OK != result) {
		modLog("write fail");
		goto bail;
	}
#endif

	// tell the VM abot the symbols added now available in ROM
	atom = findAtom(FOURCC('S', 'Y', 'M', 'B'), xsb, xsbSize, &atomSize);
	symbolCount = c_read16be(atom);
	atom += 2;
	while (symbolCount--) {
		fxNewNameX(the, (char *)atom);
		atom += c_strlen(atom) + 1;
	}

bail:
	if (xsbCopy)
		c_free(xsbCopy);
}

char *findAtom(uint32_t atomTypeIn, const uint8_t *xsb, int xsbSize, int *atomSizeOut)
{
	const uint8_t *atom = xsb, *xsbEnd = xsb + xsbSize;

	if (c_read32be(xsb + 4) != FOURCC('X', 'S', '_', 'B'))
		return NULL;

	atom += 8;

	while (atom < xsbEnd) {
		int32_t atomSize = c_read32be(atom);
		uint32_t atomType = c_read32be(atom + 4);

		if ((atomSize < 8) || ((atom + atomSize) > xsbEnd))
			return NULL;

		if (atomType == atomTypeIn) {
			if (atomSizeOut)
				*atomSizeOut = atomSize - 8;
			return (char *)(atom + 8);
		}
		atom += atomSize;
	}

	if (atomSizeOut)
		*atomSizeOut = 0;

	return NULL;
}

void remapXSB(xsMachine *the, uint8_t *xsbRAM, int xsbSize)
{
	uint8_t *atom;
	int atomSize;
	xsIndex *ids = NULL;
	int symbolCount;
	const char *symbol;
	int i = 0;
	txID keyIndex = the->keyIndex;

	atom = findAtom(FOURCC('V', 'E', 'R', 'S'), xsbRAM, xsbSize, &atomSize);
	if (!atom)
		return;

	if (XS_MAJOR_VERSION != c_read8(atom + 0)) {
		modLog("bad major version");
		goto bail;
	}
	if (XS_MINOR_VERSION != c_read8(atom + 1)) {
		modLog("bad minor version");
		goto bail;
	}

	atom = findAtom(FOURCC('S', 'Y', 'M', 'B'), xsbRAM, xsbSize, &atomSize);
	if (!atom) {
		modLog("no symbol atom");
		return;
	}

	symbolCount = c_read16be(atom);
	ids = c_malloc(sizeof(xsIndex) * symbolCount);
	if (!ids) {
		modLog("out of memory for id remap table");
		goto bail;
	}

	symbol = atom + 2;
	while (symbolCount--) {
		txID id = fxFindName(the, (char *)symbol);
		ids[i++] = id ? id : (keyIndex++ | 0x8000);		// not checking if would run out of keys. that will fail later when calling fxNewNameX.
		symbol += c_strlen(symbol) + 1;
	}

	atom = findAtom(FOURCC('C', 'O', 'D', 'E'), xsbRAM, xsbSize, &atomSize);
	if (!atom) goto bail;

	fxRemapIDs(the, atom, atomSize, ids);

bail:
	if (ids)
		c_free(ids);
}

#if !ESP32
#include "flash_utils.h"

uint8_t *espFindUnusedFlashStart(void)
{
	image_header_t header;
	section_header_t *section = (section_header_t *)(APP_START_OFFSET + sizeof(image_header_t) + kFlashStart);

	spi_flash_read(APP_START_OFFSET, (void *)&header, sizeof(image_header_t));
	while (header.num_segments--)
		section = (section_header_t *)(section->size + (char *)(section + 1));

	return (uint8 *)(((SPI_FLASH_SEC_SIZE - 1) + (int)section) & ~(SPI_FLASH_SEC_SIZE - 1));
}

#endif

