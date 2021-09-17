REM This file was taken from RakNet 4.082 without any modifications.
REM Please see licenses/RakNet license.txt for the underlying license and related copyright.

IF EXIST "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\bin" GOTO SKIPMAKEBIN
mkdir "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\bin"
:SKIPMAKEBIN
IF EXIST "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\bin\X86" GOTO SKIPMAKEX86
mkdir "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\bin\X86"
:SKIPMAKEX86
IF EXIST "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\bin\X86\%1" GOTO SKIPMAKEOUTDIR
mkdir "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\bin\X86\%1"
:SKIPMAKEOUTDIR
copy /Y "../DLL_Swig\%1\RakNet.dll"  "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\bin\X86\%1\RakNet.dll"
copy /Y "../SwigOutput\SwigCSharpOutput\*.cs" "./InternalSwigWindowsCSharpSample\InternalSwigTestApp\SwigFiles\"
