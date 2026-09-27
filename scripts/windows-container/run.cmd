@echo off
rem A Wine console is required by PowerShell, even for noninteractive vcpkg calls.
rem Redirect inside that console so the Linux entry point can stream the log.
call W:\scripts\windows-container\environment.cmd %* > W:\_external\msvc-wine\console.log 2>&1
set "FW_RESULT=%errorlevel%"
> W:\_external\msvc-wine\exit-code.txt echo %FW_RESULT%
exit /b %FW_RESULT%
