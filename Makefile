# Since the library is header-only, this makefile is only for running tests.

# CXX := implicit

OUTPUT_DIR := test/output

EXECUTABLES := file_io

WINDOWS := 0
override WINDOWS := $(filter-out 0,$(WINDOWS))

EXT_EXE := $(if $(WINDOWS),.exe)

FLAGS := -Iinclude -std=c++23 -Wall -Wextra -pedantic-errors -Wconversion -D_GLIBCXX_DEBUG -g

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

.PHONY: test_file_io
test_file_io: $(OUTPUT_DIR)/file_io$(EXT_EXE)
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



# Build the test executables.
$(foreach x,$(EXECUTABLES),$(eval\
$(OUTPUT_DIR)/$x$(EXT_EXE): test/$x.cpp | $(OUTPUT_DIR)/ ; $(CXX) $(FLAGS) $$< -o $$@ \
))

# Create any directory.
%/:
	mkdir -p $@
