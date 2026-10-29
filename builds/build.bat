@echo off
setlocal
if "%~1"=="" goto usage
set "FW_ARCH=%~2"
if not "%FW_ARCH%"=="32" if not "%FW_ARCH%"=="64" goto usage
set "FW_TARGET=x64"
set "FW_VCVARS=amd64"
set "FW_SUFFIX=-64"
if "%FW_ARCH%"=="32" (
    set "FW_TARGET=x86"
    set "FW_VCVARS=amd64_x86"
    set "FW_SUFFIX="
)
set "FW_CONFIG=%~3"
if not defined FW_CONFIG set "FW_CONFIG=Debug"
set "FW_BUILD=build%FW_SUFFIX%"
if "%FW_CONFIG%"=="Release" set "FW_BUILD=build-prod%FW_SUFFIX%"
if "%FW_CONFIG%"=="RelWithDebInfo" set "FW_BUILD=build-release%FW_SUFFIX%"
if not "%FW_CONFIG%"=="Debug" if not "%FW_CONFIG%"=="Release" if not "%FW_CONFIG%"=="RelWithDebInfo" goto usage
if not defined VSCMD_ARG_TGT_ARCH (
    call :setup_msvc
    if errorlevel 1 exit /b 1
)
if not "%VSCMD_ARG_TGT_ARCH%"=="%FW_TARGET%" (
    echo An %FW_TARGET% compiler environment is required; detected "%VSCMD_ARG_TGT_ARCH%".
    exit /b 1
)
if not "%VSCMD_ARG_HOST_ARCH%"=="x64" (
    call :setup_msvc
    if errorlevel 1 exit /b 1
)
if not "%VSCMD_ARG_HOST_ARCH%"=="x64" (
    echo A 64-bit MSVC host is required; detected "%VSCMD_ARG_HOST_ARCH%".
    exit /b 1
)
if not defined VCToolsInstallDir (
    echo The selected MSVC environment does not define VCToolsInstallDir.
    exit /b 1
)
set "FW_MSVC_BIN=%VCToolsInstallDir%bin\Hostx64\%FW_TARGET%\"
set "FW_MSVC_CL=%FW_MSVC_BIN%cl.exe"
if not exist "%FW_MSVC_CL%" exit /b 1
if not exist "%FW_MSVC_BIN%link.exe" exit /b 1
if not exist "%FW_MSVC_BIN%lib.exe" exit /b 1
pushd "%~dp0.."
chcp 65001 >nul
if "%~1"=="ToolchainCheck" goto toolchain_check
set "FW_RECONFIGURE="
set "FW_CACHED_C="
set "FW_CACHED_CXX="
if exist "builds\%FW_BUILD%\CMakeCache.txt" (
    for /f "tokens=1,* delims==" %%i in ('findstr /b /c:"CMAKE_C_COMPILER:" "builds\%FW_BUILD%\CMakeCache.txt"') do set "FW_CACHED_C=%%j"
    for /f "tokens=1,* delims==" %%i in ('findstr /b /c:"CMAKE_CXX_COMPILER:" "builds\%FW_BUILD%\CMakeCache.txt"') do set "FW_CACHED_CXX=%%j"
)
if defined FW_CACHED_C set "FW_CACHED_C=%FW_CACHED_C:/=\%"
if defined FW_CACHED_CXX set "FW_CACHED_CXX=%FW_CACHED_CXX:/=\%"
if defined FW_CACHED_C if /i not "%FW_CACHED_C%"=="%FW_MSVC_CL%" set "FW_RECONFIGURE=1"
if defined FW_CACHED_CXX if /i not "%FW_CACHED_CXX%"=="%FW_MSVC_CL%" set "FW_RECONFIGURE=1"
call :configure
if errorlevel 1 goto failed
rem A compiler change makes CMake reset its cache and discard other options.
rem Reapply the complete configure arguments after that automatic reset.
if defined FW_RECONFIGURE call :configure
if errorlevel 1 goto failed
cmake --build "builds\%FW_BUILD%" --target "%~1" --parallel %CMAKE_BUILD_PARALLEL_LEVEL%
if errorlevel 1 goto failed
popd
exit /b 0
:toolchain_check
if not exist "builds\%FW_BUILD%" mkdir "builds\%FW_BUILD%"
cl /nologo /std:c++20 /EHsc /MD /Zi /DFW_CHECK_POINTER_BITS=%FW_ARCH% /Fd"builds\%FW_BUILD%\toolchain-check.pdb" /Fo"builds\%FW_BUILD%\toolchain-check.obj" /Fe"builds\%FW_BUILD%\toolchain-check.exe" scripts\windows-container\toolchain-check.cpp
if errorlevel 1 goto failed
"builds\%FW_BUILD%\toolchain-check.exe"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
:usage
echo Usage: builds\build.bat ^<target^> ^<32^|64^> [Debug^|Release^|RelWithDebInfo]
exit /b 2
:configure
cmake -S . -B "builds\%FW_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%FW_CONFIG% -DOPTION_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF ^
    -DCMAKE_MAKE_PROGRAM="%CD%\scripts\ninja_utf8.cmd" -DCMAKE_PROJECT_INCLUDE_BEFORE="%CD%\cmake\MSVCUtf8.cmake" ^
    "-DCMAKE_C_COMPILER:FILEPATH=%FW_MSVC_CL%" "-DCMAKE_CXX_COMPILER:FILEPATH=%FW_MSVC_CL%" ^
    "-DCMAKE_LINKER:FILEPATH=%FW_MSVC_BIN%link.exe" "-DCMAKE_AR:FILEPATH=%FW_MSVC_BIN%lib.exe" %FW_CMAKE_ARGS%
exit /b %errorlevel%
:setup_msvc
set "FW_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%FW_VSWHERE%" (
    echo Visual Studio Build Tools not found. Use an x64 developer prompt or scripts\build_windows_container.sh.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%FW_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FW_VS_ROOT=%%i"
rem Preview and Insiders channels are hidden from vswhere unless asked for; use one only when no release install exists.
if not defined FW_VS_ROOT for /f "usebackq tokens=*" %%i in (`"%FW_VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FW_VS_ROOT=%%i"
if not defined FW_VS_ROOT (
    echo No Visual Studio install with the x64 C++ tools found. Use an x64 developer prompt or scripts\build_windows_container.sh.
    exit /b 1
)
call "%FW_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" %FW_VCVARS%
exit /b %errorlevel%
