#!/bin/sh
set -e

autopoint --force
autoreconf -i --force --verbose
automake --add-missing
