@echo off
REM Arguments: username password [list of ips]

set username=%1
shift
set passwd=%1
shift

echo "passwd is %passwd%"

:Start
if "%1"=="" GOTO Done
echo "rebooting %1"

".\plink.exe" -ssh -v -pw %passwd% %username%@%1 "init 6"

Shift
GOTO Start

:Done
