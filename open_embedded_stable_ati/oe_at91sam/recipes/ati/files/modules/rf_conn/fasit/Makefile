# Makefile for the RF system


default: mcp

mcp : mcp.c minion.c mcp.h fasit_c.h
	gcc -lrt -std=gnu99 -o mcp  mcp.c minion.c

clean:
	$(RM) mcp *.o 

