@echo off
call C:\tools\path.cmd
set "VCPKG_VISUAL_STUDIO_PATH=C:\msvc"
set "VS170COMNTOOLS=C:\msvc\Common7\Tools\"
set "VCPKG_DISABLE_METRICS=1"
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "VCPKG_BINARY_SOURCES=clear;files,W:\_external\msvc-wine\binary-cache,readwrite"
set "VCPKG_DOWNLOADS=W:\_external\msvc-wine\downloads"
set "POWERSHELL_TELEMETRY_OPTOUT=1"
set "FW_CMAKE_ARGS=%FW_CMAKE_ARGS% -DFW_VCPKG_ROOT=W:/_external/msvc-wine/vcpkg -DVCPKG_HOST_TRIPLET=x64-windows-mh"
if "%~2"=="32" (
    call C:\msvc\VC\Auxiliary\Build\vcvarsall.bat amd64_x86
) else (
    call C:\msvc\VC\Auxiliary\Build\vcvars64.bat
)
if errorlevel 1 exit /b 1
echo MSVC toolset %VCToolsVersion%, target %VSCMD_ARG_TGT_ARCH%
cd /d W:\
call builds\build.bat %*
exit /b %errorlevel%
