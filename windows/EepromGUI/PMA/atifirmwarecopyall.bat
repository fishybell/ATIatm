@echo off
REM Arguments: firmware username password [list of ips]

set firmware=%1
shift
set username=%1
shift
set passwd=%1
shift

echo "passwd is %passwd%"

:Start
if "%1"=="" GOTO Done
echo "copying %firmware% to %1"
".\pscp.exe" -pw %passwd% %firmware% %username%@%1:/home/root

".\plink.exe" -ssh -v -pw %passwd% %username%@%1 "chmod +x ./%firmware%"

".\plink.exe" -ssh -v -pw %passwd% %username%@%1 "./%firmware%"

Shift
GOTO Start

:Done
