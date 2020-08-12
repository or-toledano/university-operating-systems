CC = gcc
OBJS = pfind.o
EXEC = pfind
COMP_FLAG = -D_POSIX_C_SOURCE=200809 -Wall -std=c11 
SUFFIX_FLAGS = -pthread

$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(SUFFIX_FLAGS)
pfind.o: pfind.c
	$(CC) $(COMP_FLAG) -c $*.c
clean:
	rm -f $(OBJS) $(EXEC)