"c:\Program Files (x86)\SmartRange-beta\pscp.exe" -pw shoot %2 root@%1:/home/root

"c:\Program Files (x86)\SmartRange-beta\plink.exe" -ssh -v -pw shoot root@%1 "chmod +x ./atifirmware.sh"

"c:\Program Files (x86)\SmartRange-beta\plink.exe" -ssh -v -pw shoot root@%1 "./atifirmware.sh"
pause
