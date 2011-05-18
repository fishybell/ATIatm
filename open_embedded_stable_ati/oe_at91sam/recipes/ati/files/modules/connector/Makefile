# Makefile for Action Target specific files

include ../files.mk

default: event_conn bcast_client bcast_server

event_conn: $(EC_C_FILES) $(EC_H_FILES)
	$(MAKE) -C .. event_conn
	cp ../event_conn .

bcast_client: bcast_client.c bcast.h
	$(MAKE) -C .. bcast_client
	cp ../bcast_client .

bcast_server: bcast_server.c bcast.h
	$(MAKE) -C .. bcast_server
	cp ../bcast_server .

clean:
	$(RM) event_conn bcast_server bcast_client
