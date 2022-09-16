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
    // pExceptionInfo �Ľṹ����Ϣ��չ�������£�
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

    // ��ӡϵͳʱ��
    SYSTEMTIME SystemTime;
    GetLocalTime(&SystemTime);
    char szDateStr[128];
    LCID Locale = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
    GetDateFormatA(Locale, 0, &SystemTime, "dddd',' MMMM d',' yyyy", szDateStr, _countof(szDateStr));
    char szTimeStr[128];
    GetTimeFormatA(Locale, 0, &SystemTime, "HH':'mm':'ss", szTimeStr, _countof(szTimeStr));
    writeLog("Error occurred on %s at %s.\n\n", szDateStr, szTimeStr);

    // ��ȡ��ǰ���̵�һ�����
    HANDLE hProcess = GetCurrentProcess();

    SetSymOptions(FALSE);

    PEXCEPTION_RECORD pExceptionRecord = pExceptionInfo->ExceptionRecord;
    if(InitializeSym(hProcess, TRUE))
    {
        // ��ӡ������ַ��Ϣ
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

        // ��������ջ��Ϣ
        dumpStack(hProcess, GetCurrentThread(), pContext);

        if (!SymCleanup(hProcess)) {
            assert(0);
        }
    }

    // ��ӡ����ģ�����Ϣ
    dumpModules(hProcess);
    writeLog("\n");
}

// ������δ��������쳣ʱ�����ȵ��øú���
static LONG WINAPI MyUnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo)
{
    static LONG nLockNum = 0;
    // ʵ������ԭ���ԼӼ�
    if(1 == InterlockedIncrement(&nLockNum))
    {
        // SetErrorMode() �������� Windows �Ƿ���ָ�����͵����ش����ʹ����Ӧ�ó������������ǡ�
        // SEM_FAILCRITICALERRORS: ϵͳ����ʾ�ؼ���������Ϣ���෴��ϵͳ���ʹ�������ý��̡�
        // SEM_NOGPFAULTERRORBOX: ϵͳ����ʾWindows���󱨸�Ի���
        // SEM_NOALIGNMENTFAULTEXCEPT: ϵͳ���Զ��޸����ϡ��˹���ֻ֧�ֲ��ִ������ܹ���
        // SEM_NOOPENFILEERRORBOX�����޷��ҵ��ļ�ʱ����������Ի����෴�����󷵻ظ����ý��̡�
        UINT oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

        // ��������ʱд�����̵� .RPT �ļ�
        if(nullptr == g_handleCrashFile)
        {
            g_handleCrashFile = CreateFileA(g_sCrashFileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, 0, 0);
        }

        if(nullptr != g_handleCrashFile)
        {
            // ��һ���ļ��������µĶ�ȡλ��
            SetFilePointer(g_handleCrashFile, 0, 0, FILE_END);

            // ��Ҫͨ���ú����������쳣������Ϣ������ pExceptionInfo ���쳣��Ϣ�ṹ��ָ�롣
            GenerateExceptionReport(pExceptionInfo);

            // ˢ�������������Ҳ����Ϊ��ʵʱд���̣�
            FlushFileBuffers(g_handleCrashFile);
        }

        // �Ѵ���ģʽ���û�ȥ
        SetErrorMode(oldErrorMode);
    }

    // ʵ������ԭ���ԼӼ�
    InterlockedDecrement(&nLockNum);

    // ��ǰҲ���ù��쳣����������ô���������ǵ��쳣��Ϣ֮�������ٵ�������
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
        // �����Լ����쳣��������������ǰ���õ��쳣������
        g_prevUnhandledExceptionFilter = SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
        g_bHandlerSet = TRUE;
    }
}
