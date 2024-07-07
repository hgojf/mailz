#This is arbitrary, it is just the version I have on my machine
cmake_minimum_required(VERSION 3.25)

project(mailz LANGUAGES C)

include(CheckIncludeFiles)
include(CheckSymbolExists)

# function name, header it should be in, target name, replacement location
function(replace_function)
	check_symbol_exists(${ARGV0} ${ARGV1} HAVE_${ARGV0})
	if (NOT HAVE_${ARGV0})
		target_sources(${ARGV2} PRIVATE ${ARGV3})
	endif()
endfunction()

add_executable(mailz address.c mail.c mailbox.c sendmail.c)
target_compile_options(mailz PRIVATE -include config.h)
target_include_directories(mailz SYSTEM PRIVATE compat)

replace_function(reallocarray stdlib.h mailz compat/reallocarray.c)
replace_function(strsep string.h mailz compat/strsep.c)
replace_function(strlcpy string.h mailz compat/strlcpy.c)
replace_function(strtonum stdlib.h mailz compat/strtonum.c)

check_symbol_exists(flock sys/file.h HAVE_FLOCK_SYS_FILE_H)

check_symbol_exists(pledge unistd.h HAVE_PLEDGE)
check_symbol_exists(unveil unistd.h HAVE_UNVEIL)
check_include_files(sys/tree.h HAVE_SYS_TREE_H)

configure_file(config.h.in config.h @ONLY)
