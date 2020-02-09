.PHONY:clean
CC=g++
OUTDIR=./output
CFLAGS=-Wall -g
#-Wall -g
BINDIR=.
#/usr/local/xjobs
BIN=$(BINDIR)/xjobs
SRC=$(wildcard *.cpp)
OBJS=$(patsubst %.cpp,${OUTDIR}/%.o,$(SRC))
all:$(OUTDIR) $(BINDIR) $(BIN)
$(OUTDIR):
	mkdir -p $@
$(BINDIR):
	mkdir -p $@
$(BIN):$(OBJS)
	${CC} ${CFLAGS} $^ -o $@  -llog4cxx -lpthread -lACE  -lopus -ljsoncpp -lcurl -lssl -lcrypto -luuid  -lmysqlcppconn -L ./lib/linux/ -L ./lib/linux/openssl/ -L ./lib/linux/crypto 
${OUTDIR}/%.o:%.cpp
	$(CC) ${CFLAGS} -c $< -o $@ -I ./include/ 
clean:
	rm  -rf $(OBJS) $(BIN)
