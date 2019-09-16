# Makefile to build Linux-based Jasmine OpenSSD installer

CC = gcc
CFLAG = -O0 -g -Wall
INC = -I../include -I../target_spw -I../ftl_greedy
#DEPS = ./installer.c ./ata_7.h ../include/jasmine.h ../include/rom.h
SRCS = ./installer.c

installer: ./installer.c ./ata_7.h ../include/jasmine.h ../include/rom.h
	$(CC) $(CFLAG) $(SRCS) $(INC) -o $@

clean: 
	-rm installer
