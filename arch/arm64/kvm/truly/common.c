//	===
//	======				Utility Functions					======
//	===

//
//	TP version of memset()
#include "common.h"
#include "funcCipherStruct.h"
#include "osal.h"

#ifdef _LINUX
	#define ATTR_MOD1 __attribute__((section(".mod1"))) 
#else
	#define ATTR_MOD1
#endif


//#pragma code_seg(push, r1, ".mod1")

ATTR_MOD1 void __cdecl TPmemset(void * _Dst, int _Val, size_t _Size)
{
	memset(_Dst, _Val, _Size);
}

ATTR_MOD1 void TPmemcpy(void *dst, const void *src, size_t size)
{
	memcpy((unsigned char*)dst, (const unsigned char*)src, size);
}

size_t TPstrlen(const char *str)
{
	size_t len;
	for (len = 0; *str; ++str, ++len);
	return len;
}

ATTR_MOD1 int TPmemcmp(const void * _Buf1, const void * _Buf2, size_t _Size)
{
	PUCHAR p1 = (PUCHAR)_Buf1,  p2 = (PUCHAR)_Buf2 ;

	for (; _Size--; ++p1, ++p2)
		if (*p1 != *p2)
			return (*p1 - *p2) ;

	return 0 ;
}

//#pragma code_seg(pop, r1)

unsigned long long get_ticks_per_second(void)
{
	return 2000000; // 
}

#ifdef _LINUX
	extern int sprintf(char *buf, const char *fmt, ...);
#endif

wchar_t* describe_status(OSSTATUS status)
{
}
