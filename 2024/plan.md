# Overview of DLLs

* Library is a block of reusable code which can be linked to multiple executables
* Depending on when we link the library we can have
  * static libraries (linked when executable is linked, become part of executable)
    * .a, .lib
  * dynamic libraries (linked at runtime)
    * .so, .dylib, .dll
* All platform supports both flavors (Windows, Linux, OS X)
  * We'll focus on Windows/Linux

# Pros/cons of DLLs

* Linking at runtime gives us several benefits:
  * memory/disk savings (library code can be shared by multiple executables)
  * faster/easier library updates (important for security fixes)
  * ASLR support
  * more complex usage scenarios
    * e.g. plugins or load library implementation specifically for processor
* But also has disadvantages
  * additional startup overheads to load library and locate symbols
  * DLL boundary is blocker for optimization

# How DLLs work, in general

* DLL is a standalone executable module
* Keeps track of its exported symbols in special section
  * Symmetrically, executable keeps track of its imported symbols
* Each imported symbols has a slot in special address table (GOT/IAT)
  * Table is initialized by loader/runtime linker (actually details vary here)
    * This process is called "symbol resolution"
  * Direct calls would be bad for cache, take long time to update at startup and may be limited in range
* Common sources of overheads:
  * DLL load overhead (memory mapping, relocation if needed + initialization/constructors)
  * call via pointer at runtime:
```
call qword ptr [__imp_foo]
jmp  *foo@GOTPCREL(%rip)
```

# Windows specifics

* .idata/Import Directory Table on Windows
* Symbols in import table are bound to particular DLL

# Linux specifics

* .dynamic on Linux
* Symbols searched globally across all loaded DLLs

# Windows/Linux differences

* Loader loads DLL code to arbitrary position in address space
  * Windows and Linux make different choices
  * Linux shlibs are linked in position-independent mode (no explicit addresses in code)
    * So code does not need to be modified when loaded
  * Windows code may contain absolute addresses so needs to be "relocated"
    * Code is patched at load time
    * Loader in modern Windows uses same load address for DLL in all processes so overhead is paid only on first DLL load
      * Wasn't always the case
* Symbol search algorithm (+ runtime interposition, `LD_PRELOAD`)
  * Windows and Linux take different approach to symbol resolution across libraries
  * On Windows each symbol is bound to library at link time
  * On Linux symbols are searched in all loaded libraries in order
    * That allows tricks with runtime interposition e.g. `LD_PRELOAD`
* Symbol binding
  * On Windows symbols are located at startup
  * On Windows function symbol resolution is deferred until function is called the first time (lazy binding)
* Symbol visibility
  * On Windows symbols are not exported from DLL by default
  * on Linux it's vice verse
* Import libs
  * On Linux we link against shlib directly
  * On Windows we link against a small static library (called import library) which is produced together with DLL
  * Mostly a "cosmetic" change

# Speedups: lazy binding

* Linux-only
* By default loader defers resolution of function addresses until they are called first time
* This improves startup time (because costly symbol resolution is not done at startup)
* But slightly degrades overall runtime because each function call has to go through a PLT stub
* Lazy binding can be disabled with `-fno-plt` compiler flag

# Speedups: prelinking

* Library is loaded to random location each time (small lie)
* => all absolute addresses in code need to be updated (relocated)
* Example
```
int i;
int *p = &i;
```
* We can save load time if we relocate DLL in advance
* Windows solution - preferred load address (`/BASE`)
* Linux solution - Prelink tool
  * Scanned all executables and DLLs installed in system
  * Devised preferred load address for each executable to avoid conflicts with other libraries
  * Recorded necessary relocations inside the library to speed up load time
  * Also did other optimizations which are irrelevant here
* No longer relevant due to modern security guildelines:
  * ASLR demands that DLLs are loaded to random locations on every load
  * Prelink support even dropped in Glibc

# Speedups: visibility (1)

* Need to annotate exported/imported symbols
  * Without annotations, on Linux all symbols will be considered exported and interposable
  * So to preserve runtime interposition, compiler will disable a lot of optimizations, most notable inlining
  * In addition, the more symbols we export, the larger are the symbol tables and the slower symbol search is at startup
* Example:
  * this code
```
void foo() {}
void bar() { foo(); }
```
  compiles to
```
bar:
        jmp     foo@PLT
```
  but if we do not export `foo` (by adding `__attribute__((visibility("hidden")))`), inlining does happen:
```
bar:
        ret
```
* Most canonical solution is to switch default to Windows-like mode (when nothing is exported by default) via `-fvisibility=hidden`
  * Exported symbols need to be annotated via `__attribute__((visibility("default")))`
  * Alternatively can use `-fno-semantic-interposition` flag (enabled by default on Clang, enables all optimizations even on interposable functions)
* On Windows it's also important to mark imported symbols via `__declspec(dllimport)` to avoid overhead

# Speedups: visibility (2)

