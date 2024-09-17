project(mailz LANGUAGES C)
cmake_minimum_required(VERSION 3.25) # arbitrary

include(CheckFunctionExists)

add_compile_options(-isystem ${CMAKE_SOURCE_DIR}/compat -D_GNU_SOURCE)
add_compile_options(-I${CMAKE_SOURCE_DIR}) # for yacc and lex

MACRO(REPLACE_FUNCTIONS sources)
	FOREACH(name ${ARGN})
		STRING(TOUPPER have_${name} SYMBOL_NAME)
		CHECK_FUNCTION_EXISTS(${name} ${SYMBOL_NAME})
		IF(NOT ${SYMBOL_NAME})
			SET(${sources} ${${sources}} compat/${name}.c)
		ENDIF(NOT ${SYMBOL_NAME})
	ENDFOREACH(name)
ENDMACRO(REPLACE_FUNCTIONS)

REPLACE_FUNCTIONS(COMPAT_SOURCES errc freezero getdtablecount strtonum verrc)

set(COMPAT_SOURCES ${COMPAT_SOURCES} compat/imsg.c compat/imsg-buffer.c)

add_executable(maildir-extract date.c from.c header.c ibuf-util.c
	maildir-extract.c ${COMPAT_SOURCES})

add_executable(maildir-read-letter charset.c content-type.c 
	encoding.c header.c ibuf-util.c imsg-sync.c maildir-read-letter.c 
	string-util.c ${COMPAT_SOURCES})
target_link_libraries(maildir-read-letter resolv)

add_custom_command(OUTPUT parse.c 
	COMMAND yacc -do parse.c ${CMAKE_SOURCE_DIR}/parse.y
	DEPENDS parse.y)

add_custom_command(OUTPUT lex.c
	COMMAND lex -o lex.c ${CMAKE_SOURCE_DIR}/lex.l
	DEPENDS lex.l)

add_executable(mailz cache.c commands.c date.c extract.c ibuf-util.c 
	imsg-sync.c letter.c lex.c list.c maildir.c mailz.c mark.c
	parse.c print.c printable.c read-letter.c read-letters.c send.c
	setup.c ${COMPAT_SOURCES})

add_executable(regress cache.c charset.c content-type.c date.c 
	encoding.c extract.c from.c header.c ibuf-util.c imsg-sync.c 
	letter.c lex.c maildir.c parse.c printable.c read-letter.c 
	read-letters.c regress.c string-util.c ${COMPAT_SOURCES})
target_link_libraries(regress resolv)
target_compile_options(regress PRIVATE -DREGRESS)
