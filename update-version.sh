#!/bin/sh
CURRENT=`git describe --abbrev=5 --dirty --always --tags`
CURRENT="#define GIT_VERSION \""$CURRENT"\""
OLD=`cat git-version.h 2>/dev/null`
if [ x"$CURRENT" != x"$OLD" ] ; then
	echo $CURRENT > git-version.h
fi
