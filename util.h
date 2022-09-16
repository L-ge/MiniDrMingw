#ifndef UTIL_H
#define UTIL_H

#include <windows.h>
#include <imagehlp.h>

const char *getSeparator(const char *szFilename)
{
    const char *p, *q;
    p = NULL;
    q = szFilename;
    char c;
    do {
        c = *q++;
        if (c == '\\' || c == '/' || c == ':')
        {
            p = q;
        }
    } while (c);
    return p;
}

const char *getBaseName(const char *szFilename)
{
    const char *pSeparator = getSeparator(szFilename);
    if (!pSeparator)
    {
        return szFilename;
    }
    return pSeparator;
}

BOOL GetSymFromAddr(HANDLE hProcess,
               DWORD64 dwAddress,
               LPSTR lpSymName,
               DWORD nSize,
               LPDWORD lpdwDisplacement)
{
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)malloc(sizeof(SYMBOL_INFO) + nSize * sizeof(char));

    DWORD64 dwDisplacement =
        0; // Displacement of the input address, relative to the start of the symbol
    BOOL bRet;

    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = nSize;

    DWORD dwOptions = SymGetOptions();

    bRet = SymFromAddr(hProcess, dwAddress, &dwDisplacement, pSymbol);

    if (bRet) {
        // Demangle if not done already
        if ((dwOptions & SYMOPT_UNDNAME) ||
            UnDecorateSymbolName(pSymbol->Name, lpSymName, nSize, UNDNAME_NAME_ONLY) == 0) {
            strncpy(lpSymName, pSymbol->Name, nSize);
        }
        if (lpdwDisplacement) {
            *lpdwDisplacement = dwDisplacement;
        }
    }

    free(pSymbol);

    return bRet;
}

BOOL GetLineFromAddr(HANDLE hProcess,
                DWORD64 dwAddress,
                LPSTR lpFileName,
                DWORD nSize,
                LPDWORD lpLineNumber)
{
    IMAGEHLP_LINE64 Line;
    DWORD dwDisplacement =
        0; // Displacement of the input address, relative to the start of the symbol

    // Do the source and line lookup.
    memset(&Line, 0, sizeof Line);
    Line.SizeOfStruct = sizeof Line;

    if (!SymGetLineFromAddr64(hProcess, dwAddress, &dwDisplacement, &Line))
        return FALSE;

    assert(lpFileName && lpLineNumber);

    strncpy(lpFileName, Line.FileName, nSize);
    *lpLineNumber = Line.LineNumber;

    return TRUE;
}

BOOL getModuleVersionInfo(LPCSTR szModule, DWORD *dwVInfo)
{
    DWORD dummy, size;
    BOOL success = FALSE;

    size = GetFileVersionInfoSizeA(szModule, &dummy);
    if (size > 0) {
        LPVOID pVer = malloc(size);
        ZeroMemory(pVer, size);
        if (GetFileVersionInfoA(szModule, 0, size, pVer)) {
            VS_FIXEDFILEINFO *ffi;
            if (VerQueryValueA(pVer, "\\", (LPVOID *)&ffi, (UINT *)&dummy)) {
                dwVInfo[0] = ffi->dwFileVersionMS >> 16;
                dwVInfo[1] = ffi->dwFileVersionMS & 0xFFFF;
                dwVInfo[2] = ffi->dwFileVersionLS >> 16;
                dwVInfo[3] = ffi->dwFileVersionLS & 0xFFFF;
                success = TRUE;
            }
        }
        free(pVer);
    }
    return success;
}

#ifndef STATUS_FATAL_USER_CALLBACK_EXCEPTION
#define STATUS_FATAL_USER_CALLBACK_EXCEPTION ((NTSTATUS)0xC000041DL)
#endif
#ifndef STATUS_CPP_EH_EXCEPTION
#define STATUS_CPP_EH_EXCEPTION ((NTSTATUS)0xE06D7363L)
#endif
#ifndef STATUS_CLR_EXCEPTION
#define STATUS_CLR_EXCEPTION ((NTSTATUS)0xE0434f4DL)
#endif
#ifndef STATUS_WX86_BREAKPOINT
#define STATUS_WX86_BREAKPOINT ((NTSTATUS)0x4000001FL)
#endif
#ifndef STATUS_POSSIBLE_DEADLOCK
#define STATUS_POSSIBLE_DEADLOCK ((DWORD)0xC0000194L)
#endif

