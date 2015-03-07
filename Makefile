default: unit_tests run_all_tests

CC = clang --std=c99 -g

.PHONY: run_tests
run_all_tests: unit_tests/string_table.passed unit_tests/memory_tracker.passed unit_tests/config_data.passed unit_tests/json_parser.passed

%.passed : %.exe
	$<
	touch $@

unit_tests:
	mkdir unit_tests

unit_tests/string_table.exe: nf_string_table.c
	$(CC) -DNFST_UNIT_TEST $^ -o $@

unit_tests/memory_tracker.exe: nf_string_table.c nf_memory_tracker.c
	$(CC) -DNFMT_UNIT_TEST $^ -o $@

unit_tests/config_data.exe: nf_config_data.c nf_string_table.c
	$(CC) -DNFCD_UNIT_TEST $^ -o $@

unit_tests/json_parser.exe: nf_json_parser.c nf_config_data.c nf_string_table.c
	$(CC) -DNFJP_UNIT_TEST $^ -o $@

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