* How to identify which symbols need to be exported?
* In most cases these are the symbols which are declared in library's public header files
* In case you have a large monorepo with many libs it may be tedious to go over all libs
* So this approach could be automated via ShlibVisibilityChecker tool:
```
$ dpkg -L libgmp10 | grep '\.so'
/usr/lib/x86_64-linux-gnu/libgmp.so.10.4.1
/usr/lib/x86_64-linux-gnu/libgmp.so.10

$ dpkg -L libgmp-dev | grep '\.h'
/usr/include/gmpxx.h
/usr/include/x86_64-linux-gnu/gmp.h

$ bin/read_header_api --only-args /usr/include/x86_64-linux-gnu/gmp.h > api.txt
$ ./read_binary_api --permissive /usr/lib/x86_64-linux-gnu/libgmp.so.10.4.1 > abi.txt

$ diff api.txt abi.txt | wc -l
323

$ diff api.txt abi.txt | head
0a1,2
> __gmp_0
> __gmp_allocate_func
1a4,10
> __gmp_asprintf_final
> __gmp_asprintf_funs
> __gmp_asprintf_memory
> __gmp_asprintf_reps
> __gmp_assert_fail
> __gmp_assert_header
```
* Limitations: only supports C libraries atm

# Speedups: lazy loading

* Often some library is needed only is specific and rare user scenarios
  * So it does not make sense to load it on startup
* Some platforms provide a delay loading feature - library is loaded when one of its functions is called
  * which can be long after startup
* This is natively supported in Windows (see `/DELAYLOAD` linker flag) and macOS (`-Wl,-lazy-l`)
* On Linux you can use Implib.so tool
  * There is `-Wl,-z,lazy` flag, inherited from Solaris, but it does nothing

# Implib.so

* In short, Implib.so takes a shlib as input and generates a small library with stub functions
* You then link your application against the stub library, not the shlib
* At runtime, the first call to any of stub functions will cause library to be loaded
* Example usage:
```
$ implib-gen.py libxyz.so
$ gcc myprog.c libxyz.so.tramp.S libxyz.so.init.c -ldl
```
* There are more usage scenarios, visit https://github.com/yugr/Implib.so for more info
* Supports a lot of target platforms (x86, x86\_64, ARM, AArch64, MIPS, etc.)

# TODO

* `LoadLibrary`/`dlopen`
* DLL hell, sonames, symver

# Links

https://www.codeproject.com/Articles/1253835/The-Structure-of-import-Library-File-lib
https://www.codeproject.com/Articles/146652/Creating-Import-Library-from-a-DLL-with-Header-Fil

What happens without dllimport?
  * https://repnz.github.io/posts/reversing-windows-libraries/#using-the-import-table-to-load-dlls
  * https://learn.microsoft.com/en-us/archive/msdn-magazine/2002/february/inside-windows-win32-portable-executable-file-format-in-detail
  * https://devblogs.microsoft.com/oldnewthing/20060726-00/?p=30363
  * https://devblogs.microsoft.com/oldnewthing/20060721-06/?p=30433
  * https://devblogs.microsoft.com/oldnewthing/20060724-00/?p=30403

General overview:
  * https://blog.aaronballman.com/2011/10/how-dll-imports-work/
  * [Shared Libraries in Windows and Linux - Ofek Shilon - C++ on Sea 2023](https://www.youtube.com/watch?v=6TrJc06IekE)
  * [Dynamically Loaded Libraries Outside the Standard - Zhihao Yuan - CppCon 2021](https://www.youtube.com/watch?v=-dxCaM4GOqs)
  * [CppCon 2017: James McNellis “Everything You Ever Wanted to Know about DLLs”](https://www.youtube.com/watch?v=JPQWQfDhICA)
  * https://dennisbabkin.com/blog/?t=intricacies-of-microsoft-compilers-the-case-of-the-curious-__imp_

PE anatomy:
  * https://learn.microsoft.com/en-us/archive/msdn-magazine/2002/march/inside-windows-an-in-depth-look-into-the-win32-portable-executable-file-format-part-2
  * https://learn.microsoft.com/en-us/archive/msdn-magazine/2002/february/inside-windows-win32-portable-executable-file-format-in-detail
  * https://bytepointer.com/resources/pe_luevelsmeyer.htm
  * https://0xrick.github.io/win-internals/pe6/

Load-time relocation/ASLR:
  * https://stackoverflow.com/questions/33443618/relocation-of-pe-dlls-load-time-or-like-elf
  * https://devblogs.microsoft.com/oldnewthing/20170120-00/?p=95225
  * https://devblogs.microsoft.com/oldnewthing/20160413-00/?p=93301
  * https://www.mandiant.com/resources/blog/six-facts-about-address-space-layout-randomization-on-windows

DLL hinting:
  * https://devblogs.microsoft.com/oldnewthing/20100317-00/?p=14573

DLL binding:
  * https://devblogs.microsoft.com/oldnewthing/20231129-00/?p=109077
  * https://devblogs.microsoft.com/oldnewthing/20231129-00/?p=109077

IAT:
  * https://devblogs.microsoft.com/oldnewthing/20221006-07/?p=107257

Diffs:
  * no lazy binding in Windows DLLs (=> no runtime interposition)
    * all symbols resolved when DLL loaded at startup
    * no PLT analog
  * no PIC => code relocations on Windows (esp. Win32)
  * GOT on Linux =~ IAT on Windows
  * different default visibility => many more indirect GOT references on Linux by default
  * on Windows symbol is bound to particular library at link time

Similiarities:
  * runtime linking via loader
  * visibility concept
  * .dynamic =~ .idata
  * GOT =~ IAT

Spurious trampoline:
  * https://stackoverflow.com/questions/77741370/spurious-trampoline-when-calling-function-from-dll

mac OS:
  * https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/000-Introduction/Introduction.html
