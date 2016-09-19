dnl Process this file with autoconf to produce a configure script
AC_INIT(ar-t6-firmware,[0.1.0],mkschreder.uk@gmail.com)
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_SRCDIR(./firmware/)
AC_CONFIG_MACRO_DIR([m4])

m4_pattern_allow([AM_DEFAULT_VERBOSITY])

AM_INIT_AUTOMAKE

LT_INIT

AC_PROG_CC
AM_PROG_AS

AC_OUTPUT(Makefile firmware/Makefile firmware/peripherals/Makefile firmware/peripherals/src/Makefile firmware/system/Makefile)

