OBJS = bit_board.o move_generator.o position.o usi.o usioption.o misc.o thread.o timeman.o transposition_table.o move_picker.o evaluate.o search.o benchmark.o book.o main.o

CPPFLAGS = -Wall -std=c++11 -DHAVE_SSE4 -msse4 -mbmi2
LDFLAGS = -pthread

ifdef DEBUG
CPPFLAGS += -g
else
CPPFLAGS += -O2 -DNDEBUG
endif

ifdef LEARN
CPPFLAGS += -fopenmp -DLEARN
OBJS += learn.o
LDFLAGS += -fopenmp
endif


nozomi: $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@

.PHONY: clean
clean:
	$(RM) nozomi *.o *~
