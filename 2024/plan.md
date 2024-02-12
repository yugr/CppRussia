# Plan of the talk

* Overview of dynamic libraries
* Windows/Linux differences
* Performance hints

# Overview of DLLs

* Library is a block of reusable code which can be linked to multiple executables
* Depending on when we link the library we can have
  * static libraries (linked when executable is linked, become part of executable)
    * .a, .lib
  * dynamic libraries (linked at runtime)
    * .so, .dylib, .dll
* All platform supports both library flavors (Windows, Linux, macOS)
  * We'll focus on Windows/Linux
  * macOS is usually similar to Linux

# Pros/cons of DLLs

* Dynamic linking has several benefits:
  * faster/easier library updates (important for fast bugfixes)
    * dependents do not need to be updated on library update
  * memory/disk savings (library code can be shared by multiple executables)
  * support for more complex usage scenarios (more on this below)
* But also has disadvantages
  * additional startup overheads (to load library and locate symbols)
  * DLL is a boundary for optimizations
  * more fragile in use (DLL hell)

# Link-time and run-time linking of DLLs

* In most cases DLLs are linked into application the same way as static libs:
  by specifying library name to the linker:
```
# Linux
gcc prog.c -lgmp

# Windows
link.exe prog.obj libfoo.lib
```
* In that case DLL will be loaded at program startup
* But DLLs can also be loaded dynamically at runtime via special APIs
  * `LoadLibrary` on Windows, `dlopen` on Linux/macOS
  * addresses of DLL functions can then be obtained at runtime via `GetProcAddress`/`dlsym` APIs and used
* Runtime loading enables more complex usage scenarios:
  * load DLL variant which is optimized for particular processor (e.g. particular AVX support)
  * load user-provided plugins to extend program functionality
  * load DLL only when it is needed (delay loading, more on this below)

# How DLLs work, in general

* DLL is an executable module
  * Usually of same format as normal executables (PE on Windows, ELF on Linux, Mach-O on macOS)
* Keeps track of its exported symbols in special table
* Executable also keeps track of its imported symbols
* Each function, imported by executable, has a slot in special dispatch table (GOT on Linux, IAT on Windows)
* Calls to imported functions are done through this table:
```
# Windows
call qword ptr [__imp_foo]

# Linux
jmp  *foo@GOTPCREL(%rip)
```
  * Alternatively could use direct calls and patch code at runtime to insert correct address:
```
call qword ptr foo
```
  * But that would require a lot of code patching at startup to update addresses in `call` instructions
    * Long time to update
    * Bad for I-cache
    * On some platforms direct calls are limited in range (e.g. 4G) so would limit possible DLL load addresses
  * So this approach is not used by any platform
* Dispatch table is initialized by loader/runtime linker (actually details vary here)
  * This process is called "symbol binding"
  * Process of matching exported and imported symbols is called "symbol resolution"

# Windows specifics

* Windows executables and DLLs follow PE binary format
* Executable consists of a header and several sections
  * Section is a piece of code/data with similar properties
  * E.g. code section, read-only data section, etc.
* Imported symbols are stored in special `.idata` section in executable
  * Contains the Import Directory Table
  * Each entry in the table holds info about single imported DLL and symbols (functions or globals) which come from it
    * Symbols in import table are bound to particular DLL!

# Linux specifics

* Very similar to Windows case
* `.dynamic` and `.dynsym` sections on Linux instead of `.idata`
* Imported symbols are not bound to particular DLL at binary level
  * Symbols searched globally across all loaded DLLs at runtime

# Windows/Linux differences: relocation

* Loader may load DLL code to arbitrary free position in address space
  * Position is usually random due to ASLR mechanism
* If code uses absolute addresses internally as e.g. in
```
# Call to internal DLL function on Windows
call   523e11060 <puts>
```
  it will need to be patched (relocated) to match DLL load address
* Windows and Linux make different choices
* Linux shlibs are linked in position-independent mode (no explicit addresses in code, `-fPIC` required)
  * So code does not need to be modified when loaded
  * Globals which contain intra-library references still needs to be updated
