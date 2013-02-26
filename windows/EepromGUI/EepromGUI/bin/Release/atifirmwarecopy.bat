"c:\Program Files (x86)\SmartRange-beta\pscp.exe" -pw %5 %2 %4@%1:/home/root

"c:\Program Files (x86)\SmartRange-beta\plink.exe" -ssh -v -pw %5 %4@%1 "chmod +x ./%3"

"c:\Program Files (x86)\SmartRange-beta\plink.exe" -ssh -v -pw %5 %4@%1 "./%3"
pause
