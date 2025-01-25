@echo off

rem Change to the current folder.
cd "%~dp0"

rem Remove the publish folder for a fresh compile.
rd /s /q Publish

rem Build all targets
call "%~dp0BuildAllTargets.cmd"

rem Copy the output to the publish folder
xcopy /s /y libwsk\*.h Publish\include\libwsk\
del Publish\include\libwsk\Precompiled.h

xcopy /s /y Output\Binaries\*libwsk.lib Publish\lib\
xcopy /s /y Output\Binaries\*libwsk.pdb Publish\lib\
