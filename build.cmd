@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "DIST=%ROOT%..\build"
set "OBJ=%ROOT%obj"
set "BASE=%ROOT%native-filter\third_party\baseclasses"
set "SRC_FILTER=%ROOT%native-filter\src"
set "SRC_CONFIG=%ROOT%config"
set "SRC_ENCODER=%ROOT%encoder"
set "SRC_UI=%ROOT%ui"
set "DEPS_PREFIX=%ROOT%..\deps\install"
set "EXR_INC=/DNOMINMAX /I"%DEPS_PREFIX%\include\OpenEXR" /I"%DEPS_PREFIX%\include\Imath" /I"%DEPS_PREFIX%\include""
set "EXR_LIBS=/LIBPATH:"%DEPS_PREFIX%\lib" OpenEXR-3_4.lib Iex-3_4.lib IlmThread-3_4.lib OpenEXRCore-3_4.lib openjph.lib Imath-3_2.lib"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: Visual Studio 2022 was not found.
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo ERROR: Visual C++ toolchain was not found.
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

call "%ROOT%build_deps.cmd" || exit /b 1
if not exist "%DIST%" mkdir "%DIST%"
if not exist "%DIST%\bin" mkdir "%DIST%\bin"
if not exist "%OBJ%\base" mkdir "%OBJ%\base"
if not exist "%OBJ%\core" mkdir "%OBJ%\core"
if not exist "%OBJ%\tests" mkdir "%OBJ%\tests"

set "COMMON=/nologo /c /O2 /MT /EHsc /std:c++17 /utf-8 /guard:cf /DWIN32 /D_WINDOWS /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /I"%BASE%" /I"%SRC_CONFIG%" /I"%SRC_ENCODER%" /I"%SRC_UI%" /I"%SRC_FILTER%""

if not exist "%OBJ%\strmbase.lib" (
  echo Building DirectShow BaseClasses...
  for %%F in ("%BASE%\*.cpp") do (
    if /I not "%%~nxF"=="dllentry.cpp" (
      cl %COMMON% /W3 /Fo"%OBJ%\base\%%~nF.obj" "%%~fF" || exit /b 1
    )
  )
  lib /nologo /out:"%OBJ%\strmbase.lib" "%OBJ%\base\*.obj" || exit /b 1
)

echo Compiling shared modules...
cl %COMMON% /W4 /Fo"%OBJ%\core\config.obj" "%SRC_CONFIG%\encoder_config.cpp" || exit /b 1
cl %COMMON% /W4 /Fo"%OBJ%\core\encoder.obj" "%SRC_ENCODER%\encoder_controller.cpp" || exit /b 1
cl %COMMON% /W4 %EXR_INC% /Fo"%OBJ%\core\exr.obj" "%SRC_ENCODER%\exr_writer.cpp" || exit /b 1
cl %COMMON% /W4 /Fo"%OBJ%\core\ui.obj" "%SRC_UI%\property_dialog.cpp" || exit /b 1
rc /nologo /c65001 /fo"%OBJ%\core\encoder_ui.res" "%SRC_UI%\encoder_ui.rc" || exit /b 1
rc /nologo /fo"%OBJ%\core\version.res" "%ROOT%native-filter\version.rc" || exit /b 1

echo Building native DirectShow filter DLL...
cl %COMMON% /W4 /Fo"%OBJ%\core\ffmpeg_encoder.obj" "%SRC_FILTER%\ffmpeg_encoder.cpp" || exit /b 1
cl %COMMON% /W4 /Fo"%OBJ%\core\dll.obj" "%SRC_FILTER%\dll.cpp" || exit /b 1
cl %COMMON% /W4 /Fo"%OBJ%\core\mmd_host.obj" "%SRC_FILTER%\mmd_host.cpp" || exit /b 1

