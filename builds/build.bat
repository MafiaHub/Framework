: <<'WINDOWS_BATCH'
@echo off
setlocal
cd /d "%~dp0.."
if "%~1"=="" exit /b 2
if not "%~2"=="64" if not "%~2"=="32" exit /b 2
set "FW_TARGET_ARCH=x64"
if "%~2"=="32" set "FW_TARGET_ARCH=x86"
if defined VCToolsInstallDir goto configured
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FW_VS_ROOT=%%i"
if not defined FW_VS_ROOT exit /b 1
if "%~2"=="32" (
    call "%FW_VS_ROOT%\VC\Auxiliary\Build\vcvars32.bat"
) else (
    call "%FW_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
)
if errorlevel 1 exit /b %errorlevel%
:configured
if not defined FW_WINE_BUILD goto native
cmake -S . -B builds/build-%~2 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVCPKG_TARGET_TRIPLET=%FW_TARGET_ARCH%-windows-wine -DVCPKG_HOST_TRIPLET=x64-windows-wine ^
    -DVCPKG_APPLOCAL_DEPS=OFF -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded ^
    "-DVCPKG_OVERLAY_PORTS=%CD%\vendors\vcpkg\ports" ^
    -DKCDC_NPM_EXECUTABLE=KCDC_NPM_EXECUTABLE-NOTFOUND -DKCDC_PNPM_EXECUTABLE=KCDC_PNPM_EXECUTABLE-NOTFOUND
goto build
:native
cmake -S . -B builds/build-%~2 -G Ninja -DCMAKE_BUILD_TYPE=Debug
:build
if errorlevel 1 exit /b %errorlevel%
cmake --build builds/build-%~2 --target %1
exit /b %errorlevel%
WINDOWS_BATCH
if [[ ${2:-} == linux64 ]]; then
    exec bash "$(dirname -- "${BASH_SOURCE[0]}")/../scripts/linux/build.sh" "$@"
fi
exec bash "$(dirname -- "${BASH_SOURCE[0]}")/../scripts/windows/build.sh" "$@"
