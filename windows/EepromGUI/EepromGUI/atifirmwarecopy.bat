"c:\Program Files (x86)\SmartRange-beta\pscp.exe" -pw shoot %2 root@%1:/home/root

"c:\Program Files (x86)\SmartRange-beta\plink.exe" -ssh -v -pw shoot root@%1 "chmod +x ./%3"

"c:\Program Files (x86)\SmartRange-beta\plink.exe" -ssh -v -pw shoot root@%1 "./%3"
pause
