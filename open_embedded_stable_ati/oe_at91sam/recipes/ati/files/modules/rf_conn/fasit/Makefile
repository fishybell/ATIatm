# Makefile for the RF system


default: mcp RFmaster

mcp : mcp.c minion.c mcp.h fasit_c.h
	gcc -lrt -std=gnu99 -o mcp mcp.c minion.c

RFmaster : RFmaster.c mcp.h fasit_c.h
	gcc -lrt -std=gnu99 -o RFmaster RFmaster.c

clean:
	$(RM) mcp RFmaster *.o 

