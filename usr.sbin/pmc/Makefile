.include <src.opts.mk>

PACKAGE=	pmc
PROG_CXX=	pmc
MAN=	
CWARNFLAGS.gcc+= -Wno-redundant-decls
CFLAGS+= -I${SRCTOP}/lib/libpmcstat

LIBADD=	pmc m ncursesw pmcstat elf

SRCS=	pmc.c pmc_util.c display.cc cmd_pmc_stat.c \
	cmd_pmc_list.c cmd_pmc_filter.cc \
	cmd_pmc_summary.cc cmd_pmc_system.cc \
	cmd_pmc_info.cc cmd_pmc_program.cc \
	cmd_pmc_function.cc \
	cmd_pmc_ibs.cc

.include <bsd.prog.mk>
