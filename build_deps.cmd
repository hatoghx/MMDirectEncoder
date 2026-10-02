@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "DEPS=%ROOT%..\deps"
set "PREFIX=%DEPS%\install"
set "IMATH_TAG=v3.2.3"
set "OPENEXR_TAG=v3.4.16"

if exist "%PREFIX%\lib\openjph.lib" (
  echo Dependencies are already built.
  exit /b 0
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo ERROR: Visual C++ toolchain was not found.
  exit /b 1
)
set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
  echo ERROR: CMake bundled with Visual Studio was not found.
  exit /b 1
)

if not exist "%DEPS%" mkdir "%DEPS%"
if not exist "%DEPS%\imath-src" (
  git -c advice.detachedHead=false clone --depth 1 --branch %IMATH_TAG% https://github.com/AcademySoftwareFoundation/Imath.git "%DEPS%\imath-src" || exit /b 1
)
if not exist "%DEPS%\openexr-src" (
  git -c advice.detachedHead=false clone --depth 1 --branch %OPENEXR_TAG% https://github.com/AcademySoftwareFoundation/openexr.git "%DEPS%\openexr-src" || exit /b 1
)

set "COMMON_ARGS=-G "Visual Studio 17 2022" -A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="%PREFIX%" -DCMAKE_PREFIX_PATH="%PREFIX%" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW"

"%CMAKE%" -S "%DEPS%\imath-src" -B "%DEPS%\imath-build" %COMMON_ARGS% -DPYTHON=OFF -DIMATH_INSTALL_PKG_CONFIG=OFF || exit /b 1
"%CMAKE%" --build "%DEPS%\imath-build" --config Release --target install || exit /b 1

"%CMAKE%" -S "%DEPS%\openexr-src" -B "%DEPS%\openexr-build" %COMMON_ARGS% ^
  -DOPENEXR_BUILD_TOOLS=OFF -DOPENEXR_INSTALL_TOOLS=OFF -DOPENEXR_BUILD_EXAMPLES=OFF ^
  -DOPENEXR_TEST_LIBRARIES=OFF -DOPENEXR_TEST_TOOLS=OFF -DOPENEXR_TEST_PYTHON=OFF ^
  -DOPENEXR_INSTALL_PKG_CONFIG=OFF -DOPENEXR_FORCE_INTERNAL_DEFLATE=ON || exit /b 1
"%CMAKE%" --build "%DEPS%\openexr-build" --config Release --target install || exit /b 1
copy /y "%DEPS%\openexr-build\external\OpenJPH\src\core\openjph.dir\Release\openjph.lib" "%PREFIX%\lib\openjph.lib" >nul || exit /b 1

echo Dependencies built successfully.
endlocal
