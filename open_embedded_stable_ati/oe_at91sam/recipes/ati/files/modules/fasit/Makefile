# Makefile for Action Target specific files

include ../files.mk

default: fasit_conn

fasit_conn: $(FC_C_FILES) $(FC_H_FILES)
	$(MAKE) -C .. fasit_conn
	cp ../fasit_conn .

clean:
	$(RM) fasit_conn
