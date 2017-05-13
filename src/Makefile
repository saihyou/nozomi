OBJS = bit_board.o move_generator.o position.o usi.o usioption.o misc.o thread.o timeman.o transposition_table.o move_picker.o evaluate.o search.o benchmark.o book.o main.o

CPPFLAGS = -Wall -std=c++11 -DHAVE_SSE4 -march=native
LDFLAGS = -pthread

ifdef DEBUG
CPPFLAGS += -g
else
CPPFLAGS += -Ofast -DNDEBUG
endif

ifdef LEARN
CPPFLAGS += -fopenmp -DLEARN
OBJS += learn.o reinforcer.o kifu_maker.o
LDFLAGS += -fopenmp
endif

ifdef UPDATE
CPPFLAGS += -fopenmp -DLEARN -DDISABLE_TT
OBJS += learn.o reinforcer.o kifu_maker.o
LDFLAGS += -fopenmp
endif

ifdef APERY_BOOK
OBJS += apery_book.o
CPPFLAGS += -DAPERY_BOOK
endif

nozomi: $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@

.PHONY: clean
clean:
	$(RM) nozomi *.o *~
