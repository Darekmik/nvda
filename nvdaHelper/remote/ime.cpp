/*
This file is a part of the NVDA project.
URL: http://www.nvda-project.org/
Copyright 2010-2012 World Light Information Limited and Hong Kong Blind Union.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2.0, as published by
    the Free Software Foundation.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

#include <windows.h>
#include <wchar.h>
#include "nvdaHelperRemote.h"
#include "nvdaControllerInternal.h"
#include "typedCharacter.h"
#include "tsf.h"
#include "ime.h"

#define GETLANG()		LOWORD(g_hklCurrent)
#define GETPRIMLANG()	((WORD)PRIMARYLANGID(GETLANG()))
#define GETSUBLANG()	SUBLANGID(GETLANG())

#define LANG_CHS MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)
#define LANG_CHT MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)

#define MAKEIMEVERSION(major,minor) ( (DWORD)( ( (BYTE)( major ) << 24 ) | ( (BYTE)( minor ) << 16 ) ) )
#define IMEID_VER(dwId)		( ( dwId ) & 0xffff0000 )
#define IMEID_LANG(dwId)	( ( dwId ) & 0x0000ffff )

#define _CHT_HKL_DAYI				( (HKL)0xE0060404 )	// DaYi
#define _CHT_HKL_NEW_PHONETIC		( (HKL)0xE0080404 )	// New Phonetic
#define _CHT_HKL_NEW_CHANG_JIE		( (HKL)0xE0090404 )	// New Chang Jie
#define _CHT_HKL_NEW_QUICK			( (HKL)0xE00A0404 )	// New Quick
#define _CHT_HKL_HK_CANTONESE		( (HKL)0xE00B0404 )	// Hong Kong Cantonese
#define _CHT_IMEFILENAME	"TINTLGNT.IME"	// New Phonetic
#define _CHT_IMEFILENAME2	"CINTLGNT.IME"	// New Chang Jie
#define _CHT_IMEFILENAME3	"MSTCIPHA.IME"	// Phonetic 5.1
#define IMEID_CHT_VER42 ( LANG_CHT | MAKEIMEVERSION( 4, 2 ) )	// New(Phonetic/ChanJie)IME98  : 4.2.x.x // Win98
#define IMEID_CHT_VER43 ( LANG_CHT | MAKEIMEVERSION( 4, 3 ) )	// New(Phonetic/ChanJie)IME98a : 4.3.x.x // Win2k
#define IMEID_CHT_VER44 ( LANG_CHT | MAKEIMEVERSION( 4, 4 ) )	// New ChanJie IME98b          : 4.4.x.x // WinXP
#define IMEID_CHT_VER50 ( LANG_CHT | MAKEIMEVERSION( 5, 0 ) )	// New(Phonetic/ChanJie)IME5.0 : 5.0.x.x // WinME
#define IMEID_CHT_VER51 ( LANG_CHT | MAKEIMEVERSION( 5, 1 ) )	// New(Phonetic/ChanJie)IME5.1 : 5.1.x.x // IME2002(w/OfficeXP)
#define IMEID_CHT_VER52 ( LANG_CHT | MAKEIMEVERSION( 5, 2 ) )	// New(Phonetic/ChanJie)IME5.2 : 5.2.x.x // IME2002a(w/WinXP)
#define IMEID_CHT_VER60 ( LANG_CHT | MAKEIMEVERSION( 6, 0 ) )	// New(Phonetic/ChanJie)IME6.0 : 6.0.x.x // New IME 6.0(web download)
#define IMEID_CHT_VER_VISTA ( LANG_CHT | MAKEIMEVERSION( 7, 0 ) )	// All TSF TIP under Cicero UI-less mode: a hack to make GetImeId() return non-zero value

#define _CHS_HKL		( (HKL)0xE00E0804 )	// MSPY
#define _CHS_IMEFILENAME	"PINTLGNT.IME"	// MSPY1.5/2/3
#define _CHS_IMEFILENAME2	"MSSCIPYA.IME"	// MSPY3 for OfficeXP
#define IMEID_CHS_VER41	( LANG_CHS | MAKEIMEVERSION( 4, 1 ) )	// MSPY1.5	// SCIME97 or MSPY1.5 (w/Win98, Office97)
#define IMEID_CHS_VER42	( LANG_CHS | MAKEIMEVERSION( 4, 2 ) )	// MSPY2	// Win2k/WinME
#define IMEID_CHS_VER53	( LANG_CHS | MAKEIMEVERSION( 5, 3 ) )	// MSPY3	// WinXP

// Definition from Win98DDK version of IMM.H
typedef struct tagINPUTCONTEXT2 {
    HWND            hWnd;                           
    BOOL            fOpen;                          
    POINT           ptStatusWndPos;                 
    POINT           ptSoftKbdPos;                   
    DWORD           fdwConversion;                  
    DWORD           fdwSentence;                    
    union {                                           
        LOGFONTA    A;                              
        LOGFONTW    W;                              
    } lfFont;                                           
    COMPOSITIONFORM cfCompForm;                     
    CANDIDATEFORM   cfCandForm[4];                  
    HIMCC           hCompStr;                       
    HIMCC           hCandInfo;                      
    HIMCC           hGuideLine;                     
    HIMCC           hPrivate;                       
    DWORD           dwNumMsgBuf;                    
    HIMCC           hMsgBuf;                        
    DWORD           fdwInit;                        
    DWORD           dwReserve[3];                   
} INPUTCONTEXT2, *PINPUTCONTEXT2, NEAR *NPINPUTCONTEXT2, FAR *LPINPUTCONTEXT2;  

static HMODULE gImm32Module = NULL;
static DWORD lastConversionModeFlags=0;
bool disableIMEConversionModeUpdateReporting=false;

static LPINPUTCONTEXT2 (WINAPI* immLockIMC)(HIMC) = NULL;
static BOOL (WINAPI* immUnlockIMC)(HIMC) = NULL;
static LPVOID (WINAPI* immLockIMCC)(HIMCC) = NULL;
static BOOL (WINAPI* immUnlockIMCC)(HIMCC) = NULL;


DWORD getIMEVersion(HKL kbd_layout, wchar_t* filename) {
	DWORD version=0;
	switch ((DWORD)kbd_layout) {
		case _CHT_HKL_NEW_PHONETIC:
		case _CHT_HKL_NEW_CHANG_JIE:
		case _CHT_HKL_NEW_QUICK:
		case _CHT_HKL_HK_CANTONESE:
		case _CHS_HKL:
			break;
		default:
			// Do not know how to extract version number
			return 0;
	}
	DWORD ver_handle;
	DWORD buf_size = GetFileVersionInfoSizeW(filename, &ver_handle);
	if (!buf_size)  return 0;
	void* buf = malloc(buf_size);
    if (!buf)  return 0;
	if (GetFileVersionInfoW(filename, ver_handle, buf_size, buf)) {
		void* data = NULL;
		UINT  data_len;
		if (VerQueryValueW(buf, L"\\", &data, &data_len)) {
			VS_FIXEDFILEINFO FAR* info = (VS_FIXEDFILEINFO FAR*)data;
			version = (info->dwFileVersionMS & 0x00ff0000) << 8
					| (info->dwFileVersionMS & 0x000000ff) << 16
					| (DWORD)kbd_layout & 0xffff;
		}
	}
	free(buf);
	return version;
}

typedef UINT (WINAPI* GetReadingString_funcType)(HIMC, UINT, LPWSTR, PINT, BOOL*, PUINT);

void handleReadingStringUpdate(HWND hwnd) {
	/* Obtain IME context */
	HIMC imc = ImmGetContext(hwnd);
	if (!imc)  return;
	wchar_t* read_str=NULL;
	HKL kbd_layout = GetKeyboardLayout(0);
	WCHAR filename[MAX_PATH + 1];
	DWORD version=0;
	HMODULE IMEFile=NULL;
	GetReadingString_funcType GetReadingString=NULL;
	if(ImmGetIMEFileNameW(kbd_layout, filename, MAX_PATH)>0) {
		IMEFile=LoadLibrary(filename);
		if(IMEFile) {
			GetReadingString=(GetReadingString_funcType)GetProcAddress(IMEFile, "GetReadingString");
		}
		if(!GetReadingString) {
			version=getIMEVersion(kbd_layout,filename);
		}
	}
	if(GetReadingString) {
		// Use GetReadingString() API if available
		UINT   len = 0;
		INT    err = 0;
		BOOL vert = FALSE;
		UINT max_len = 0;
		len = GetReadingString(imc, 0, NULL, &err, &vert, &max_len);
		if (len) {
			read_str = (WCHAR*)malloc(sizeof(WCHAR) * (len + 1));
			read_str[len] = '\0';
			GetReadingString(imc, len, read_str, &err, &vert, &max_len);
		}
	} else if(version) {
		// Read private data in IMCC
		UINT   len = 0;
		INT    err = 0;
		LPINPUTCONTEXT2 ctx = immLockIMC(imc);
		LPBYTE          priv = (LPBYTE)immLockIMCC(ctx->hPrivate);
		LPBYTE          p = 0;
		LPBYTE          str = NULL;
		switch (version) {
			case IMEID_CHT_VER42:
			case IMEID_CHT_VER43:
			case IMEID_CHT_VER44:
				p = *(LPBYTE*)(priv + 24);
				if (!p) break;
				len = *(DWORD*)(p + 7*4 + 32*4);  //m_dwInputReadStrLen
				err = *(DWORD*)(p + 8*4 + 32*4);  //m_dwErrorReadStrStart
				str =          (p + 56);
				break;

			case IMEID_CHT_VER51:  // 5.1.x.x // IME2002(w/OfficeXP)
			case IMEID_CHT_VER52:  // 5.2.x.x // (w/whistler)
			case IMEID_CHS_VER53:  // 5.3.x.x // SCIME2k or MSPY3 (w/OfficeXP and Whistler)
				p = *(LPBYTE*)(priv + 4);  // PCKeyCtrlManager
				if (!p) break;
				p = *(LPBYTE*)((LPBYTE)p + 1*4 + 5*4);  // = PCReading = &STypingInfo
				if (!p) break;
				len = *(DWORD*)(p + 1*4 + (16*2+2*4) + 5*4 + 16*2);        //m_dwDisplayStringLength;
				err = *(DWORD*)(p + 1*4 + (16*2+2*4) + 5*4 + 16*2 + 1*4);  //m_dwDisplayErrorStart;
				str =          (p + 1*4 + (16*2+2*4) + 5*4);
				break;

			case IMEID_CHS_VER42:  // 4.2.x.x // SCIME98 or MSPY2 (w/Office2k, Win2k, WinME, etc)
				p = *(LPBYTE*)(priv + 1*4 + 1*4 + 6*4);  // = PCReading = &STypintInfo
				if (!p) break;
				len = *(DWORD*)(p + 1*4 + (16*2+2*4) + 5*4 + 16*2);        //m_dwDisplayStringLength;
				err = *(DWORD*)(p + 1*4 + (16*2+2*4) + 5*4 + 16*2 + 1*4);  //m_dwDisplayErrorStart;
				str =          (p + 1*4 + (16*2+2*4) + 5*4);               //m_tszDisplayString
				break;
		}
		read_str = (WCHAR*)malloc(sizeof(WCHAR) * (len + 1));
		read_str[len] = '\0';
		memcpy(read_str, str, sizeof(WCHAR) * len);
		immUnlockIMCC(ctx->hPrivate);
		immUnlockIMC(imc);
	}
	if(IMEFile) FreeLibrary(IMEFile);
	ImmReleaseContext(hwnd, imc);
	if(read_str) {
		long len=(long)wcslen(read_str);
		if(len>1||(len==1&&read_str[0]!=L'\x3000')) {
			long cursorPos=(long)wcslen(read_str);
			nvdaControllerInternal_inputCompositionUpdate(read_str,cursorPos,cursorPos,1);
		}
		free(read_str);
	}
}

