".\pscp.exe" -pw %5 %2 %4@%1:/home/root

".\plink.exe" -ssh -v -pw %5 %4@%1 "chmod +x ./%3"

".\plink.exe" -ssh -v -pw %5 %4@%1 "./%3"

