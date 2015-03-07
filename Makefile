# You can build this using Visual Studio 2013 (earlier versions
# don't have C99 support) by setting `$NF_USE_MSVC` and running `make`
# from a Windows command prompt. You still need to install make
# from Cygwin or similar.

default: unit_tests run_all_tests

CC = clang --std=c99 -g
DEFINE = -D
OUT = -o

ifdef NF_USE_MSVC
	CC = cl /Zi /nologo /Dinline=/**/
	DEFINE = /D
	OUT = /Fe
endif

.PHONY: run_tests
run_all_tests: unit_tests/string_table.passed unit_tests/memory_tracker.passed unit_tests/config_data.passed unit_tests/json_parser.passed

%.passed : %.exe
	$<
	touch $@

unit_tests:
	mkdir unit_tests

unit_tests/string_table.exe: nf_string_table.c
	$(CC) $(DEFINE)NFST_UNIT_TEST $^ $(OUT)$@

unit_tests/memory_tracker.exe: nf_string_table.c nf_memory_tracker.c
	$(CC) $(DEFINE)NFMT_UNIT_TEST $^ $(OUT)$@

unit_tests/config_data.exe: nf_config_data.c nf_string_table.c
	$(CC) $(DEFINE)NFCD_UNIT_TEST $^ $(OUT)$@

unit_tests/json_parser.exe: nf_json_parser.c nf_config_data.c nf_string_table.c
	$(CC) $(DEFINE)NFJP_UNIT_TEST $^ $(OUT)$@

.PHONY: clean
clean:
	rm -rf unit_tests
	rm -f *.obj
	rm -f vc120.pdb
	rm -f nflibs.h
	rm -rf doc

.PHONY: header
header:
	ruby tools/make_header.rb .

.PHONY: doc
doc:
	ruby tools/make_doc.rb .