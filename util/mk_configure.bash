#!/bin/bash
mk_header() {
	echo "# $1"
	printf "\n# %s\nprintf \"\\\n# %s\\\n\" >> Makefile\n" "$1" "$1" >> configure
}

do_expand() {
  if [ -z "$(echo "$1" | cut -d ' ' -f2)" ]
  then
    key=$(echo "$1" | cut -d ":" -f 1)
	printf "%s=%s\nfor iter in \$%s\n" "$key" "$key" "$key"
  else
	printf "for iter in"
	for iter in $1
	do
		key=$(echo "$iter" | cut -d ":" -f 1)
		printf " %s" "$key"
	done
  fi
  printf "\ndo\n\t"
}

do_expand2() {
  if [ -z "$(echo "$1" | cut -d ' ' -f2)" ]
  then
    key=$(echo "$1" | cut -d ":" -f 1)
	printf "%s=%s\nfor iter in \$%s\n" "$key" "$key" "$key"
  else
	printf "for iter in"
	for iter in $1
	do printf " %s" "$iter"
	done
  fi
  printf "\ndo\n\tkey=\$(echo \"\$iter\" | cut -d ':' -f 1)\n\tval=\$(echo \"\$iter\" | cut -d ':' -f 2)\n\tlib=\$(echo \"\$iter\" | cut -d ':' -f 4)\n\t"
}

set -e
[ -f configure ] && rm configure
[ -f Makefile  ] && rm Makefile
while read -r line
do
  line=$(echo "$line" | cut -d"#" -f1)
  [ -z "$line" ] && continue
  category=$(echo "$line" | cut -d ":" -f1 | xargs)
  long=$(    echo "$line" | cut -d ":" -f2 | xargs)
#  short=$(   echo "$line" | cut -d ":" -f3 | xargs)
#  help=$(    echo "$line" | cut -d ":" -f4 | xargs)
  default=$( echo "$line" | cut -d ":" -f5)

  inc=$( echo "$line" | cut -d ":" -f6)
  lib=$( echo "$line" | cut -d ":" -f7)
  if [ "$category" = "LIB"  ]
  then
    echo "# ARG_OPTIONAL_BOOLEAN([$long],, [enable $long], [$default])"
    echo "# ARG_OPTIONAL_REPEATED([${long}-inc], , [Directories where to look for include files for $long], [$inc])"
    echo "# ARG_OPTIONAL_SINGLE([$long-lib], , [${long} library], [$lib])"
    inc=$( echo "$inc" | tr " " ",")
    LIB+="$long:$default:$inc:$lib "
  elif [ "$category" = "OPT" ]
  then
#    default=$(echo "$default" | sed 's/ /\\ /g')
    default=${default/ /\\ /}
    OPT+="$long:\"${default}\" "
    echo "# ARG_OPTIONAL_SINGLE([$long], , [value for ${long~~}], [$default])"
  elif [ "$category" = "DIR" ]
  then
    echo "# ARG_OPTIONAL_SINGLE([$long], , [$long path], [$default])"
    DIR+="${long}:$default "
  elif [ "$category" = "DBG" ]
  then
    DBG+="$long:$default "
    echo "# ARG_OPTIONAL_BOOLEAN([debug-${long}], , [debug $long], [$default])"
  elif [ "$category" = "USE" ]
  then
    USE+="$long:$default "
    echo "# ARG_OPTIONAL_BOOLEAN([${long}], , [debug $long], [$default])"
  elif [ "$category" = "DEF" ]
  then
    DEF+="$long:$default "
    echo "# ARG_OPTIONAL_SINGLE([${long}], , [debug $long], [$default])"
 fi
done < "$1" >> configure

cat << EOF >> configure
# ARG_HELP([The general script's help msg])
# ARGBASH_GO

EOF

argbash configure -o configure
#exit
# remove footer
head -n -1 configure > /tmp/_test.argbash
mv /tmp/_test.argbash configure

#mk_header "prepare configure" # needs other func
for iter in $OPT
do
  key=$(echo "${iter}" | cut -d ":" -f 1)
  sed -i "s/^_arg_${key}=\(.*\)/: \"\${${key~~}:=\1}\"\n: \"\$\{_arg_${key}:=\$${key~~}\}\"/" configure
done

