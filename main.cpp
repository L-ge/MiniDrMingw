#include "dllexport.h"
#include <windows.h>
#include <string>
#include <imagehlp.h>
#include <shlobj.h>
#include <assert.h>
#include <psapi.h>
#include <tlhelp32.h>
#include "log.h"
#include "formatoutput.h"

static std::string g_sCrashFileName;
static BOOL g_bHandlerSet = FALSE;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevUnhandledExceptionFilter = nullptr;

DWORD SetSymOptions(BOOL fDebug)
{
    DWORD dwSymOptions = SymGetOptions();

    // We have more control calling UnDecorateSymbolName directly Also, we
    // don't want DbgHelp trying to undemangle MinGW symbols (e.g., from DLL
    // exports) behind our back (as it will just strip the leading underscore.)
    if (0) {
        dwSymOptions |= SYMOPT_UNDNAME;
    } else {
        dwSymOptions &= ~SYMOPT_UNDNAME;
    }

    dwSymOptions |= SYMOPT_LOAD_LINES | SYMOPT_OMAP_FIND_NEAREST;

    if (TRUE) {
        dwSymOptions |= SYMOPT_DEFERRED_LOADS;
    }

    if (fDebug) {
        dwSymOptions |= SYMOPT_DEBUG;
    }

#ifdef _WIN64
    dwSymOptions |= SYMOPT_INCLUDE_32BIT_MODULES;
#endif

    return SymSetOptions(dwSymOptions);
}

BOOL InitializeSym(HANDLE hProcess, BOOL fInvadeProcess)
{
    // Provide default symbol search path
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms680689.aspx
    // http://msdn.microsoft.com/en-gb/library/windows/hardware/ff558829.aspx
    std::string sSymSearchPathBuf;
    const char *szSymSearchPath = nullptr;
    if (getenv("_NT_SYMBOL_PATH") == nullptr && getenv("_NT_ALT_SYMBOL_PATH") == nullptr) {
        char szLocalAppData[MAX_PATH];
        HRESULT hr = SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, szLocalAppData);
        assert(SUCCEEDED(hr));
        if (SUCCEEDED(hr)) {
            // Cache symbols in %APPDATA%\drmingw
            sSymSearchPathBuf += "srv*";
            sSymSearchPathBuf += szLocalAppData;
            sSymSearchPathBuf += "\\drmingw*http://msdl.microsoft.com/download/symbols";
            szSymSearchPath = sSymSearchPathBuf.c_str();
        } else {
            // No cache
            szSymSearchPath = "srv*http://msdl.microsoft.com/download/symbols";
        }
    }

    return SymInitialize(hProcess, szSymSearchPath, fInvadeProcess);
}