* Windows code is linked in absolute mode and is actually relocated at load time
  * Loader in modern Windows uses same load address for DLL in all processes so overhead is paid only on first DLL load
  * Wasn't always the case (more on this below)

# Windows/Linux differences: symbol search

* Process of matching symbols exported by shlibs and imported by executables or other shlibs is called "symbol resolution"
* Windows and Linux use different algorithm for symbol resolution
* On Windows each symbol is bound to fixed library at link time so loader knows exactly in which library to look for symbol
* On Linux symbols are searched in all loaded libraries in order and the first library which has it takes precedence over others
  * This allows so called runtime interposition technique when we forcedly insert a library which intercepts some symbol
  * Often used by diagnostic or hardening tools e.g. memory debuggers like [efence](https://linux.die.net/man/3/efence)
  * Usually interposition is performed via `LD_PRELOAD` environment variable:
```
$ cat prog.c
int main() { printf("%d\n", 1); }
$ cat lib.c
int printf(const char *fmt, ...) { puts("Hello from printf inteceptor\n"); }
$ LD_PRELOAD=./lib.so ./prog
Hello from printf interceptor
```

# Windows/Linux differences: symbol binding

* Process of setting function address, found during symbol resolution, in dispatch table is called "symbol binding"
* On Windows all symbols are resolved and bound at program startup
* On Linux function symbol binding is deferred until function is called the first time (lazy binding)
  * Speeds up startup time at the cost of one extra jump

# Windows/Linux differences: other

* Symbol visibility
  * On Windows symbols are not exported from DLL by default (need to annotate with `__declspec(dllexport)` markers)
  * on Linux it's the opposite
* Import libs
  * On Linux we link against shlib directly:
```
$ ls
libA.so tmp.c
$ gcc tmp.c -L. -lA
```
  * On Windows we link against a small static library (called import library) which is generated by linker together with DLL
  * This library contains information about DLL's interface (its exported symbols)

# DLL overheads

* Common sources of overheads when using DLLs
  * Load-time overheads:
    * Relocation processing when loading the library
    * Constructors of global variables
  * Symbol resolution overheads:
    * Search imported symbols in library's export tables
  * Run-time overheads:
    * Indirect function calls

# Speedup load time: prelinking

* Library is loaded to random location each time
  * => all absolute addresses in code need to be updated (relocated)
* Example
```
int i;
int *p = &i;
```
* We can save load time if we pre-relocate DLL in advance
* Windows solution - preferred load address (`/BASE`)
  * Developer selects unlikely address when compiling the program
  * If this address is unoccupied by other libraries at runtime no relocation is needed
* Linux solution - Prelink tool
  * Scanned all executables and DLLs installed in system
  * Devised preferred load address for each executable to avoid conflicts with other libraries
  * Recorded necessary relocations inside the library to speed up load time
  * Also did other optimizations (static symbol resolution) which are irrelevant here
* No longer relevant due to modern security guildelines:
  * ASLR demands that DLLs are loaded to random locations on every load
  * Prelink support dropped in Glibc

# Speedup load time: lazy loading

* Often some library is needed only is specific and rare user scenarios
  * So it does not make sense to load it on startup
* Some platforms provide a delay loading feature - library is loaded when one of its functions is called
  * which can be long after startup
* This is natively supported in Windows (see `/DELAYLOAD` linker flag) and macOS (`-Wl,-lazy-l`)
* On Linux you can use the Implib.so tool
  * There is also `-Wl,-z,lazy` flag, inherited from Solaris, but it's ignored on Linux

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

# Speedup runtime: avoid PLT stubs

* Linux-only
* By default each call to an external function goes through a special stub function (PLT stub, PLT trampoline)
  * On first call this function locates the address of external function that needs to be called and stores it in dispatch table (GOT)
  * On other calls it just loads the already computed address from dispatch table
```
0000000000001030 <foo@plt>:                                         # PLT stub
    1030:       ff 25 e2 2f 00 00       jmpq   *0x2fe2(%rip)        # Load from dispatch table
    1036:       68 00 00 00 00          pushq  $0x0
    103b:       e9 e0 ff ff ff          jmpq   1020 <.plt>

0000000000001120 <bar>:
    1120:       31 c0                   xor    %eax,%eax
    1122:       e9 09 ff ff ff          jmpq   1030 <foo@plt>
```
* The main purpose of PLT stub is to delay search of external function address until the first call
  * At the cost of stub call overhead on every call
* We can get rid of PLT stubs completely via `-fno-plt` compiler flag
```
0000000000001110 <bar>:
    1110:       31 c0                   xor    %eax,%eax
    1112:       ff 25 d8 2e 00 00       jmpq   *0x2ed8(%rip)        # Load from dispatch table directly
```
  * All functions will now be resolved at program startup time
  * But on the other hand all function calls will no longer have the jump-through-PLT overhead
  * Also successive calls to the same function will reuse the function address loaded from GOT the first time

# Speedup runtime: limit exported symbols (1)

* This is a Linux-only issue
  * Due to some bad historical design choices
* Example:
```
void foo() {}
void bar() { foo(); }
```
  compiles to
```
bar:
  jmp     foo@PLT
```
  Inlining didn't work because `foo` can theoretically be overridden (interposed) at runtime by function in another library.
  Also call to `foo` goes through PLT even though symbol resolution is trivial in this case.
* By default GCC considers that all functions may be interposed at runtime
* This causes performance overheads:
  * Calls to functions have to go through PLT stub and dispatch table (which is much slower than direct calls)
  * Interprocedural optimizations are severely limited (e.g. function inlining, function cloning)
* Solution is to explicitly mark functions which are meant to be exported from library and treat the rest as non-exported ("hidden"):
  * compile with `-fvisibility=hidden`
  * annotate exported functions with `__attribute__((visibility("default")))`
* In above example when using annotations:
```
void foo() {}
__attribute__((visibility("default"))) void bar() { foo(); }
```
  `foo` is now successfully inlined under `-fvisibility=hidden`:
```
bar:
  ret
```

# Speedup runtime: limit exported symbols (2)

* How to identify which symbols need to be marked as exported?
* In most cases these are the symbols which are declared in library's public header files
* In case you have a large monorepo with many libs it may be tedious to analyze them by hand
* This approach could be automated via ShlibVisibilityChecker tool:
```
$ dpkg -L libgmp10 | grep '\.so'
/usr/lib/x86_64-linux-gnu/libgmp.so.10

$ dpkg -L libgmp-dev | grep '\.h'
/usr/include/x86_64-linux-gnu/gmp.h

# Collect public symbols in headers and in dynamic library
$ bin/read_header_api --only-args /usr/include/x86_64-linux-gnu/gmp.h > api.txt
$ ./read_binary_api --permissive /usr/lib/x86_64-linux-gnu/libgmp.so.10.4.1 > abi.txt

# Compare both
$ diff api.txt abi.txt | wc -l
323

# We can see that libgmp exports a lot of unnecessary functions, a shame on them!
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

# TODO

* Performance numbers
* DLL interface
* DLL hell, sonames, symver
* Conclusions
* Recommended readings

# Links

General overview:
  * https://blog.aaronballman.com/2011/10/how-dll-imports-work/
  * [Shared Libraries in Windows and Linux - Ofek Shilon - C++ on Sea 2023](https://www.youtube.com/watch?v=6TrJc06IekE)
  * [Dynamically Loaded Libraries Outside the Standard - Zhihao Yuan - CppCon 2021](https://www.youtube.com/watch?v=-dxCaM4GOqs)
  * [CppCon 2017: James McNellis “Everything You Ever Wanted to Know about DLLs”](https://www.youtube.com/watch?v=JPQWQfDhICA)
  * https://dennisbabkin.com/blog/?t=intricacies-of-microsoft-compilers-the-case-of-the-curious-__imp_

What happens without dllimport?
  * https://repnz.github.io/posts/reversing-windows-libraries/#using-the-import-table-to-load-dlls
  * https://learn.microsoft.com/en-us/archive/msdn-magazine/2002/february/inside-windows-win32-portable-executable-file-format-in-detail
  * https://devblogs.microsoft.com/oldnewthing/20060726-00/?p=30363
  * https://devblogs.microsoft.com/oldnewthing/20060721-06/?p=30433
  * https://devblogs.microsoft.com/oldnewthing/20060724-00/?p=30403

PE anatomy:
  * https://learn.microsoft.com/en-us/archive/msdn-magazine/2002/march/inside-windows-an-in-depth-look-into-the-win32-portable-executable-file-format-part-2
  * https://learn.microsoft.com/en-us/archive/msdn-magazine/2002/february/inside-windows-win32-portable-executable-file-format-in-detail
  * https://bytepointer.com/resources/pe_luevelsmeyer.htm
  * https://0xrick.github.io/win-internals/pe6/
  * https://www.codeproject.com/Articles/1253835/The-Structure-of-import-Library-File-lib
  * IAT: https://devblogs.microsoft.com/oldnewthing/20221006-07/?p=107257

Load-time relocation/ASLR:
  * https://stackoverflow.com/questions/33443618/relocation-of-pe-dlls-load-time-or-like-elf
  * https://devblogs.microsoft.com/oldnewthing/20170120-00/?p=95225
  * https://devblogs.microsoft.com/oldnewthing/20160413-00/?p=93301
  * https://www.mandiant.com/resources/blog/six-facts-about-address-space-layout-randomization-on-windows

Windows DLL optimizations:
  * hinting:
    * https://devblogs.microsoft.com/oldnewthing/20100317-00/?p=14573
  * binding:
    * https://devblogs.microsoft.com/oldnewthing/20231129-00/?p=109077
    * https://devblogs.microsoft.com/oldnewthing/20100318-00/?p=14563

Diffs:
  * no lazy binding in Windows DLLs (=> no runtime interposition)
    * all symbols resolved when DLL loaded at startup
    * no PLT analog
  * no PIC => code relocations on Windows (esp. Win32)
  * GOT on Linux =~ IAT on Windows
  * different default visibility => many more indirect GOT references on Linux by default
  * on Windows symbol is bound to particular library at link time

CMake support for DLLs:
  * https://www.youtube.com/watch?v=m0DwB4OvDXk

Spurious trampoline:
  * https://stackoverflow.com/questions/77741370/spurious-trampoline-when-calling-function-from-dll

macOS:
  * https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/000-Introduction/Introduction.html

Performance:
  * -fno-semantic-interposition:
    * https://developers.redhat.com/blog/2020/06/25/red-hat-enterprise-linux-8-2-brings-faster-python-3-8-run-speeds
    * https://www.facebook.com/dan.colascione/posts/10107358290728348
    * https://lore.kernel.org/lkml/20210501235549.vugtjeb7dmd5xell@google.com/
  * -Bsymbolic:
    * https://bugzilla.redhat.com/show_bug.cgi?id=1956484
    * https://bugs.archlinux.org/task/70697
  * Prelink:
    * https://people.redhat.com/jakub/prelink.pdf
  * -fno-plt:
    * https://gcc.gnu.org/legacy-ml/gcc-patches/2015-05/msg00001.html
    * https://github.com/rust-lang/rust/pull/54592
    * https://cyberleninka.ru/article/n/optimizatsiya-dinamicheskoy-zagruzki-bibliotek-na-arhitekture-arm/viewer
    * https://gcc.gnu.org/legacy-ml/gcc-patches/2015-05/msg00225.html

https://www.codeproject.com/Articles/146652/Creating-Import-Library-from-a-DLL-with-Header-Fil

DLL memory savings:
  * https://zvrba.net/articles/solib-memory-savings.html

`LD_BIND_NOW` in distros:
  * RHEL: https://fedoraproject.org/wiki/Security_Features_Matrix
  * Ubuntu: https://wiki.ubuntu.com/ToolChain/CompilerFlags
