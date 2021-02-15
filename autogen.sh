#!/bin/sh
set -e

autoreconf -i --force --verbose
automake --add-missing
