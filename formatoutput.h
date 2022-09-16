#ifndef FORMATOUTPUT_H
#define FORMATOUTPUT_H

#include "log.h"
#include "util.h"

BOOL dumpSourceCode(LPCSTR lpFileName, DWORD dwLineNumber)
{
    FILE *fp;
    unsigned i;
    DWORD dwContext = 2;

    if ((fp = fopen(lpFileName, "r")) == NULL)
        return FALSE;

    i = 0;
    while (!feof(fp) && ++i <= dwLineNumber + dwContext) {
        int c;

        if ((int)i >= (int)dwLineNumber - (int)dwContext) {
            writeLog(i == dwLineNumber ? ">%5i: " : "%6i: ", i);
            while (!feof(fp) && (c = fgetc(fp)) != '\n')
                if (isprint(c))
                    writeLog("%c", c);
            writeLog("\n");
        } else {
            while (!feof(fp) && fgetc(fp) != '\n')
                ;
        }
    }

    fclose(fp);
    return TRUE;
}

void dumpContext(
#ifdef _WIN64
    const WOW64_CONTEXT *pContext
#else
    const CONTEXT *pContext
#endif
)
{
    // Show the registers
    writeLog("Registers:\n");
    if (pContext->ContextFlags & CONTEXT_INTEGER) {
        writeLog("eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n", pContext->Eax,
                pContext->Ebx, pContext->Ecx, pContext->Edx, pContext->Esi, pContext->Edi);
    }
    if (pContext->ContextFlags & CONTEXT_CONTROL) {
        writeLog("eip=%08lx esp=%08lx ebp=%08lx iopl=%1lx %s %s %s %s %s %s %s %s %s %s\n",
                pContext->Eip, pContext->Esp, pContext->Ebp,
                (pContext->EFlags >> 12) & 3,                  //  IOPL level value
                pContext->EFlags & 0x00100000 ? "vip" : "   ", //  VIP (virtual interrupt pending)
                pContext->EFlags & 0x00080000 ? "vif" : "   ", //  VIF (virtual interrupt flag)
                pContext->EFlags & 0x00000800 ? "ov" : "nv",   //  VIF (virtual interrupt flag)
                pContext->EFlags & 0x00000400 ? "dn" : "up",   //  OF (overflow flag)
                pContext->EFlags & 0x00000200 ? "ei" : "di",   //  IF (interrupt enable flag)
                pContext->EFlags & 0x00000080 ? "ng" : "pl",   //  SF (sign flag)
                pContext->EFlags & 0x00000040 ? "zr" : "nz",   //  ZF (zero flag)
                pContext->EFlags & 0x00000010 ? "ac" : "na",   //  AF (aux carry flag)
                pContext->EFlags & 0x00000004 ? "po" : "pe",   //  PF (parity flag)
                pContext->EFlags & 0x00000001 ? "cy" : "nc"    //  CF (carry flag)
        );
    }
    if (pContext->ContextFlags & CONTEXT_SEGMENTS) {
        writeLog("cs=%04lx  ss=%04lx  ds=%04lx  es=%04lx  fs=%04lx  gs=%04lx", pContext->SegCs,
                pContext->SegSs, pContext->SegDs, pContext->SegEs, pContext->SegFs,
                pContext->SegGs);
        if (pContext->ContextFlags & CONTEXT_CONTROL) {
            writeLog("             efl=%08lx", pContext->EFlags);
        }
    } else {
        if (pContext->ContextFlags & CONTEXT_CONTROL) {
            writeLog("                                                                       "
                    "efl=%08lx",
                    pContext->EFlags);
        }
    }
    writeLog("\n\n");
}

