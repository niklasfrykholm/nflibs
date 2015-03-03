default: run_tests

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
	clang --std=c11 -g -DNFST_UNIT_TEST nf_string_table.c -o unit_tests/string_table

unit_tests/memory_tracker: nf_string_table.c nf_memory_tracker.c
	clang --std=c11 -g -DNFMT_UNIT_TEST nf_memory_tracker.c nf_string_table.c -o unit_tests/memory_tracker

unit_tests/config_data: nf_config_data.c nf_string_table.c
	clang --std=c11 -g -DNFCD_UNIT_TEST nf_config_data.c nf_string_table.c -o unit_tests/config_data

unit_tests/json_parser: nf_json_parser.c nf_config_data.c
	clang --std=c11 -g -DNFJP_UNIT_TEST nf_config_data.c nf_string_table.c nf_json_parser.c -o unit_tests/json_parser

.PHONY: clean
clean:
	rm -rf unit_tests
