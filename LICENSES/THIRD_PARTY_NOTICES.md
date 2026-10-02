# Third-Party Notices

MMDirect Encoder uses the following third-party software.

| Software | Use in MMDirect Encoder | License |
|---|---|---|
| [FFmpeg 7.1.1, Gyan.dev essentials build](https://www.gyan.dev/ffmpeg/builds/) | Video encoding and audio extraction backend | GPL v3 |
| [Microsoft DirectShow BaseClasses](https://github.com/microsoft/Windows-classic-samples) | DirectShow filter infrastructure | MIT |

The bundled FFmpeg executables report `--enable-gpl --enable-version3` and are therefore distributed under GPL v3. Their complete build configuration can be displayed with `ffmpeg -version`.

Copyright notices and license terms are provided by each linked upstream project. FFmpeg source and license information for the bundled build are available from the linked Gyan.dev distribution page.
