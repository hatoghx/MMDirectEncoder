# Third-Party Notices

MMDirectEncoder uses the following third-party software.

| Software | Use in MMDirectEncoder | License |
|---|---|---|
| [FFmpeg 7.1.1, Gyan.dev full build](https://www.gyan.dev/ffmpeg/builds/) | Video and image encoding backend (bundled as `bin\ffmpeg.exe`) | GPL v3 |
| [OpenEXR 3.4.16](https://github.com/AcademySoftwareFoundation/openexr) | OpenEXR file writing, half conversion and ZIP compression (statically linked) | BSD-3-Clause, see `OpenEXR-LICENSE.md` |
| [Imath 3.2.3](https://github.com/AcademySoftwareFoundation/Imath) | Half-float type used by OpenEXR (statically linked) | BSD-3-Clause, see `Imath-LICENSE.md` |
| [libdeflate](https://github.com/ebiggers/libdeflate) | Compression library vendored in OpenEXR (statically linked) | MIT, see `libdeflate-LICENSE.txt` |
| [OpenJPH](https://github.com/aous72/OpenJPH) | HTJ2K library vendored in OpenEXR (statically linked) | BSD-2-Clause, see `OpenJPH-LICENSE.txt` |
| [Microsoft DirectShow BaseClasses](https://github.com/microsoft/Windows-classic-samples) | DirectShow filter infrastructure | MIT |

The bundled FFmpeg executable reports `--enable-gpl --enable-version3` and is therefore distributed under GPL v3. Its complete build configuration can be displayed with `ffmpeg -version`.

OpenEXR, Imath, libdeflate and OpenJPH are distributed under permissive licenses that are compatible with GPL v3. Their copyright notices and license terms are included in this folder. The exact versions are fetched and built by `build_deps.cmd`.