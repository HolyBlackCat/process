# Since the library is header-only, this makefile is only for running tests.

# CXX := implicit

OUTPUT_DIR := test/output-$(CXX)

WINDOWS := 0
override WINDOWS := $(filter-out 0,$(WINDOWS))

EXT_EXE := $(if $(WINDOWS),.exe)

EXECUTABLES := file_io redirects simple_io
# Converts a name of one of the text executables (one of `EXECUTABLES`) to a path to it.
override exe_name = $(OUTPUT_DIR)/$1$(EXT_EXE)

FLAGS := -Iinclude -std=c++23 -Wall -Wextra -pedantic-errors -Wconversion -D_GLIBCXX_DEBUG -g

# We use this directory and file for testing IO.
DUMMY_DIR := test/dummy
DUMMY_FILE := $(DUMMY_DIR)/1.txt

override define lf :=
$(strip)
$(strip)
endef
override safe_shell = $(shell $1)$(if $(filter-out 0,$(.SHELLSTATUS)),$(error Command failed with code $(.SHELLSTATUS):$(lf)$1$(lf)))
override safe_shell_exec = $(call,$(call safe_shell,$1))

override quote = '$(subst ','"'"',$1)'

override rm_file = $(call safe_shell_exec,rm -f $(call quote,$(DUMMY_FILE)))



.PHONY: test_all
test_all: test_file_io test_redirects

ifneq ($(WINDOWS),)
EXECUTABLES += win_arg_escaping
test_all: test_win_arg_escaping
endif


.PHONY: test_file_io
test_file_io: DUMMY_DIR := $(OUTPUT_DIR)/test_file_io/dir
test_file_io: DUMMY_FILE := $(DUMMY_DIR)/1.txt
test_file_io: $(call exe_name,file_io)
	# Read:

	# * The file is missing.
	rm -rf $(call quote,$(DUMMY_DIR))
	$< read $(DUMMY_FILE) expect_error $(if $(WINDOWS),'`CreateFileW()` failed: Path not found.','`open()` failed: No such file or directory.')

	# * The file exists.
	mkdir -p $(call quote,$(DUMMY_DIR))
	echo -n blah >$(call quote,$(DUMMY_FILE))
	$< read $(DUMMY_FILE) expect_contents "blah"

	# * Since the test executable reads in blocks of 3 bytes, try giving it an exact multiple of that.
	echo -n blabla >$(call quote,$(DUMMY_FILE))
	$< read $(DUMMY_FILE) expect_contents "blabla"

	# * Now an empty file.
	echo -n '' >$(call quote,$(DUMMY_FILE))
	$< read $(DUMMY_FILE) expect_contents ""

	# * Also try passing zero size to read.
	$< read $(DUMMY_FILE) zero_bytes
	# * This also passes a null pointer as the buffer.
	$< read $(DUMMY_FILE) read_zero_bytes_null

	# Write simple:

	# * The directory is missing.
	rm -rf $(call quote,$(DUMMY_DIR))
	$< write $(DUMMY_FILE) simple expect_error $(if $(WINDOWS),'`CreateFileW()` failed: Path not found.','`open()` failed: No such file or directory.')

	# * The directory exists.
	mkdir -p $(call quote,$(DUMMY_DIR))
	$< write $(DUMMY_FILE) simple value "blah" expect_contents "blah"

	# * Zero bytes.
	$< write $(DUMMY_FILE) simple zero_bytes
	$< write $(DUMMY_FILE) simple read_zero_bytes_null

	# Write with params:

	for params_mode in params params_struct; do \
		for existing in keep overwrite error; do \
			for allow_creating in 0 1; do \
				for flags in "" no_append; do \
					for provide in nothing dir file; do \
						if [[ $$provide != nothing ]]; then \
							mkdir -p $(call quote,$(DUMMY_DIR)); \
							if [[ $$provide == file ]]; then \
								echo -n blahblah >$(DUMMY_FILE); \
							fi \
						else \
							rm -rf $(call quote,$(DUMMY_DIR)); \
						fi; \
						\
						CMD=( $< write $(DUMMY_FILE) $$params_mode existing:$$existing allow_creating:$$allow_creating "$$flags" ); \
						if [[ $$allow_creating == 0 && $$existing == error ]]; then \
							CMD+=( expect_error 'Illegal mode combination for a file: `allow_creating == false && existing == error`.' ); \
						elif [[ $$existing == error && $$provide == file ]]; then \
							CMD+=( expect_error $(if $(WINDOWS),'`CreateFileW()` failed: File exists.','`open()` failed: File exists.') ); \
						elif [[ $$provide == nothing ]]; then \
							CMD+=( expect_error $(if $(WINDOWS),'`CreateFileW()` failed: Path not found.','`open()` failed: No such file or directory.') ); \
						elif [[ $$provide == dir && $$allow_creating == 0 ]]; then \
							CMD+=( expect_error $(if $(WINDOWS),'`CreateFileW()` failed: File not found.','`open()` failed: No such file or directory.') ); \
						elif [[ $$provide != file || $$existing == overwrite ]]; then \
							CMD+=( value "123" expect_contents "123" ); \
						elif [[ $$provide == file && $$flags == no_append ]]; then \
							CMD+=( value "123" expect_contents "123hblah" ); \
						else \
							CMD+=( value "123" expect_contents "blahblah123" ); \
						fi; \
						\
						echo "--- providing $$provide --- $${CMD[@]}"; \
						"$${CMD[@]}"; \
					done \
				done \
			done \
		done \
	done