void handleIMEConversionModeUpdate(HWND hwnd, bool report) {
	/* Obtain IME context */
	HIMC imc = ImmGetContext(hwnd);
	if (!imc)  return;
	DWORD flags=0;
	ImmGetConversionStatus(imc,&flags,NULL);
	ImmReleaseContext(hwnd, imc);
	if(report&&flags!=lastConversionModeFlags) {
		nvdaControllerInternal_inputConversionModeUpdate(lastConversionModeFlags,flags,((unsigned long)GetKeyboardLayout(0))&0xffff);
	}
	lastConversionModeFlags=flags;
}

static bool handleCandidates(HWND hwnd) {
	/* Obtain IME context */
	HIMC imc = ImmGetContext(hwnd);
	if (!imc)  return false;

	/* Make sure there is at least one candidate list */
	DWORD count = 0;
	DWORD len = ImmGetCandidateListCountW(imc, &count);
	if (!count) {
		ImmReleaseContext(hwnd, imc);
		return false;
	}

	/* Read first candidate list */
	CANDIDATELIST* list = (CANDIDATELIST*)malloc(len);
	ImmGetCandidateList(imc, 0, list, len);
	ImmReleaseContext(hwnd, imc);

	/* Determine candidates currently being shown */
	DWORD pageEnd = list->dwPageStart + list->dwPageSize;
	DWORD selection=list->dwSelection-list->dwPageStart;
	if (list->dwPageSize == 0) {
		pageEnd = list->dwCount;
	} else if (pageEnd > list->dwCount) {
		pageEnd = list->dwCount;
	}

	/* Concatenate currently shown candidates into a string */
	WCHAR* cand_str = (WCHAR*)malloc(len);
	WCHAR* ptr = cand_str;
	for (DWORD n = list->dwPageStart, count = 0;  n < pageEnd;  ++n) {
		DWORD offset = list->dwOffset[n];
		WCHAR* cand = (WCHAR*)(((char*)list) + offset);
		size_t clen = wcslen(cand);
		if (!clen)  continue;
		CopyMemory(ptr, cand, (clen + 1) * sizeof(WCHAR));
		if ((n + 1) < pageEnd)  ptr[clen] = '\n';
		ptr += (clen + 1);
		++count;
	}
	if(cand_str&&wcslen(cand_str)>0) {
		nvdaControllerInternal_inputCandidateListUpdate(cand_str,selection);
	}
	/* Clean up */
	free(cand_str);
	free(list);
	return (count > 0);
}