LPCSTR getExceptionString(NTSTATUS ExceptionCode)
{
    switch (ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION: // 0xC0000005
        return "Access Violation";
    case EXCEPTION_IN_PAGE_ERROR: // 0xC0000006
        return "In Page Error";
    case EXCEPTION_INVALID_HANDLE: // 0xC0000008
        return "Invalid Handle";
    case EXCEPTION_ILLEGAL_INSTRUCTION: // 0xC000001D
        return "Illegal Instruction";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: // 0xC0000025
        return "Cannot Continue";
    case EXCEPTION_INVALID_DISPOSITION: // 0xC0000026
        return "Invalid Disposition";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: // 0xC000008C
        return "Array bounds exceeded";
    case EXCEPTION_FLT_DENORMAL_OPERAND: // 0xC000008D
        return "Floating-point denormal operand";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: // 0xC000008E
        return "Floating-point division by zero";
    case EXCEPTION_FLT_INEXACT_RESULT: // 0xC000008F
        return "Floating-point inexact result";
    case EXCEPTION_FLT_INVALID_OPERATION: // 0xC0000090
        return "Floating-point invalid operation";
    case EXCEPTION_FLT_OVERFLOW: // 0xC0000091
        return "Floating-point overflow";
    case EXCEPTION_FLT_STACK_CHECK: // 0xC0000092
        return "Floating-point stack check";
    case EXCEPTION_FLT_UNDERFLOW: // 0xC0000093
        return "Floating-point underflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: // 0xC0000094
        return "Integer division by zero";
    case EXCEPTION_INT_OVERFLOW: // 0xC0000095
        return "Integer overflow";
    case EXCEPTION_PRIV_INSTRUCTION: // 0xC0000096
        return "Privileged instruction";
    case EXCEPTION_STACK_OVERFLOW: // 0xC00000FD
        return "Stack Overflow";
    case EXCEPTION_POSSIBLE_DEADLOCK: // 0xC0000194
        return "Possible deadlock condition";
    case STATUS_FATAL_USER_CALLBACK_EXCEPTION: // 0xC000041D
        return "Fatal User Callback Exception";
    case STATUS_ASSERTION_FAILURE: // 0xC0000420
        return "Assertion failure";

    case STATUS_CLR_EXCEPTION: // 0xE0434f4D
        return "CLR exception";
    case STATUS_CPP_EH_EXCEPTION: // 0xE06D7363
        return "C++ exception handling exception";

    case EXCEPTION_GUARD_PAGE: // 0x80000001
        return "Guard Page Exception";
    case EXCEPTION_DATATYPE_MISALIGNMENT: // 0x80000002
        return "Alignment Fault";
    case EXCEPTION_BREAKPOINT: // 0x80000003
        return "Breakpoint";
    case EXCEPTION_SINGLE_STEP: // 0x80000004
        return "Single Step";

    case STATUS_WX86_BREAKPOINT: // 0x4000001F
        return "Breakpoint";
    case DBG_TERMINATE_THREAD: // 0x40010003
        return "Terminate Thread";
    case DBG_TERMINATE_PROCESS: // 0x40010004
        return "Terminate Process";
    case DBG_CONTROL_C: // 0x40010005
        return "Control+C";
    case DBG_CONTROL_BREAK: // 0x40010008
        return "Control+Break";
    case 0x406D1388:
        return "Thread Name Exception";

    case RPC_S_UNKNOWN_IF:
        return "Unknown Interface";
    case RPC_S_SERVER_UNAVAILABLE:
        return "Server Unavailable";

    default:
        return NULL;
    }
}

#endif // UTIL_H