for iter in $LIB
do
  key=$(echo "${iter}" | cut -d ":" -f 1)
  sed -i "s/^_arg_${key}=\(.*\)/: \"\${${key~~}_D:=\1}\"\n: \"\$\{_arg_${key}:=\$${key~~}_D\}\"/" configure
  sed -i "s/^_arg_${key}_inc=(\(.*\))/: \"\${${key~~}_INC:=\1}\"\n: \"\$\{_arg_${key}_inc:=\$${key~~}_INC\}\"/" configure
  sed -i "s/^_arg_${key}_lib=\(\(.*\)\)/: \"\${${key~~}_LIB:=\1}\"\n: \"\$\{_arg_${key}_lib:=\$${key~~}_LIB\}\"/" configure
done

for dir in $DIR
do
  key=$(echo "${dir}" | cut -d ":" -f 1)
  sed -i "s/^_arg_${key}=\(.*\)/: \"\${${key~~}_DIR:=\1}\"\n: \"\$\{_arg_${key}:=\$${key~~}_DIR\}\"/" configure
done

for iter in $DBG
do
  key=$(echo "${iter}" | cut -d ":" -f 1)
  sed -i "s/^_arg_debug_${key}=\(.*\)/: \"\${DEBUG_${key~~}:=\1}\"\n: \"\$\{_arg_debug_${key}:=\$DEBUG_${key~~}\}\"/" configure
done

for iter in $USE
do
  key=$(echo "${iter}" | cut -d ":" -f 1)
  val=$(echo "${iter}" | cut -d ":" -f 2)
  sed -i "s/^_arg_${key}=\(.*\)/: \"\${USE_${key~~}:=${val}}\"\n: \"\$\{_arg_${key}:=\$USE_${key~~}\}\"/" configure
done

for iter in $DEF
do
  key=$(echo "${iter}" | cut -d ":" -f 1)
  val=$(echo "${iter}" | cut -d ":" -f 2)
  sed -i "s/^_arg_${key}=\(.*\)/: \"\${${key~~}:=${val}}\"\n: \"\$\{_arg_${key}:=\$${key~~}\}\"/" configure
done

{
	echo "set -e"
	echo -e "\n# remove Makefile\n[ -f Makefile  ] && rm Makefile"
	# check default driver
	echo -e "\n# check default driver"
	printf "VALID_DRIVER=\""
	for iter in $LIB
	do
		key=$(echo "${iter}" | cut -d ":" -f 1)
		printf " %s" "$key"
	done
	echo -e "\"\ngrep \"\$_arg_d_func\" <<<  \"\$VALID_DRIVER\" > /dev/null || { echo \"invalid default driver\";exit 1; }\n"

	echo "if [ \"\$_arg_double\" = \"on\" ]; then _CFLAGS+=\" -DUSE_DOUBLE -DSPFLOAT=double\";fi"
	echo "if [ \"\$_arg_double\" = \"1\"  ];then _CFLAGS+=\" -DUSE_DOUBLE -DSPFLOAT=double\";fi"
	echo "([ \"\$_arg_double\" = \"on\" ] || [ \"\$_arg_double\" = \"1\"  ]) || _CFLAGS+=\" -DSPFLOAT=float\""
	# build with coverage for now
	echo "if [ \"\$USE_COVERAGE\" = \"1\"  ]; then _CFLAGS+=\" -ftest-coverage -fprofile-arcs --coverage\";fi"
	echo "if [ \"\$USE_COVERAGE\" = \"on\" ]; then _CFLAGS+=\" -ftest-coverage -fprofile-arcs --coverage\";fi"
	echo "if [ \"\$_arg_soundpipe_inc\" ]; then _CFLAGS+=\" \$_arg_soundpipe_inc\";fi"
#	echo "\$_arg_cc -Iinclude -DDEBUG \$_CFLAGS util/generate_header.c core/err_msg.c -o util/generate_header || (echo 'invalid compilation options'; exit 1;)"
	echo "cmd=\"\$_arg_cc -Iinclude -DDEBUG \$_CFLAGS util/generate_header.c core/err_msg.c -o util/generate_header\""
	echo "eval \"\$cmd\" || (echo 'invalid compilation options'; exit 1;)"

	# generate header
	echo "./util/generate_header || exit 1"
} >> configure


############
# Makefile #
############
echo "echo \"# generated by ./configure\" >> Makefile" >> configure
mk_header "handle base options"
{
  do_expand "$OPT"
  printf "arg=\"_arg_\${iter}\"\n\techo \"\${iter~~} ?= \${!arg}\"\ndone >> Makefile\n"
} >> configure
#cat << _EOF >> configure

