#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([DADA],[1.0],[straten@astron.nl])

AC_CONFIG_SRCDIR([src/dada_def.h])
AC_CONFIG_MACRO_DIR([config])
AC_CANONICAL_TARGET

AC_ARG_VAR([PSRHOME],    [Standard pulsar home directory])
AC_ARG_VAR([LOGIN_ARCH], [Architecture-dependent sub-directory of PSRHOME])
AC_ARG_VAR([PACKAGES],   [Root directory of third-party packages])
AC_ARG_VAR([PGPLOT_DIR], [Pgplot graphics library directory])

AC_PREFIX_DEFAULT([${PSRHOME:-"/usr/local"}${PSRHOME:+"/$LOGIN_ARCH"}])

AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_DISABLE_SHARED

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL

ACX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"

# 
# Set up to use the fink package installation directory
#
if test -d /sw; then
  CPPFLAGS="-I/sw/include $CPPFLAGS"
  LDFLAGS="-L/sw/lib $LDFLAGS"
else
  #
  # Set up to use the standard package installation directory
  #
  if test -d /usr/local; then
    CPPFLAGS="-I/usr/local/include $CPPFLAGS"
    LDFLAGS="-L/usr/local/lib $LDFLAGS"
  fi
fi

SWIN_LIB_PGPLOT
SWIN_LIB_CUDA

AX_OPENMP
SWIN_LIB_FFTW
SWIN_LIB_PGPLOT
SWIN_LIB_IPP
SWIN_LIB_MKL
SWIN_LIB_IPP
SWIN_LIB_GSL
SWIN_LIB_RDMACM
SWIN_LIB_SOFA
SWIN_LIB_HWLOC
SWIN_LIB_XGPU

AC_CHECK_LIB([m], [sin])

#
# Checks for header files.
#
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h netdb.h netinet/in.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h sys/statvfs.h sys/vfs.h sys/mount.h syslog.h unistd.h])

#
# Checks for typedefs, structures, and compiler characteristics.
#
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

#
# Checks for library functions.
#
SWIN_FUNC_AFFINITY
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_SETVBUF_REVERSED
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gethostbyname gethostname gettimeofday inet_ntoa memset select socket strchr strcspn strdup strerror strstr])

if test "x$prefix" != xNONE; then
  PSRDADA_INSTALL=$prefix
else
  PSRDADA_INSTALL=$ac_default_prefix
fi

AC_SUBST(PSRDADA_INSTALL)

AM_CONFIG_HEADER([config.h])

AC_CONFIG_FILES([Makefile
		Management/Makefile
		Management/psrdada_ldflags
		Management/psrdada_cflags
		src/Makefile
		scripts/Makefile
    web/Makefile
    config/Makefile
    apsr/Makefile
		apsr/src/Makefile
		apsr/scripts/Makefile
		apsr/web/Makefile
		apsr/config/Makefile
		apsr/config/apsr.cfg
    bpsr/Makefile
		bpsr/src/Makefile
		bpsr/mon/Makefile
		bpsr/rfi/Makefile
		bpsr/swinproc/Makefile
		bpsr/swinproc/dedisp/Makefile
		bpsr/fil_edit/Makefile
		bpsr/scripts/Makefile
		bpsr/web/Makefile
		bpsr/config/Makefile
		bpsr/config/bpsr.cfg
    caspsr/Makefile
    caspsr/src/Makefile
    caspsr/scripts/Makefile
    caspsr/config/Makefile
    caspsr/config/caspsr.cfg
    caspsr/web/Makefile
    comap/Makefile
    comap/src/Makefile
    examples/Makefile
    examples/src/Makefile
    gmrt/Makefile
    gmrt/src/Makefile
    leda/Makefile
    leda/src/Makefile
    mopsr/Makefile
    mopsr/scripts/Makefile
    mopsr/config/Makefile
    mopsr/config/mopsr.cfg
    mopsr/src/Makefile
    mopsr/src/sigproc/Makefile
    mopsr/sla/Makefile
    mopsr/web/Makefile
    hispec/Makefile
    hispec/config/Makefile
    hispec/config/hispec.cfg
    hispec/scripts/Makefile
    hispec/src/Makefile
    hispec/web/Makefile
	psrix/Makefile	
    ska1/Makefile
    ska1/src/Makefile
		3rdparty/Makefile])

case $target_os in
*linux*) AM_CONDITIONAL([HAVE_LINUX],[true]);;
*) AM_CONDITIONAL([HAVE_LINUX],[false]);;
esac

#		puma2/Makefile # Removed since tim.h is missing
AC_OUTPUT

