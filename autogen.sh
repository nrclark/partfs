#!/bin/sh
set -e
autoreconf -i --force
automake --add-missing