#cat <<-  EOF >> Makefile
{
	echo " echo -e \"
# base flags
LDFLAGS += -lm -ldl -rdynamic -lpthread
CFLAGS += -Iinclude -std=c99 -O3 -mfpmath=sse -mtune=native -fno-strict-aliasing -Wall -pedantic -D_GNU_SOURCE\" >> Makefile"
} >> configure
#EOF"

#_EOF

mk_header "handle boolean options"
{
  do_expand "$USE"
  printf "arg=\"_arg_\$iter\"\n\tif [ \"\$iter\" = \"double\" ]\n\tthen echo \"USE_\${iter~~}  = \${!arg}\"\n"
  printf "\telse echo \"USE_\${iter~~} ?= \${!arg}\"\n\tfi\ndone >> Makefile\n"
} >> configure

mk_header "handle definitions"
{
  do_expand "$DEF"
  printf "arg=\"_arg_\${iter}\"\n\techo \"\${iter~~} ?= \${!arg}_driver\"\ndone >> Makefile;\n"
} >> configure

mk_header "handle directories"
{
  do_expand2 "$DIR"
  printf "echo \"GWION_\${key~~}_DIR ?= \\\${PREFIX}/lib/Gwion/\${val}\"\ndone >> Makefile"
} >> configure

mk_header "handle libraries"
{
  do_expand "$LIB"
  printf "arg=\"_arg_\$iter\"\n\techo \"\${iter~~}_D ?= \${!arg}\"\ndone >> Makefile\n"
} >> configure

mk_header "handle debug"
{
  do_expand2 "$DBG"
  printf "arg=\"_arg_debug_\$key\"\n\techo \"DEBUG_\${key~~} ?= \${!arg}\"\ndone >> Makefile\n"
} >> configure

mk_header "initialize source lists"
{
  do_expand "core lang ugen eval"
  printf "echo \"\${iter}_src := \\\$(wildcard \${iter}/*.c)\"\ndone >> Makefile\necho \"drvr_src := drvr/driver.c\" >> Makefile\n"
} >> configure

mk_header "add libraries"
{
  do_expand2 "$LIB"
  printf "if [ \"\${val}\" = \"on\" ]\n\tthen val=1\n\telse val=0\n\tfi\n"
  cat << EOF
	[ -z "\$lib" ] && printf "ifeq (\\\${%s_D}, on)\\\nCFLAGS += -DHAVE_%s\\\ndrvr_src += drvr/%s.c\\\nelse ifeq (\\\${%s_D}, 1)\\\nCFLAGS +=-DHAVE_%s\\\ndrvr_src +=drvr/%s.c\\\nendif\\\n" "\${key~~}" "\${key~~}" "\${key}" "\${key~~}" "\${key~~}" "\${key}"
	[ -z "\$lib" ] || printf "ifeq (\\\${%s_D}, on)\\\nLDFLAGS += %s\\\nCFLAGS += -DHAVE_%s\\\ndrvr_src += drvr/%s.c\\\nelse ifeq (\\\${%s_D}, 1)\\\nLDFLAGS += %s\\\nCFLAGS +=-DHAVE_%s\\\ndrvr_src +=drvr/%s.c\\\nendif\\\n" "\${key~~}" "\${lib}" "\${key~~}" "\${key}" "\${key~~}" "\${lib}" "\${key~~}" "\${key}"
	done >> Makefile
EOF
} >> configure

mk_header "add boolean"
{
  do_expand2 "$USE"
  cat << EOF
if [ "\${val}" = "on" ]
then [ "\$key" = "double" ] && val=double;
else [ "\$key" = "double" ] && val=float;
fi
[ "\$key" = "memcheck" ] && printf "ifeq (\\\${USE_%s}, on)\\\nCFLAGS += -g\\\nelse " "\${key~~}"
[ "\$key" = "memcheck" ] && printf "ifeq (\\\${USE_%s}, 1)\\\nCFLAGS += -g\\\nendif\n" "\${key~~}"
[ "\$key" = "coverage" ] && printf "ifeq (\\\${USE_%s}, on)\\\nCFLAGS += -ftest-coverage -fprofile-arcs\\\nelse " "\${key~~}"
[ "\$key" = "coverage" ] && printf "ifeq (\\\${USE_%s}, 1)\\\nCFLAGS += -ftest-coverage -fprofile-arcs\\\\nendif\n" "\${key~~}"
[ "\$key" = "coverage" ] && printf "ifeq (\\\${USE_%s}, on)\\\nLDFLAGS += --coverage\nelse " "\${key~~}"
[ "\$key" = "coverage" ] && printf "ifeq (\\\${USE_%s}, 1)\\\nLDFLAGS += --coverage\nendif\n" "\${key~~}"
done >> Makefile
key="double"
printf "ifeq (\\\${USE_%s}, on)\\\nCFLAGS += -DUSE_%s} -DSPFLOAT=double\\\nelse ifeq (\\\${USE_%s}, 1)\\\nCFLAGS +=-DUSE_%s -DSPFLOAT=double\\\nelse\\\nCFLAGS+=-DSPFLOAT=float\\\nendif\\\n" "\${key~~}" "\${key~~}" "\${key~~}" "\${key~~}" >> Makefile
EOF
} >> configure

