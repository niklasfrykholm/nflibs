default: run_tests

CC = clang --std=c99 -g

.PHONY: run_tests
run_tests: build_tests
	unit_tests/string_table
	unit_tests/memory_tracker
	unit_tests/config_data
	unit_tests/json_parser

.PHONY: build_tests
build_tests: unit_tests unit_tests/string_table unit_tests/memory_tracker unit_tests/config_data unit_tests/json_parser

unit_tests:
	mkdir unit_tests

unit_tests/string_table: nf_string_table.c
	$(CC) -DNFST_UNIT_TEST $^ -o $@

unit_tests/memory_tracker: nf_string_table.c nf_memory_tracker.c
	$(CC) -DNFMT_UNIT_TEST $^ -o $@

unit_tests/config_data: nf_config_data.c nf_string_table.c
	$(CC) -DNFCD_UNIT_TEST $^ -o $@

unit_tests/json_parser: nf_json_parser.c nf_config_data.c nf_string_table.c
	$(CC) -DNFJP_UNIT_TEST $^ -o $@

.PHONY: clean
clean:
	rm -rf unit_tests
