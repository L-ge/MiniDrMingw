# MiniDrMingw


# 站在巨人的肩膀上

> [Dr. Mingw](https://github.com/jrfonseca/drmingw)


# 概述
- 剖析 Dr. Mingw 的源代码，抽取最核心的代码，封装成一个动态库，名为 MiniDrMingw.dll。
- MiniDrMingw.dll 是面向过程的，容易看懂，没有什么跳转。
- 可以加入各种个性化需求（如在崩溃时，记录崩溃时的 CPU 占用率、内存占用情况等）。
- 大概使用方式就是：
    ```
    1、应用程序加上编译参数，生成 .map 文件，里面记录着程序的函数地址等。
    # gcc 编译选项，生成 .map 文件
    QMAKE_LFLAGS += -Wl,-Map=$$PWD'/bin/'$$TARGET'.map'
    
    2、应用程序在 main 函数里面，使用下面语句进行初始化：
    ExcHndlInit((a.applicationDirPath() +"/" + a.applicationName() + ".RPT").toLocal8Bit().data());
    ```
- 在崩溃时，如何定位出崩溃的函数，后面在讲 Demo 的时候再说清楚。


# MiniDrMingw.dll 的主要函数：SetUnhandledExceptionFilter-设置异常捕获函数。
- 当异常没有处理的时候,系统就会调用 SetUnhandledExceptionFilter 所设置异常处理函数.
- 当发生异常时，比如内存访问违例时，CPU 硬件会发现此问题，并产生一个异常（你可以把它理解为中断），然后 CPU 会把代码流程切换到异常处理服务例程。操作系统异常处理服务例程会查看当前进程是否处于调试状态。如果是，则通知调试器发生了异常，如果不是则操作系统会查看当前线程是否安装了的异常帧链(FS[0])，如果安装了 SEH（try.... catch....），则调用 SEH，并根据返回结果决定是否全局展开或局部展开。如果异常链中所有的 SEH 都没有处理此异常，而且此进程还处于调试状态，则操作系统会再次通知调试器发生异常（二次异常）。如果还没人处理，则调用操作系统的默认异常处理代码 UnhandledExceptionHandler，不过操作系统允许你 Hook 这个函数，就是通过 SetUnhandledExceptionFilter 函数来设置。大部分异常通过此种方法都能捕获，不过栈溢出、覆盖的有可能捕获不到。
- 该函数说明如下：
	```
	LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter( _In_LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
	- 参数：lpTopLevelExceptionFilter，函数指针。当异常发生时，且程序不处于调试模式（在 vs 或者别的调试器里运行）则首先调用该函数。
	- 返回值：返回以前设置的回调函数。
	``` 


# MiniDrMingw.dll 的函数调用关系
```
void ExcHndlInit(const char *pCrashFileName)
	↓
SetUnhandledExceptionFilter(MyUnhandledExceptionFilter)
	↓
MyUnhandledExceptionFilter
	↓
GenerateExceptionReport
	↓
dumpException(hProcess, pExceptionRecord)、dumpStack(hProcess, GetCurrentThread(), pContext)、dumpModules(hProcess);
```
- 详细说明可以看下面源代码的注释。


# MiniDrMingw.dll 的源代码
- 虽然用 QtCreator 开发，但是源代码基本没有用到 Qt 的东西。
- 以动态库的形式。
- 分为以前几个文件：
	- main.cpp：导出函数、主要函数。
	- dllexport.h：导出接口。
	- log.h：写文件。
	- formatoutput.h：将崩溃信息格式为可读信息。
	- util.h：一些辅助类的函数。
- 工程文件 DrMingwDemo.pro 部分源代码如下：
    ```
	...
    LIBS += -lpsapi -lversion -ldbghelp
	...
	```

# 测试工具 DrMingwDemo
- 工程文件 DrMingwDemo.pro 部分源代码如下：
    ```
	...
    LIBS += -L$$PWD/lib -lMiniDrMingw

    # gcc 编译选项，生成 .map 文件
    QMAKE_LFLAGS += -Wl,-Map=$$PWD'/bin/'$$TARGET'.map'
	...
    ```

# 崩溃分析
- DrMingwDemo.exe 编译后，会在工程的 bin 目录下生成 DrMingwDemo.map。
- 双击运行 DrMingwDemo.exe，然后闪退，此时在程序的运行目录，产生了 DrMingwDemo.RPT。
- 打开 DrMingwDemo.RPT 文件，内容大概如下：
    ```
    -------------------

    Error occurred on Friday, September 9, 2022 at 20:54:00.
    
    DrMingwDemo.exe caused an Access Violation at location 004028A0 in module DrMingwDemo.exe Writing to location 00000000.
    
    Registers:
    eax=004042f4 ebx=0065fe78 ecx=0065fe78 edx=00000001 esi=0108b4a0 edi=0108b468
    eip=004028a0 esp=02c1fefc ebp=02c1ff28 iopl=0         nv up ei pl nz na po nc
    cs=0023  ss=002b  ds=002b  es=002b  fs=0053  gs=002b             efl=00010206
    
    AddrPC   Params
    004028A0 7606689C 76066CFF 0065FE78  DrMingwDemo.exe!0x28a0
    76066CFF 02C1FF80 769DFA29 0108C610  msvcrt.dll!_beginthreadex+0xcf
    76066DC1 0108C610 769DFA10 02C1FFDC  msvcrt.dll!_endthreadex+0x91
    769DFA29 0108C610 4F76EDC1 00000000  KERNEL32.DLL!BaseThreadInitThunk+0x19
    771A7A9E FFFFFFFF 771C8BAC 00000000  ntdll.dll!RtlGetAppContainerNamedObjectPath+0x11e
    771A7A6E 76066D60 0108C610 00000000  ntdll.dll!RtlGetAppContainerNamedObjectPath+0xee
    
    00400000-00451000 
    ```
- 由上面可以可以看出是 004028A0 这个地址出现了崩溃。此时，我们打开 DrMingwDemo.map 文件，找到离该地址最近的函数（一般函数地址会小于等于崩溃地址）。
- DrMingwDemo.map 的部分内容如下：
    ```
    ...
     *(SORT(.text$*))
     *fill*         0x00402818        0x8 
     .text$_ZN10QByteArrayD1Ev
                    0x00402820       0x40 release/main.o
                    0x00402820                QByteArray::~QByteArray()
     .text$_ZN7QStringD1Ev
                    0x00402860       0x40 release/main.o
                    0x00402860                QString::~QString()
     .text$_ZN9TestClass3runEv
                    0x004028a0       0x10 release/main.o
                    0x004028a0                TestClass::run()
     .text$_ZN9TestClassD0Ev
                    0x004028b0       0x20 release/main.o
                    0x004028b0                TestClass::~TestClass()
     .text$_ZN9TestClassD1Ev
                    0x004028d0       0x10 release/main.o
                    0x004028d0                TestClass::~TestClass()
     .text$_ZplRK7QStringPKc
                    0x004028e0       0xc0 release/main.o
                    0x004028e0                operator+(QString const&, char const*)
    ...
    ```
- 因此，可以看出是 TestClass::run() 函数发生了崩溃。
- 注意，该方法只能定位出哪个函数出现了崩溃，却难以定位出是哪行导致的崩溃，就算在项目工程中加入了 Debug 信息也如此。