void dumpStack(HANDLE hProcess, HANDLE hThread, const CONTEXT *pContext)
{
    DWORD MachineType;

    assert(pContext);

    STACKFRAME64 StackFrame;
    ZeroMemory(&StackFrame, sizeof StackFrame);

    BOOL bWow64 = FALSE;
    if (0) {       // if (HAVE_WIN64)
        IsWow64Process(hProcess, &bWow64);
    }
    if (bWow64) {
        const WOW64_CONTEXT *pWow64Context = reinterpret_cast<const WOW64_CONTEXT *>(pContext);
        // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
        assert((pWow64Context->ContextFlags & WOW64_CONTEXT_FULL) == WOW64_CONTEXT_FULL);
#ifdef _WIN64
        dumpContext(pWow64Context);
#endif
        MachineType = IMAGE_FILE_MACHINE_I386;
        StackFrame.AddrPC.Offset = pWow64Context->Eip;
        StackFrame.AddrStack.Offset = pWow64Context->Esp;
        StackFrame.AddrFrame.Offset = pWow64Context->Ebp;
    } else {
        // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
        assert((pContext->ContextFlags & CONTEXT_FULL) == CONTEXT_FULL);
#ifndef _WIN64
        MachineType = IMAGE_FILE_MACHINE_I386;
        dumpContext(pContext);                      // 打印当时寄存器的信息
        StackFrame.AddrPC.Offset = pContext->Eip;
        StackFrame.AddrStack.Offset = pContext->Esp;
        StackFrame.AddrFrame.Offset = pContext->Ebp;
#else
        MachineType = IMAGE_FILE_MACHINE_AMD64;
        StackFrame.AddrPC.Offset = pContext->Rip;
        StackFrame.AddrStack.Offset = pContext->Rsp;
        StackFrame.AddrFrame.Offset = pContext->Rbp;
#endif
    }
    StackFrame.AddrPC.Mode = AddrModeFlat;
    StackFrame.AddrStack.Mode = AddrModeFlat;
    StackFrame.AddrFrame.Mode = AddrModeFlat;

    /*
     * StackWalk64 modifies Context, so pass a copy.
     */
    CONTEXT Context = *pContext;

    if (MachineType == IMAGE_FILE_MACHINE_I386) {
        writeLog("AddrPC   Params\n");
    } else {
        writeLog("AddrPC           Params\n");
    }

    BOOL bInsideWine = []() -> BOOL
    {
            HMODULE hNtDll = GetModuleHandleA("ntdll");
            if (!hNtDll) {
                return FALSE;
            }
            return GetProcAddress(hNtDll, "wine_get_version") != NULL;
    }();

    DWORD64 PrevFrameStackOffset = StackFrame.AddrStack.Offset - 1;
    int nudge = 0;

    while (TRUE) {
        constexpr int MAX_SYM_NAME_SIZE = 512;
        char szSymName[MAX_SYM_NAME_SIZE] = "";
        char szFileName[MAX_PATH] = "";
        DWORD dwLineNumber = 0;

        if (!StackWalk64(MachineType, hProcess, hThread, &StackFrame, &Context,
                         NULL, // ReadMemoryRoutine
                         SymFunctionTableAccess64, SymGetModuleBase64,
                         NULL // TranslateAddress
                         ))
            break;

        if (MachineType == IMAGE_FILE_MACHINE_I386) {
            writeLog("%08lX %08lX %08lX %08lX", (DWORD)StackFrame.AddrPC.Offset,
                    (DWORD)StackFrame.Params[0], (DWORD)StackFrame.Params[1],
                    (DWORD)StackFrame.Params[2]);
        } else {
            writeLog("%016I64X %016I64X %016I64X %016I64X", StackFrame.AddrPC.Offset,
                    StackFrame.Params[0], StackFrame.Params[1], StackFrame.Params[2]);
        }

        BOOL bSymbol = TRUE;
        BOOL bLine = FALSE;
        DWORD dwOffsetFromSymbol = 0;

        DWORD64 AddrPC = StackFrame.AddrPC.Offset;
        HMODULE hModule = (HMODULE)(INT_PTR)SymGetModuleBase64(hProcess, AddrPC);
        char szModule[MAX_PATH];
        if (hModule && GetModuleFileNameExA(hProcess, hModule, szModule, MAX_PATH)) {
            writeLog("  %s", getBaseName(szModule));

            bSymbol = GetSymFromAddr(hProcess, AddrPC + nudge, szSymName, MAX_SYM_NAME_SIZE,
                                     &dwOffsetFromSymbol);
            if (bSymbol) {
                writeLog("!%s+0x%lx", szSymName, dwOffsetFromSymbol - nudge);

                bLine =
                    GetLineFromAddr(hProcess, AddrPC + nudge, szFileName, MAX_PATH, &dwLineNumber);
                if (bLine) {
                    writeLog("  [%s @ %ld]", szFileName, dwLineNumber);
                }
            } else {
                writeLog("!0x%I64x", AddrPC - (DWORD64)(INT_PTR)hModule);
            }
        }

        writeLog("\n");

        if (bLine) {
            dumpSourceCode(szFileName, dwLineNumber);
        }

        // Basic sanity check to make sure  the frame is OK.  Bail if not.
        if (StackFrame.AddrStack.Offset <= PrevFrameStackOffset ||
            StackFrame.AddrPC.Offset == 0xBAADF00D) {
            break;
        }
        PrevFrameStackOffset = StackFrame.AddrStack.Offset;

        // Wine's StackWalk64 implementation on certain yield never ending
        // stack backtraces unless one bails out when AddrFrame is zero.
        if (bInsideWine && StackFrame.AddrFrame.Offset == 0) {
            break;
        }

        /*
         * When we walk into the callers, StackFrame.AddrPC.Offset will not
         * contain the calling function's address, but rather the return
         * address.  This could be the next statement, or sometimes (for
         * no-return functions) a completely different function, so nudge the
         * address by one byte to ensure we get the information about the
         * calling statement itself.
         */
        nudge = -1;
    }

    writeLog("\n");
}

