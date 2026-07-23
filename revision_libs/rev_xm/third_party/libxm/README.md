libxm-windows
=====

Fork of [Artefact2/libxm](https://github.com/Artefact2/libxm), a small XM (Fasttracker II Extended Module) player library. 

Modified to be C89 compliant. Compiles on VC 6.0, VS 2005 and VS 2026. 


<h4>Major differences:</h4>

* Can be used with C++ projects. 
  * No `restrict` used around in `xm.h` (or `xm.h.in`). 

* Windows 98 compatible. Separate archives for VC 6.0 and VS 2005 projects are provided. 
  The VS solution includes a project for a static library, along with the examples as the other VS projects.
  * Dynamic libraries (`.dll`s) are planned for later. Static only for now. 
  * Modify `xm.h` to change build conditions as offered by the `CMakeLists.txt` file

* Polyfills were used for missing functions and operators and macros. 
  * The polyfills used should be as portable as possible. Intended to be C89 compliant. 
  * C99+ features will be used instead when they are available. 

* `NOTICE` and `TRACE` macros defined for internal use in `xm_internal.h` now require the 
  names of functions they were called from (as C string) as their first argument. 

* Libxm's tests are yet to work on Windows. Help needed.


<h4>Main features:</h4>

* Small and hackable: many features can be disabled at compile-time, or are
  optimized out by the compiler if not used. It is easy to bundle libxm in your
  game, demo, intro, etc.

* Fairly portable. Minimal dependencies. No memory allocations. 

* Reasonable accuracy compared to Fasttracker 2. Deviations from FT2 playback, that 
  aren't obviously bugs in FT2, are also possibly libxm bugs. If you have a module that plays 
  incorrectly, please test it in FT2/FT2clone and open an issue at [Artefact2/libxm](https://github.com/Artefact2/libxm)! \
  **AND MAKE SURE IT IS A LIBXM ISSUE**. If it is an issue with libxm-windows, make an issue [here](../../issues?q=is%3Aissue%20state%3Aopen%20label%3Abug). 

* Can load most XM/MOD/S3M files. However, playback accuracy of non-XM is best-effort. 

* Timing functions for synchronising against specific instruments, samples or channels.

* Samples can be loaded and altered at run-time, making it possible to
  use libxm with softsynths or other real-time signal processors.


The name is a bit misleading. This project can be built for other platforms when compiled as C99. And it can be 
be compiled as C89 should the user provide their own polyfills for `stdint.h` and `stdbool.h`. (check `xm.h.in`). \
However, if possible, stick with the original project due to better optimisations and hints.

Ported to C89 and released under the WTFPL license, version 2.


Disclaimer
==========

Libxm comes **without any warranty, to the extent permitted by applicable law**.
In particular,

* Load untrusted modules at your own risk. While precautions are taken in the
  loading/parsing code, bugs are likely and a maliciously crafted module file
  could cause arbitrary code execution, data loss or worse;

* Load modules with `xm_restore_context()` *if and only if* you used
  `xm_dump_context()` yourself, as there are no safety checks at all in the
  loading code. This function is meant for sizecoding, the use case being
  statically embedding *known* modules in games, demos, intros and such.


The same disclaimers of libxm shown above applies to libxm-windows as well. 
Plus, 

* It is not the best port to C89. It was done somewhat lazily, moving all declarations to the top
  of the functions, even where it was not needed. There were better ways available, but I wanted
  to avoid analyzing the code more than I needed to. I needed something that worked first, not
  something that was fast. And something that can work on more compilers than just `msvc`. 

* While it can be compiled with `msvc`, it is at the cost of some possible optimisations being lost. 
  Lack of attributes and hinting to the compiler and what not. 
  
* Since the original project uses `float`s so extensively, it may be slow(er) on older computers.

* This fork is not thoroughly tested. It has been tested with [one .xm file](https://opengameart.org/content/mammoth) so far. It was only tested on 
  `msvc`, and `clang` in VS 2026. I have yet to compare the dumped contexts between Linux and Windows builds. 

Building with CMake
========

* Build the library :

  ~~~
  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -Bbuild -Ssrc
  ~~~
  And then build the created solution in Visual Studio.

* Build a specific example:

  ~~~
  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -Bbuild-FOO -Sexamples/FOO
  ~~~
  Same as before.

* To build a shared library and link dynamically using CMake, use
  `cmake -DBUILD_SHARED_LIBS=ON`.

* To see a list of build options, use `cmake -L` or `cmake-gui`.

* To use libxm-windows in your program, put these lines in the `CMakeLists.txt` of your  project, then `#include <xm.h>`:

  ~~~
  add_subdirectory(/path/to/libxm-windows/src libxm_build)
  target_link_libraries(my_stuff PRIVATE xm)
  ~~~


Building for Windows 98
========

I would recommend building this library and your own projects on VS 2005, due to it being more recent and being better at optimising stuff.

* Extract the relevant .zip files in the `archives` folder to some location. E.g. `C:\`.

* Open the solution or workspace file. It should be `libxm-windows.sln` or `libxm.dsw`

* To see a list of equivalents to the CMake build options, look at the end of `xm.h` for further information on the 
  macros involved. Modify the macros shown around line 48 accordingly as needed. Note, all the examples will be affected. 

* For VS 2005, the process for including the headers and static libraries are same as modern Visual Studio.
  Refer to what Cherno does [here](https://youtu.be/or1dAmUO8k0) at timestamps `10:15` and `14:40`. With, for all configurations,  
  * `path\to\libxm-windows\include` added to 'Additional Include Directories'
  * `path\to\libxm-windows\lib\$(ConfigurationName)\xm.lib` added to 'Additional Dependencies'

* For VC 6.0, Toolbar -> Project -> Settings<sup>(or press Alt+F7)</sup>. Check [here](https://www.steptools.com/docs/help/settings_vc6.html)<sup>[(Ghost Archive)](https://ghostarchive.org/archive/YV8OH)</sup> for reference. With
  * `path\to\libxm-windows\include` added to 'Additional include directories' in the Preprocessor category of the C/C++ tab for All Configurations.
  * `path\to\libxm-windows\lib\Debug\xm.lib` &nbsp;&nbsp;&nbsp;added to 'Object/library modules' in the linker tab for the &nbsp;Win32 Debug&nbsp;&nbsp; configuation.
  * `path\to\libxm-windows\lib\Release\xm.lib` added to 'Object/library modules' in the linker tab for the &nbsp;Win32 Release&nbsp; configuation.

* Make sure to build the library before using it. We did not package the binaries.



Size
====

If you are using libxm to play a single module (like in a demo/intro), disable
features as suggested by `libxmize analyze` to save a few more bytes.

For example:

* `libxmize analyze mindrmr.xm` suggests `-DXM_DISABLED_EFFECTS=0xFFFFD9FBFFDE68E1 -DXM_DISABLED_VOLUME_EFFECTS=0x0CC0 -DXM_DISABLED_FEATURES=0x120037FFFC77EE04 -DXM_PANNING_TYPE=8`

* We only want to play the module once and quit, so we can use
  `-DXM_LOOPING_TYPE=1`

* To get better compression of the module file itself, we also use
  `-DXM_SAMPLE_TYPE=int8_t -DXM_LIBXM_DELTA_SAMPLES=ON`


* The dumped context, as generated by `libxmize dump`, compresses to
  **299915 bytes** (the base XM file compresses to 302260 bytes)

Another example with `elysium.mod` (flags used: `-DXM_DISABLED_EFFECTS=0xFFFFFFFFFFFFE3FD -DXM_DISABLED_VOLUME_EFFECTS=0xFFFE -DXM_DISABLED_FEATURES=0x047D37FFF7FFFDFB -DXM_PANNING_TYPE=1 -DXM_LOOPING_TYPE=2 -DXM_SAMPLE_TYPE=int8_t -DXM_LIBXM_DELTA_SAMPLES=OFF`): **73516 bytes** for the dumped context (the base MOD file compresses to 73849 bytes).

Examples
========

* `libxmize`: an auxiliary tool to use various libxm features. Run `libxmize`
  without any arguments for usage instructions.

* A sort of replacement for `libxmtoau` is planned. As it cannot be ported easily to Windows. 
  And any CLI players I wrote so far are too finicky on Windows 98.
  
* `xmwave` is a CLI program written for this libxm fork. It takes a tracker file as input, 
  and converts and saves it to a (16-bit PCM, 48kHz, mono) `.wav` file. 
  * Usage: `xmwave path\to\file.xm  path\to\output.wav`


Here are some interesting modules, most showcase unusual or advanced
tracking techniques (and thus are a good indicator of a player's
accuracy):

* [Cerror - Expatarimental](https://artefact2.github.io/libxm.js/#136603)
* [Lamb - Among the stars](https://artefact2.github.io/libxm.js/#165819)
* [Raina - Cyberculosis](https://artefact2.github.io/libxm.js/#165308)
* [Raina - Slumberjack](https://artefact2.github.io/libxm.js/#148721)
* [Strobe - One for all](https://artefact2.github.io/libxm.js/#161246)
* [Strobe - Paralysicical death](https://artefact2.github.io/libxm.js/#65817)

Known inaccuracies
==================

* Set channel panning (E8y; not in base FT2) is supported for XM/MOD
  * Can be disabled via `EFFECT_SET_CHANNEL_PANNING`
* Glissando control (E3y) with Amiga frequencies is not yet supported
* Arpeggios after pitch slides with Amiga frequencies are subtly incorrect
* Amiga filter toggle (E0y) is not supported, and is unlikely to be
* Invert loop / funk repeat (EFy) is not supported, and is unlikely to be
* Period wraparound after a long slide down (with eg, 2xx) is not accurate
* Global volume effects (Gxx/Hxy) are subtly incorrect
* Tone portamento (3xx/Mx) does not "lock" its direction
* Arpeggio (0xy) does not reset vibrato offset (Vy) when Spd=1
* (MOD only) Sample offset (9xx) beyond sample loop end will cut the note
  * Can be manually toggled with `FEATURE_ACCURATE_SAMPLE_OFFSET_EFFECT`
* (S3M only) Sxy has incorrect memory semantics
* (S3M only) Note cut (SCy), finetune (S2y), tone portamento (Gxx), waveform control (S3y/S4y) effects are implemented incorrectly
* (S3M only) Old stereo control (SAy) might not behave correctly
* (S3M only) 16 bit samples are supported (not in base ST3)
* (S3M only) Stereo samples are not supported (not in base ST3)

To report more, please [open an issue](../../issues?q=is%3Aissue%20state%3Aopen%20label%3Abug).
**DO NOT HARASS [Artefact2](https://github.com/Artefact2) OVER ISSUES OF THIS FORK!** 

Tests
=====

Some XM files for testing can be found in the `tests` directory. 

Libxm comes with some tests. And while I ported them to C89, the tests do not work entirely on Windows. 
I think it is because the tests were made for Linux in mind rather than the library not working identically. 

I do not know much about getting them to work. But I welcome any sane contributions here. 

Thanks
======

Thanks to:

* Romain "Artefact2" Dalmaso <artefact2@gmail.com>, for creating his wonderful project. 

* Thunder <kurttt@sfu.ca>, for writing the `modfil10.txt` file;

* Matti "ccr" Hamalainen <ccr@tnsp.org>, for writing the `xm-form.txt`
  file;

* Mr.H of Triton and Guru and Alfred of Sahara Surfers, for writing
  the specification of XM 1.04 files;

* All the MilkyTracker contributors, for the [thorough
  documentation](http://www.milkytracker.org/docs/manual/MilkyTracker.html#effects)
  of effects;

* Vladimir Kameñar, for writing `The Unofficial XM File Format Specification`;

* All the people that helped on `#milkytracker` IRC;

* All the
  [libxm](https://github.com/Artefact2/libxm/graphs/contributors)
  contributors.