static WCHAR* getCompositionString(HIMC imc, DWORD index) {
	int len = ImmGetCompositionStringW(imc, index, 0, 0);
	if (len < sizeof(WCHAR))  return NULL;
	WCHAR* wstr = (WCHAR*)malloc(len + sizeof(WCHAR));
	len = ImmGetCompositionStringW(imc, index, wstr, len) / sizeof(WCHAR);
	wstr[len] = '\0';
	 return wstr;
}

static bool handleComposition(HWND hwnd, WPARAM wParam, LPARAM lParam) {
	/* Obtain IME context */
	HIMC imc = ImmGetContext(hwnd);
	if (!imc)  return false;

	wchar_t* comp_str = getCompositionString(imc, GCS_COMPSTR);
	long selectionStart=ImmGetCompositionString(imc,GCS_CURSORPOS,NULL,0)&0xffff;
	ImmReleaseContext(hwnd, imc);
	if(!comp_str) return false;

	/* Generate notification */
	long len=(long)wcslen(comp_str);
	if(len>1||(len==1&&comp_str[0]!=L'\x3000')) {
		nvdaControllerInternal_inputCompositionUpdate(comp_str,selectionStart,selectionStart,0);
	}
	free(comp_str);
	return true;
}

static bool handleEndComposition(HWND hwnd, WPARAM wParam, LPARAM lParam) {
	/* Obtain IME context */
	HIMC imc = ImmGetContext(hwnd);
	if (!imc)  return false;

	wchar_t* comp_str = getCompositionString(imc, GCS_RESULTSTR);
	ImmReleaseContext(hwnd, imc);

	/* Generate notification */
	nvdaControllerInternal_inputCompositionUpdate((comp_str?comp_str:L""),-1,-1,0);
	if(comp_str) {
		free(comp_str);
		return true;
	}
	return false;
}

