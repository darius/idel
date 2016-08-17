CLEAN += $(shell echo bin/idelasm bin/idelvm libidel.a src/*.o src/*.a src/*.inc src/*_names.h src/image)

src/public_names.inc: src/public.names
	$(AWK) '{ printf("#define %-20s idel_%s\n", $$1, $$1); }' $< >$@
src/private_names.inc: src/private.names
	$(AWK) '{ printf("#define %-20s idel__%s\n", $$1, $$1); }' $< >$@

header := src/idel.h src/idel_porting.h src/idel_private.h \
	  src/public_names.inc src/private_names.inc src/enum.inc
src/codegen.o:    $(header) src/codegen.c \
                  src/effect.inc src/prims.inc src/peep.inc src/peep1.inc
src/debug.o:      $(header) src/idel_private.h src/debug.c \
		  src/names.inc src/args.inc
src/interleave.o: $(header) src/interleave.c
src/obj_encode.o: $(header) src/obj_encode.c
src/obj_decode.o: $(header) src/obj_decode.c
src/utility.o:    $(header) src/utility.c
src/idelasm.o:    $(header) src/idelasm.c src/dict.inc 
src/idelvm.o:     $(header) src/idelvm.c src/interp.inc src/labels.inc
src/idelvmmain.o: src/idel.h src/idel_porting.h src/idelvm.c \
                  src/interp.inc src/labels.inc

utilityobjs := src/interleave.o src/utility.o 
encodeobjs := src/obj_decode.o src/obj_encode.o

libidelobjs := src/codegen.o src/debug.o src/idelvm.o \
	       $(utilityobjs) $(encodeobjs)

libidel.a: $(libidelobjs)
	-rm -f $@
	$(AR) cru $@ $(libidelobjs)
	$(RANLIB) $@

idelasmobjs := src/idelasm.o 
bin/idelasm: $(idelasmobjs) $(utilityobjs) src/obj_encode.o
	$(CC) $(CFLAGS) -o $@ $(idelasmobjs) $(utilityobjs) src/obj_encode.o

idelvmobjs := src/idelvmmain.o libidel.a
bin/idelvm: $(idelvmobjs) 
	$(CC) $(CFLAGS) -o $@ $(idelvmobjs)

src/interp.inc src/dict.inc src/names.inc src/args.inc \
src/enum.inc src/labels.inc src/effect.inc src/prims.inc \
src/peep.inc src/peep1.inc: \
		src/opcodes src/combos src/opcodes.awk
	$(AWK) -v cc=$(CC) -f src/opcodes.awk src/opcodes


my_core_objs := src/idelvmmain.o $(libidelobjs) $(idelasmobjs)
CORE_SRC += $(patsubst %.o,%.c,$(my_core_objs))
CORE_SRC += src/idel.h src/idel_private.h src/idel_porting.h
CORE_SRC += src/opcodes src/opcodes.awk
