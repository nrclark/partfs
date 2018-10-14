#!/usr/bin/env python3
""" Command-line tool for reading an arbitrary block of data from anywhere
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

    description = """Command-line utility for reading bytes from arbitrary
    ranges in a file."""

    parser = argparse.ArgumentParser(description=description)

    parser.add_argument('-n', '--no-newline', dest='nonl', action="store_true",
                        help="Don't print a newline after requested data.")

    parser.add_argument('-o', '--offset', dest='offset', default="0",
                        help="Starting offset into file (default: 0)")

    parser.add_argument('-c', '--count', dest='count', default=None,
                        help="Number of bytes to read from file. If not " +
                        "specified, all bytes from [offset] to end-of-file " +
                        "are read.")

    parser.add_argument('-x', '--hexdump', dest='hexdump', action="store_true",
                        help="Print output in hexadecimal")

    parser.add_argument('infile', metavar="INFILE", help="Input file to read")

    return parser


def parse_args(parser):
    """ Runs argument-parser and applies any post-parsing operations. """

    args = parser.parse_args()

    args.offset = normalize_int(args.offset)

    if args.count is not None:
        args.count = normalize_int(args.count)

    args.infile = normalize_path(args.infile)

    if not os.path.isfile(args.infile):
        sys.stderr.write("Error: couldn't find file [%s]\n" % args.infile)
        sys.exit(1)

    filesize = os.path.getsize(args.infile)

    if args.count is None:
        args.count = filesize

    if args.offset > filesize:
        err_msg = "Error: offset [%d] is too big for file [%s]\n"
        err_msg %= (args.offset, args.infile)
        sys.stderr.write(err_msg)
        sys.exit(1)

    if (args.offset + args.count) > filesize:
        err_msg = "Error: count [%d] extends past the end of file [%s]\n"
        err_msg %= (args.count, args.infile)
        sys.stderr.write(err_msg)
        sys.exit(1)

    return args


def main():
    """ Main routine of program. Makes a parser, runs it, and prints the
    results. """

    parser = create_parser()
    args = parse_args(parser)

    with open(args.infile, 'rb') as infile:
        infile.seek(args.offset)
        data = infile.read(args.count)

    if args.hexdump:
        hexdump = ''.join([hex(x)[2:].zfill(2).upper() for x in data])
        data = bytes([ord(x) for x in hexdump])

    sys.stdout.buffer.write(data)

    if chr(data[-1]) != '\n' and args.nonl is False:
        sys.stdout.buffer.write(bytes([ord('\n')]))

if __name__ == "__main__":
    main()