bool hasValidIMEContext(HWND hwnd) {
	if(!hwnd) return false;
	bool valid_imc = false;
	HIMC imc = ImmGetContext(hwnd);
	if (imc) {
		LPINPUTCONTEXT2 ctx = immLockIMC(imc);
		if (ctx) {
			if (ctx->hWnd)  valid_imc = true;
			immUnlockIMC(imc);
		}
		ImmReleaseContext(hwnd, imc);
	}
	return valid_imc;
}

static LRESULT CALLBACK IME_callWndProcHook(int code, WPARAM wParam, LPARAM lParam) {
	static HWND curIMEWindow=NULL;
	CWPSTRUCT* pcwp=(CWPSTRUCT*)lParam;
	// Ignore messages with invalid HIMC
	if(!hasValidIMEContext(pcwp->hwnd)) return 0; 
	switch (pcwp->message) {
		case WM_IME_NOTIFY:
			switch (pcwp->wParam) {
				case IMN_OPENCANDIDATE:
				case IMN_CHANGECANDIDATE:
					handleCandidates(pcwp->hwnd);
					break;

				case IMN_CLOSECANDIDATE:
					nvdaControllerInternal_inputCandidateListUpdate(L"",-1);
					break;

				case IMN_SETCONVERSIONMODE:
					if(!disableIMEConversionModeUpdateReporting) handleIMEConversionModeUpdate(pcwp->hwnd,true);
					break;
			}

		case WM_IME_COMPOSITION:
			if(!isTSFThread(true)) {
				handleReadingStringUpdate(pcwp->hwnd);
				bool compFailed=false;
				if(lParam&GCS_COMPSTR||lParam&GCS_CURSORPOS) {
					compFailed=!handleComposition(pcwp->hwnd, pcwp->wParam, pcwp->lParam);
					if(!compFailed) curIMEWindow=pcwp->hwnd;
				}
				if(compFailed&&curIMEWindow==pcwp->hwnd) {
					handleEndComposition(pcwp->hwnd, pcwp->wParam, pcwp->lParam);
					curIMEWindow=NULL;
					//Disable further typed character notifications produced by TSF
					typedCharacter_window=NULL;
				}
			}
			break;

		case WM_IME_ENDCOMPOSITION:
			if(curIMEWindow==pcwp->hwnd&&!isTSFThread(true)) {
				handleEndComposition(pcwp->hwnd, pcwp->wParam, pcwp->lParam);
				curIMEWindow=NULL;
				//Disable further typed character notifications produced by TSF
				typedCharacter_window=NULL;
			}
			break;

		case WM_ACTIVATE:
		case WM_SETFOCUS:
			handleIMEConversionModeUpdate(pcwp->hwnd,false);
			if(!isTSFThread(true)) {
				if (pcwp->hwnd != GetFocus())  break;
				handleComposition(pcwp->hwnd, pcwp->wParam, pcwp->lParam);
				handleCandidates(pcwp->hwnd);
			}
			break;
		default:
			break;
	}
	return 0;
}

