CC = gcc
CFLAGS = -Wall 
LDFLAGS = -lrt

SRC = server.c rpc.c
SRC2 = client.c rpc.c

OBJ = $(SRC:.c=.o)
OBJ2 = $(SRC2:.c=.o)

EXE = rpc-server
EXE2 = rpc-client 

all: $(EXE) $(EXE2)

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) $(SRC) -o $(EXE) $(LDFLAGS)

$(EXE2): $(OBJ2)
	$(CC) $(CFLAGS) $(SRC2) -o $(EXE2) $(LDFLAGS)

rpc.o: rpc.c rpc.h

clean:
	rm -f $(OBJ) $(EXE)
	rm -f $(OBJ2) $(EXE2)