link /nologo /DLL /MACHINE:X64 /DYNAMICBASE /NXCOMPAT /GUARD:CF /OPT:REF /OPT:ICF ^
  /OUT:"%DIST%\MMDirectEncoder.dll" /PDB:"%OBJ%\MMDirectEncoder.pdb" ^
  /IMPLIB:"%OBJ%\MMDirectEncoder.lib" ^
  /DEF:"%ROOT%native-filter\ffmpeg_encoder.def" ^
  "%OBJ%\core\config.obj" "%OBJ%\core\encoder.obj" "%OBJ%\core\exr.obj" "%OBJ%\core\ui.obj" ^
  "%OBJ%\core\ffmpeg_encoder.obj" "%OBJ%\core\dll.obj" "%OBJ%\core\mmd_host.obj" ^
  "%OBJ%\core\encoder_ui.res" "%OBJ%\core\version.res" ^
  "%OBJ%\strmbase.lib" strmiids.lib comctl32.lib uxtheme.lib shell32.lib shlwapi.lib ^
  winmm.lib ole32.lib oleaut32.lib user32.lib gdi32.lib advapi32.lib dxgi.lib %EXR_LIBS%
if errorlevel 1 exit /b 1

echo Building standalone config executable...
cl %COMMON% /W4 /Fo"%OBJ%\core\standalone.obj" "%SRC_UI%\standalone_config.cpp" || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /MACHINE:X64 /DYNAMICBASE /NXCOMPAT ^
  /OUT:"%DIST%\MMDirectEncoderConfig.exe" /PDB:"%OBJ%\MMDirectEncoderConfig.pdb" ^
  "%OBJ%\core\config.obj" "%OBJ%\core\encoder.obj" "%OBJ%\core\exr.obj" "%OBJ%\core\ui.obj" ^
  "%OBJ%\core\standalone.obj" "%OBJ%\core\encoder_ui.res" ^
  comctl32.lib uxtheme.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib user32.lib gdi32.lib advapi32.lib dxgi.lib %EXR_LIBS%
if errorlevel 1 exit /b 1

if not exist "%DIST%\bin\ffmpeg.exe" (
  if exist "%LOCALAPPDATA%\MMDirectEncoder\bin\ffmpeg.exe" (
    copy /y "%LOCALAPPDATA%\MMDirectEncoder\bin\ffmpeg.exe" "%DIST%\bin\ffmpeg.exe" >nul
  )
)

if exist "%DIST%\LICENSES" rmdir /s /q "%DIST%\LICENSES"
if exist "%DIST%\LICENSE.txt" del /q "%DIST%\LICENSE.txt"
mkdir "%DIST%\LICENSES"
copy /y "%ROOT%LICENSES\THIRD_PARTY_NOTICES.md" "%DIST%\LICENSES\THIRD_PARTY_NOTICES.md" >nul
copy /y "%ROOT%LICENSE" "%DIST%\LICENSE" >nul

echo Building tests...
cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /permissive- /utf-8 ^
  /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 ^
  /Fo"%OBJ%\tests\abi_smoke.obj" "%ROOT%native-filter\tests\abi_smoke.cpp" ^
  /Fe:"%OBJ%\tests\abi_smoke.exe" ole32.lib strmiids.lib
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /permissive- /utf-8 ^
  /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 ^
  /I"%BASE%" /I"%SRC_FILTER%" /Fo"%OBJ%\tests\test_graph.obj" "%ROOT%native-filter\tests\test_graph.cpp" ^
  /Fe:"%OBJ%\tests\test_graph.exe" ole32.lib oleaut32.lib strmiids.lib
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /permissive- /utf-8 ^
  /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 ^
  /I"%SRC_CONFIG%" /I"%SRC_ENCODER%" /Fo"%OBJ%\tests\test_suite.obj" ^
  "%ROOT%native-filter\tests\test_encoder_suite.cpp" ^
  "%OBJ%\core\config.obj" "%OBJ%\core\encoder.obj" "%OBJ%\core\exr.obj" ^
  /Fe:"%OBJ%\tests\test_encoder_suite.exe" shell32.lib ole32.lib advapi32.lib dxgi.lib /link %EXR_LIBS%
if errorlevel 1 exit /b 1

set "ISCC="
for %%P in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe") do if exist %%P set "ISCC=%%~P"
if defined ISCC (
  echo Building installer...
  "%ISCC%" /Q "%ROOT%installer\MMDirectEncoder.iss" || exit /b 1
) else (
  echo Inno Setup 6 was not found. The installer was not built.
)

echo Build finished successfully!
endlocal