mk_header "add definitions"
{
  do_expand2 "$DEF"
  printf "echo \"CFLAGS+= -D\${key~~}=\\\${\${key~~}}\"\ndone >> Makefile\n"
} >> configure

mk_header "add directories"
{
  do_expand "$DIR"
  printf "printf \"CFLAGS+=-DGWION_%%s_DIR=%q\\\${GWION_%%s_DIR}%q\\\n\" \"\${iter~~}\" \"\${iter~~}\"\ndone >> Makefile\n" '\\\"' '\\\"'
} >> configure

mk_header "add debug flags"
{
  do_expand "$DBG"
  printf "printf \"ifeq (\\\${DEBUG_%%s}, on)\\\nCFLAGS += -DDEBUG_%%s\\\nelse \" \"\${iter~~}\" \"\${iter~~}\"\n"
  printf "\tprintf \"ifeq (\\\${DEBUG_%%s},  1)\\\nCFLAGS += -DDEBUG_%%s\\\nendif\\\n\" \"\${iter~~}\" \"\${iter~~}\"\ndone >> Makefile\n"
} >> configure

mk_header "add soundpipe"
{
  echo "echo \"LDFLAGS+=\${SOUNDPIPE_LIB}\" >> Makefile"
  echo "echo \"CFLAGS+=\${SOUNDPIPE_INC}\"  >> Makefile"
} >> configure

mk_header "initialize object lists"
{
  do_expand "core lang ugen eval drvr"
  printf "echo \"\${iter}_obj := \\\$(\${iter}_src:.c=.o)\"\ndone >> Makefile\n"
} >> configure

cat << _EOF >> configure
###########
# recipes #
###########
cat << EOF >> Makefile

# if any debug flag is set, we need -DDEBUG
ifeq (\\\$(findstring DEBUG,\\\$(CFLAGS)), DEBUG)
DEBUG = 1
endif

ifeq (\\\${DEBUG}, 1)
CFLAGS+=-DDEBUG
endif

LDFLAGS+=-lsndfile

# os specific
ifeq (\\\$(shell uname), Linux)
LDFLAGS+=-lrt
endif

# recipes
all: \\\${core_obj} \\\${lang_obj} \\\${eval_obj} \\\${ugen_obj} \\\${drvr_obj}
	\\\${CC} \\\${core_obj} \\\${lang_obj} \\\${eval_obj} \\\${ugen_obj} \\\${drvr_obj} \\\${LDFLAGS} -o \\\${PRG}

clean:
	@rm -f */*.o \${PRG}

.c.o:
	\\\${CC} \\\${CFLAGS} -c \\\$< -o \\\$(<:.c=.o)

install: directories
	cp \\\${PRG} \\\${PREFIX}

uninstall:
	rm \\\${PREFIX}/\\\${PRG}

test:
	@bash -c "source util/test.sh; do_test examples tests/error tests/tree tests/sh tests/bug | consummer"

parser:
	\\\${YACC} -o eval/parser.c -dv eval/gwion.y -x

lexer:
	\\\${LEX}  -o eval/lexer.c eval/gwion.l

directories:
	mkdir -p \\\${PREFIX}
	mkdir -p \\\${GWION_API_DIR} \\\${GWION_DOC_DIR} \\\${GWION_TAG_DIR} \\\${GWION_TOK_DIR} \\\${GWION_ADD_DIR}
EOF

# ] <-- needed because of Argbash
_EOF


sed -i "s/'\"\(.*\)\"'/'\1'/" configure
sed -i "s/\"\"\(.*\)\"\"/'\1'/" configure

printf "%s\n%s\n" "#!/bin/bash" "$(cat configure)" > configure
chmod +x configure
