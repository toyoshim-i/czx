# g800 (Vendored Upstream Source)

This directory contains the unmodified upstream source distribution of **g800**, a SHARP pocket computer (PC-G850 / PC-G815 / PC-E200) emulator written by **maruhiro**.

## Provenance

| Item | Details |
| :--- | :--- |
| **Project** | `g800` — SHARP PC-G850 / PC-G815 / PC-E200 Emulator |
| **Author** | `maruhiro` (Version 0) |
| **Homepage** | https://ver0.sakura.ne.jp/pc/index.html#g800 (`http://ver0.sakura.ne.jp/`) |
| **Upstream Version** | `0.11.4` (source file timestamps up to `2023-06-19`) |
| **Source Archive** | https://ver0.sakura.ne.jp/pc/g800src.0.11.4.tgz |
| **License** | 2-Clause BSD License (`g800`); permissive free-software license (`k6x8` font) |
| **Vendored Commit** | `cb9e9fd` (extracted directly from `g800src.0.11.4.tgz`, since upstream distributes versioned `.tgz` tarballs rather than a Git repository) |

## Local Modifications & Build Integration

- **Unmodified upstream files**: All files extracted from `g800src.0.11.4.tgz` are kept verbatim. This `README.md` is the only file added inside `third_party/g800/`.
- **No copyrighted ROMs**: No SHARP system ROM images are included.
- **SDL-free `czx` integration**: `czx` builds `third_party/g800/` as the static library `czx_g800_upstream` via the POSIX/SDL-free bridge under [cmake/g800_czx/](../../cmake/g800_czx/) (`g800_wrap.c`, `depend.h`, `g800_bridge.h`) and [src/core/g800_core.cpp](../../src/core/g800_core.cpp). Upstream SDL/platform files (`depend.c`, `SDL_hex.c`, `sdlxpm.c`) are not compiled or linked.
- **Configure-time build patch**: [CMakeLists.txt](../../CMakeLists.txt) writes a patched copy of `basic.c` to `${CMAKE_CURRENT_BINARY_DIR}/g800_basic.c` fixing pointer initialization order in `runProg()` (`skipBlank(&p); p = buf;` → `p = buf; skipBlank(&p);`), leaving [basic.c](basic.c) untouched.

## Licenses

Full upstream license notices as distributed in [doc/manual.txt](doc/manual.txt):

### `g800` (2-Clause BSD License)

```text
Copyright (c) 2005 ~ 2023 maruhiro
All rights reserved.

Redistribution and use in source and binary forms,
with or without modification, are permitted provided that
the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Embedded Font `k6x8` ([font.txt](font.txt) / [font.h](font.h))

`g800` embeds a modified version of Num Kadoma's `k6x8` bitmap font:

```text
These fonts are free softwares.
Unlimited permission is granted to use, copy, and distribute it, with or without modification, either commercially and noncommercially.
THESE FONTS ARE PROVIDED "AS IS" WITHOUT WARRANTY.

Copyright(C) 2000-2007 Num Kadoma
```

### Upstream SDL Notice ([doc/README-SDL.txt](doc/README-SDL.txt))

[doc/README-SDL.txt](doc/README-SDL.txt) (SDL zlib license notice) is preserved verbatim as part of the upstream `g800src.0.11.4.tgz` archive; `czx`'s built-in `g800` core does not link or redistribute SDL.

## Refreshing from Upstream

To update to a newer upstream tarball `g800src.<ver>.tgz` while keeping `README.md`:

```bash
tar -xz --strip-components=1 -C third_party/g800 -f g800src.<ver>.tgz
git checkout -- third_party/g800/README.md
```
