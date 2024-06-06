CC = gcc
CFLAGS = -pedantic -Wall -std=gnu99 -pthread -I/local/courses/csse2310/include
LDFLAGS = -L/local/courses/csse2310/lib -lcsse2310a4 -lcsse2310a3 -lm
TARGETS = auctionclient auctioneer
.DEFAULT_GOAL := all
all: $(TARGETS)

auctionclient: auctionClient.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

auctionClient.o: auctionClient.c
	$(CC) $(CFLAGS) -c $<

auctioneer: auctioneer.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

auctioneer.o: auctioneer.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -rf $(TARGETS) *.o
