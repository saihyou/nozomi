OBJS = bit_board.o move_generator.o position.o usi.o usioption.o misc.o thread.o timeman.o transposition_table.o move_picker.o evaluate.o search.o move_probability.o benchmark.o book.o main.o

CPPFLAGS = -Wall -std=c++1z -DHAVE_SSE4 -march=native
LDFLAGS = -pthread

ifdef DEBUG
CPPFLAGS += -g
else
CPPFLAGS += -Ofast -DNDEBUG
endif

ifdef LEARN
CPPFLAGS += -fopenmp -DLEARN
OBJS += reinforcer.o kifu_maker.o
LDFLAGS += -fopenmp
endif

ifdef UPDATE
CPPFLAGS += -fopenmp -DLEARN -DDISABLE_TT -DREPETITION_CHECK_FULL
OBJS += reinforcer.o kifu_maker.o
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