void GenerateExceptionReport(PEXCEPTION_POINTERS pExceptionInfo)
{
    // pExceptionInfo 的结构体信息扩展开来如下：
    // typedef struct _EXCEPTION_POINTERS {
    //  DWORD ExceptionCode;
    //  DWORD ExceptionFlags;
    //  struct _EXCEPTION_RECORD *ExceptionRecord;
    //  PVOID ExceptionAddress;
    //  DWORD NumberParameters;
    //  ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
    //
    //  DWORD ContextFlags;
    //  DWORD Dr0;
    //  DWORD Dr1;
    //  DWORD Dr2;
    //  DWORD Dr3;
    //  DWORD Dr6;
    //  DWORD Dr7;
    //  DWORD ControlWord;
    //  DWORD StatusWord;
    //  DWORD TagWord;
    //  DWORD ErrorOffset;
    //  DWORD ErrorSelector;
    //  DWORD DataOffset;
    //  DWORD DataSelector;
    //  BYTE RegisterArea[SIZE_OF_80387_REGISTERS];
    //  DWORD Cr0NpxState;
    //  DWORD SegGs;
    //  DWORD SegFs;
    //  DWORD SegEs;
    //  DWORD SegDs;
    //  DWORD Edi;
    //  DWORD Esi;
    //  DWORD Ebx;
    //  DWORD Edx;
    //  DWORD Ecx;
    //  DWORD Eax;
    //  DWORD Ebp;
    //  DWORD Eip;
    //  DWORD SegCs;
    //  DWORD EFlags;
    //  DWORD Esp;
    //  DWORD SegSs;
    //  BYTE ExtendedRegisters[MAXIMUM_SUPPORTED_EXTENSION];
    //} EXCEPTION_POINTERS,*PEXCEPTION_POINTERS;

    // Start out with a banner
    writeLog("-------------------\n\n");

    // 打印系统时间
    SYSTEMTIME SystemTime;
    GetLocalTime(&SystemTime);
    char szDateStr[128];
    LCID Locale = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
    GetDateFormatA(Locale, 0, &SystemTime, "dddd',' MMMM d',' yyyy", szDateStr, _countof(szDateStr));
    char szTimeStr[128];
    GetTimeFormatA(Locale, 0, &SystemTime, "HH':'mm':'ss", szTimeStr, _countof(szTimeStr));
    writeLog("Error occurred on %s at %s.\n\n", szDateStr, szTimeStr);

    // 获取当前进程的一个句柄
    HANDLE hProcess = GetCurrentProcess();

    SetSymOptions(FALSE);

    PEXCEPTION_RECORD pExceptionRecord = pExceptionInfo->ExceptionRecord;
    if(InitializeSym(hProcess, TRUE))
    {
        // 打印崩溃地址信息
        dumpException(hProcess, pExceptionRecord);

        PCONTEXT pContext = pExceptionInfo->ContextRecord;
        assert(pContext);

        // XXX: In 64-bits WINE we can get context record that don't match the exception record somehow
#ifdef _WIN64
        PVOID ip = (PVOID)pContext->Rip;
#else
        PVOID ip = (PVOID)pContext->Eip;
#endif
        if(pExceptionRecord->ExceptionAddress != ip)
        {
            writeLog("warning: inconsistent exception context record\n");
        }

        // 函数调用栈信息
        dumpStack(hProcess, GetCurrentThread(), pContext);

        if (!SymCleanup(hProcess)) {
            assert(0);
        }
    }

    // 打印调用模块的信息
    dumpModules(hProcess);
    writeLog("\n");
}

// 当出现未被处理的异常时，会先调用该函数
static LONG WINAPI MyUnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo)
{
    static LONG nLockNum = 0;
    // 实现数的原子性加减
    if(1 == InterlockedIncrement(&nLockNum))
    {
        // SetErrorMode() 函数控制 Windows 是否处理指定类型的严重错误或使调用应用程序来处理它们。
        // SEM_FAILCRITICALERRORS: 系统不显示关键错误处理消息框。相反，系统发送错误给调用进程。
        // SEM_NOGPFAULTERRORBOX: 系统不显示Windows错误报告对话框。
        // SEM_NOALIGNMENTFAULTEXCEPT: 系统会自动修复故障。此功能只支持部分处理器架构。
        // SEM_NOOPENFILEERRORBOX：当无法找到文件时不弹出错误对话框。相反，错误返回给调用进程。
        UINT oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

        // 创建崩溃时写到磁盘的 .RPT 文件
        if(nullptr == g_handleCrashFile)
        {
            g_handleCrashFile = CreateFileA(g_sCrashFileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, 0, 0);
        }

        if(nullptr != g_handleCrashFile)
        {
            // 在一个文件中设置新的读取位置
            SetFilePointer(g_handleCrashFile, 0, 0, FILE_END);

            // 主要通过该函数来生成异常报告信息，其中 pExceptionInfo 是异常信息结构体指针。
            GenerateExceptionReport(pExceptionInfo);

            // 刷新输出缓存区（也就是为了实时写磁盘）
            FlushFileBuffers(g_handleCrashFile);
        }

        // 把错误模式设置回去
        SetErrorMode(oldErrorMode);
    }

    // 实现数的原子性加减
    InterlockedDecrement(&nLockNum);

    // 以前也设置过异常捕获函数，那么处理完我们的异常信息之后，我们再调用它。
    if(g_prevUnhandledExceptionFilter)
        return g_prevUnhandledExceptionFilter(pExceptionInfo);
    else
        return EXCEPTION_CONTINUE_SEARCH;
}

void ExcHndlInit(const char *pCrashFileName)
{
    if(nullptr != pCrashFileName)
        g_sCrashFileName = pCrashFileName;
    else
        g_sCrashFileName = "./crash.RPT";

    if(FALSE == g_bHandlerSet)
    {
        // 设置自己的异常捕获函数，返回以前设置的异常捕获函数
        g_prevUnhandledExceptionFilter = SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
        g_bHandlerSet = TRUE;
    }
}
