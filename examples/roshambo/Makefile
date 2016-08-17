
# Each .idel file is assembled into a .bot file
IDEL_SOURCES	:= $(wildcard *.idel)
IDEL_PROGRAMS	:= $(patsubst %.idel,%.bot,$(IDEL_SOURCES))

all: $(IDEL_PROGRAMS)

clean:
	-rm $(IDEL_PROGRAMS)

distclean: clean
	-rm *~

test: all
	@echo "Starting tournament.."
	./roshambo.py 10 'idelvm titfortat.bot' 'idelvm shortsight.bot'

run: all
	./roshambo.py 30 'idelvm dc3.bot' 'idelvm freq.bot'

%.bot: %.idel
	idelasm < $< > $@