.PHONY: test_redirects
test_redirects: TEST_DIR := $(OUTPUT_DIR)/test_file_io
test_redirects: stdout := $(TEST_DIR)/stdout.txt
test_redirects: stderr := $(TEST_DIR)/stderr.txt
test_redirects: $(call exe_name,redirects) $(call exe_name,simple_io)
	mkdir -p $(call quote,$(TEST_DIR))

	# Input:

	# * Null input.
	$< null inherit inherit $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(echo 'stdout[]')
	diff $(stderr) <(echo 'stderr[]')

	# * Inherited input.
	echo -n blah | $< inherit inherit inherit $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(echo 'stdout[blah]')
	diff $(stderr) <(echo 'stderr[blah]')

	echo -n bleh >$(TEST_DIR)/in.txt

	# * File name input.
	$< file $(TEST_DIR)/in.txt inherit inherit $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(echo 'stdout[bleh]')
	diff $(stderr) <(echo 'stderr[bleh]')

	# * File stream input.
	echo -n bleh >$(TEST_DIR)/in.txt
	$< file_stream $(TEST_DIR)/in.txt inherit inherit $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(echo 'stdout[bleh]')
	diff $(stderr) <(echo 'stderr[bleh]')

	# Output:

	# * Null:

	#   * Null stdout.
	$< null null inherit $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(true)
	diff $(stderr) <(echo 'stderr[]')
	#   * Null stderr.
	$< null inherit null $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(echo 'stdout[]')
	diff $(stderr) <(true)

	#   * Null stdout with stderr merged.
	$< null null merge $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(true)
	diff $(stderr) <(true)
	#   * Null stderr with stdout merged.
	$< null merge null $(call exe_name,simple_io) >$(stdout) 2>$(stderr)
	diff $(stdout) <(true)
	diff $(stderr) <(true)


	# * File:

	for file_method in file file_stream file_desc; do \
		echo '------ Method:' $$file_method; \
		echo '--- File stdout.'; \
		rm $(TEST_DIR)/out.txt; \
		$< null $$file_method $(TEST_DIR)/out.txt inherit $(call exe_name,simple_io) >$(stdout) 2>$(stderr); \
		diff $(stdout) <(true); \
		diff $(stderr) <(echo 'stderr[]'); \
		diff $(TEST_DIR)/out.txt <(echo 'stdout[]'); \
		\
		echo '--- File stderr.'; \
		rm $(TEST_DIR)/out.txt; \
		$< null inherit $$file_method $(TEST_DIR)/out.txt $(call exe_name,simple_io) >$(stdout) 2>$(stderr); \
		diff $(stdout) <(echo 'stdout[]'); \
		diff $(stderr) <(true); \
		diff $(TEST_DIR)/out.txt <(echo 'stderr[]'); \
		\
		echo '--- File stdout with stderr merged.'; \
		rm $(TEST_DIR)/out.txt; \
		$< null $$file_method $(TEST_DIR)/out.txt merge $(call exe_name,simple_io) >$(stdout) 2>$(stderr); \
		diff $(stdout) <(true); \
		diff $(stderr) <(true); \
		diff $(TEST_DIR)/out.txt <(echo -e 'stdout[]\nstderr[]'); \
		\
		echo '--- File stderr with stdout merged.'; \
		rm $(TEST_DIR)/out.txt; \
		$< null merge $$file_method $(TEST_DIR)/out.txt $(call exe_name,simple_io) >$(stdout) 2>$(stderr); \
		diff $(stdout) <(true); \
		diff $(stderr) <(true); \
		diff $(TEST_DIR)/out.txt <(echo -e 'stdout[]\nstderr[]'); \
	done

ifneq ($(WINDOWS),)
.PHONY: test_win_arg_escaping
test_win_arg_escaping: $(call exe_name,win_arg_escaping)
	$<
endif

# Build the test executables.
$(foreach x,$(EXECUTABLES),$(eval\
$(call exe_name,$x): test/$x.cpp | $(OUTPUT_DIR)/ ; $(CXX) $(FLAGS) $$< -o $$@ \
))

# Create any directory.
%/:
	mkdir -p $@
