@echo off
rem Keep localized MSVC /showIncludes output in the code page detected by CMake.
chcp 65001 >nul

if not defined VSINSTALLDIR goto ninja_from_path
set "framework_ninja=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%framework_ninja%" goto ninja_from_path
"%framework_ninja%" %*
exit /b %errorlevel%

:ninja_from_path
ninja.exe %*
exit /b %errorlevel%
