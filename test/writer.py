#!/usr/bin/env python3
""" Command-line tool for writing an arbitrary block of data to anywhere
in a file. """

import argparse
import ast
import os
import sys


def normalize_path(path):
    """ Fully expands and normalizes a path. """

    path = os.path.normpath(path)
    path = os.path.expanduser(path)
    path = os.path.expandvars(path)
    path = os.path.abspath(path)
    path = os.path.realpath(path)
    return path


def normalize_int(number):
    """ Normalizes a number to an integer value. Interprets hex/octal/binary,
    and also interprets size suffixes (k,m,g,t). Use of 'eval' is a little
    risky maybe, but a best attempt is made to sanitize it. """

    orig = number

    if isinstance(number, int):
        return number

    if isinstance(number, bytes):
        number = number.decode()

    number = number.strip().lower()

    if number[1:2] in ['o', 'x', 'b']:
        return ast.literal_eval(number)

    number = number.replace("b", "")
    number = number.replace("i", "")
    number = number.replace("k", "*(1024**1)")
    number = number.replace("m", "*(1024**2)")
    number = number.replace("g", "*(1024**3)")
    number = number.replace("t", "*(1024**4)")

    try:
        # pylint: disable=eval-used; eval has been neutered here, and
        # should be OK.
        number = eval(number.replace("_", ""), {'__builtins__': {}}, {})
    except SyntaxError:
        err_msg = "Error: couldn't parse number [%s]\n" % orig
        sys.stderr.write(err_msg)
        sys.exit(1)

    return int(number)


def create_parser():
    """ Creates an argparse parser for the command-line application. """

    description = """Command-line utility for wirting bytes to an arbitrary
    place in a file."""

    parser = argparse.ArgumentParser(description=description)

    parser.add_argument('-o', '--offset', dest='offset', default="0",
                        help="Starting offset into file (default: 0)")

    parser.add_argument('-i', '--insane', dest='insane', action="store_true",
                        help="Skip all offset/filesize checks")

    parser.add_argument('outfile', metavar="OUTFILE", help="Output file to " +
                        "write. Must already exist, and be large enough to " +
                        "handle input data.")

    return parser


def parse_args(parser):
    """ Runs argument-parser and applies any post-parsing operations. """

    args = parser.parse_args()

    args.offset = normalize_int(args.offset)
    args.outfile = normalize_path(args.outfile)

    if not os.path.isfile(args.outfile):
        sys.stderr.write("Error: couldn't find file [%s]\n" % args.outfile)
        sys.exit(1)

    filesize = os.path.getsize(args.outfile)

    if (args.insane is False) and (args.offset > filesize):
        err_msg = "Error: offset [%d] is too big for file [%s]\n"
        err_msg %= (args.offset, args.outfile)
        sys.stderr.write(err_msg)
        sys.exit(1)

    return args


def main():
    """ Main routine of program. Makes a parser, runs it, and prints the
    results. """

    parser = create_parser()
    args = parse_args(parser)

    filesize = os.path.getsize(args.outfile)

    data = sys.stdin.buffer.read()

    if (args.insane is False) and ((args.offset + len(data)) > filesize):
        err_msg = "Error: data would extend past the end of file [%s]\n"
        err_msg %= args.outfile
        sys.stderr.write(err_msg)
        sys.exit(1)

    with open(args.outfile, 'r+b') as outfile:
        outfile.seek(args.offset)
        outfile.write(data)

if __name__ == "__main__":
    main()