WCHAR* IME_getCompositionString() {
	HWND hwnd = GetFocus();
	if (!hwnd)  return NULL;
	HIMC imc = ImmGetContext(hwnd);
	if (!imc)  return NULL;
	WCHAR* comp_str = getCompositionString(imc, GCS_COMPSTR);
	ImmReleaseContext(hwnd, imc);
	return comp_str;
}

void IME_inProcess_initialize() {
	gImm32Module = LoadLibraryA("imm32.dll");
	if (gImm32Module) {
		immLockIMC    = (LPINPUTCONTEXT2 (WINAPI*)(HIMC))
			GetProcAddress(gImm32Module, "ImmLockIMC");
		immUnlockIMC  = (BOOL (WINAPI*)(HIMC))
			GetProcAddress(gImm32Module, "ImmUnlockIMC");
		immLockIMCC   = (LPVOID (WINAPI*)(HIMCC))
			GetProcAddress(gImm32Module, "ImmLockIMCC");
		immUnlockIMCC = (BOOL (WINAPI*)(HIMCC))
			GetProcAddress(gImm32Module, "ImmUnlockIMCC");
	}
	registerWindowsHook(WH_CALLWNDPROC, IME_callWndProcHook);
}

void IME_inProcess_terminate() {
	unregisterWindowsHook(WH_CALLWNDPROC, IME_callWndProcHook);
	if (gImm32Module)  FreeLibrary(gImm32Module);
}