void dumpException(HANDLE hProcess, PEXCEPTION_RECORD pExceptionRecord)
{
    NTSTATUS ExceptionCode = pExceptionRecord->ExceptionCode;

    char szModule[MAX_PATH];
    LPCSTR lpcszProcess;
    HMODULE hModule;

    if (GetModuleFileNameExA(hProcess, NULL, szModule, MAX_PATH))
    {
        lpcszProcess = getBaseName(szModule);
    }
    else
    {
        lpcszProcess = "Application";
    }

    // First print information about the type of fault
    writeLog("%s caused", lpcszProcess);

    LPCSTR lpcszException = getExceptionString(ExceptionCode);
    if (lpcszException) {
        LPCSTR lpszArticle;
        switch (lpcszException[0]) {
        case 'A':
        case 'E':
        case 'I':
        case 'O':
        case 'U':
            lpszArticle = "an";
            break;
        default:
            lpszArticle = "a";
            break;
        }

        writeLog(" %s %s", lpszArticle, lpcszException);
    } else {
        writeLog(" an Unknown [0x%lX] Exception", ExceptionCode);
    }

    // Now print information about where the fault occurred
    writeLog(" at location %p", pExceptionRecord->ExceptionAddress);
    if((hModule = (HMODULE)(INT_PTR)
             SymGetModuleBase64(hProcess, (DWORD64)(INT_PTR)pExceptionRecord->ExceptionAddress)) &&
        GetModuleFileNameExA(hProcess, hModule, szModule, sizeof szModule))
    {
        writeLog(" in module %s", getBaseName(szModule));
    }

    // If the exception was an access violation, print out some additional information, to the error
    // log and the debugger.
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082%28v=vs.85%29.aspx
    if ((ExceptionCode == EXCEPTION_ACCESS_VIOLATION || ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        pExceptionRecord->NumberParameters >= 2)
    {
        LPCSTR lpszVerb;
        switch (pExceptionRecord->ExceptionInformation[0]) {
        case 0:
            lpszVerb = "Reading from";
            break;
        case 1:
            lpszVerb = "Writing to";
            break;
        case 8:
            lpszVerb = "DEP violation at";
            break;
        default:
            lpszVerb = "Accessing";
            break;
        }

        writeLog(" %s location %p", lpszVerb, (PVOID)pExceptionRecord->ExceptionInformation[1]);
    }

    writeLog(".\n\n");
}

void dumpModules(HANDLE hProcess)
{
    HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetProcessId(hProcess));
    if (hModuleSnap == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD MachineType;
#ifdef _WIN64
    BOOL bWow64 = FALSE;
    IsWow64Process(hProcess, &bWow64);
    if (bWow64) {
        MachineType = IMAGE_FILE_MACHINE_I386;
    } else {
#else
    {
#endif
#ifndef _WIN64
        MachineType = IMAGE_FILE_MACHINE_I386;
#else
        MachineType = IMAGE_FILE_MACHINE_AMD64;
#endif
    }

    MODULEENTRY32 me32;
    me32.dwSize = sizeof me32;
    if (Module32First(hModuleSnap, &me32)) {
        do {
            if (MachineType == IMAGE_FILE_MACHINE_I386) {
                writeLog(
                    "%08lX-%08lX ",
                    (DWORD)(DWORD64)me32.modBaseAddr,
                    (DWORD)(DWORD64)me32.modBaseAddr + me32.modBaseSize
                );
            } else {
                writeLog(
                    "%016I64X-%016I64X ",
                    (DWORD64)me32.modBaseAddr,
                    (DWORD64)me32.modBaseAddr + me32.modBaseSize
                );
            }

            char *ptr = nullptr;
            long len = WideCharToMultiByte(CP_ACP, 0, me32.szExePath, -1, NULL, 0, NULL, NULL);
            WideCharToMultiByte(CP_ACP, 0, me32.szExePath, -1, ptr, len + 1, NULL, NULL);

            const char *szBaseName = getBaseName(ptr);

            DWORD dwVInfo[4];
            if (getModuleVersionInfo(ptr, dwVInfo)) {
                writeLog("%-12s\t%lu.%lu.%lu.%lu\n", szBaseName, dwVInfo[0], dwVInfo[1], dwVInfo[2],
                        dwVInfo[3]);
            } else {
                writeLog("%s\n", szBaseName);
            }
        } while (Module32Next(hModuleSnap, &me32));
        writeLog("\n");
    }

    CloseHandle(hModuleSnap);
}

#endif // FORMATOUTPUT_H
