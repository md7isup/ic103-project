CC      = gcc
CFLAGS  = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g
OBJS    = main.o ui.o stopwatch_stub.o alarm_stub.o budget_stub.o file_stub.o signals_stub.o app_stub.o

sw: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) sw

.PHONY: clean
