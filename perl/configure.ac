AC_INIT(nix-perl, m4_esyscmd([bash -c "echo -n $(cat ../.version)$VERSION_SUFFIX"]))
AC_CONFIG_SRCDIR(MANIFEST)
AC_CONFIG_AUX_DIR(../config)

CFLAGS=
CXXFLAGS=
AC_PROG_CC
AC_PROG_CXX

AC_CANONICAL_HOST

# Use 64-bit file system calls so that we can support files > 2 GiB.
AC_SYS_LARGEFILE

AC_DEFUN([NEED_PROG],
[
AC_PATH_PROG($1, $2)
if test -z "$$1"; then
    AC_MSG_ERROR([$2 is required])
fi
])

NEED_PROG(perl, perl)
NEED_PROG(curl, curl)
NEED_PROG(bzip2, bzip2)
NEED_PROG(xz, xz)

# Test that Perl has the open/fork feature (Perl 5.8.0 and beyond).
AC_MSG_CHECKING([whether Perl is recent enough])
if ! $perl -e 'open(FOO, "-|", "true"); while (<FOO>) { print; }; close FOO or die;'; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([Your Perl version is too old.  Lix requires Perl 5.8.0 or newer.])
fi
AC_MSG_RESULT(yes)


# Figure out where to install Perl modules.
AC_MSG_CHECKING([for the Perl installation prefix])
perlversion=$($perl -e 'use Config; print $Config{version};')
perlarchname=$($perl -e 'use Config; print $Config{archname};')
AC_SUBST(perllibdir, [${libdir}/perl5/site_perl/$perlversion/$perlarchname])
AC_MSG_RESULT($perllibdir)

# Look for libsodium.
PKG_CHECK_MODULES([SODIUM], [libsodium], [CXXFLAGS="$SODIUM_CFLAGS $CXXFLAGS"])

# Check for the required Perl dependencies (DBI and DBD::SQLite).
perlFlags="-I$perllibdir"

AC_ARG_WITH(dbi, AC_HELP_STRING([--with-dbi=PATH],
  [prefix of the Perl DBI library]),
  perlFlags="$perlFlags -I$withval")

AC_ARG_WITH(dbd-sqlite, AC_HELP_STRING([--with-dbd-sqlite=PATH],
  [prefix of the Perl DBD::SQLite library]),
  perlFlags="$perlFlags -I$withval")

AC_MSG_CHECKING([whether DBD::SQLite works])
if ! $perl $perlFlags -e 'use DBI; use DBD::SQLite;' 2>&5; then
    AC_MSG_RESULT(no)
    AC_MSG_FAILURE([The Perl modules DBI and/or DBD::SQLite are missing.])
fi
AC_MSG_RESULT(yes)

AC_SUBST(perlFlags)

PKG_CHECK_MODULES([NIX], [nix-store])

NEED_PROG([NIX], [nix])

# Expand all variables in config.status.
test "$prefix" = NONE && prefix=$ac_default_prefix
test "$exec_prefix" = NONE && exec_prefix='${prefix}'
for name in $ac_subst_vars; do
    declare $name="$(eval echo "${!name}")"
    declare $name="$(eval echo "${!name}")"
    declare $name="$(eval echo "${!name}")"
done

rm -f Makefile.config
ln -sfn ../mk mk

AC_CONFIG_FILES([])
AC_OUTPUT
