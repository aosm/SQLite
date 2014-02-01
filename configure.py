#!/usr/bin/python

import sys
import string
import os

def print_usage(): 
    print 'configure.py [-help] [-debug] [-cflags="..."]'

# these configure and cflags arguments must be manually sychronized to the B&I Makefile contents
normal_args = ['--enable-threadsafe']
normal_cflags = "-DSQLITE_ENABLE_LOCKING_STYLE -O3 -DUSE_PREAD -fno-strict-aliasing -DSQLITE_MAX_LENGTH=2147483647 -DSQLITE_MAX_SQL_LENGTH=3000000 -DSQLITE_MAX_VARIABLE_NUMBER=500000"

debug_args = ['--enable-threadsafe', '--enable-debug']
debug_cflags = "-DSQLITE_ENABLE_LOCKING_STYLE -O3 -DUSE_PREAD -fno-strict-aliasing -g -DSQLITE_MAX_LENGTH=2147483647 -DSQLITE_MAX_SQL_LENGTH=3000000 -DSQLITE_MAX_VARIABLE_NUMBER=500000"
add_cflags = ""

args = normal_args
cflags = normal_cflags

if len(sys.argv) > 1:
    for arg in sys.argv[1:]:
        if arg[:2] == "-h":
            print_usage()
            sys.exit(0)
        elif arg == "-debug":
            args = debug_args
            cflags = debug_cflags
            print "Invoking debug Mac OS X sqlite configure command"
        elif arg[:8] == "-cflags=":
            add_cflags = arg[8:]
            print "Invoking sqlite configure command with added cflags: " + add_cflags
        else:
            print "Unexpected argument: %s" % (arg)
            print_usage()
            sys.exit(1)
else:
    print "Invoking standard Mac OS X sqlite configure command"    

mypath = sys.argv[0]
if mypath[-12:] != "configure.py":
    print "Unrecognized command invocation, can't extract path"
    print_usage()
    sys.exit(1)

if add_cflags:
    cflags = cflags + " " + add_cflags
    
command_string = mypath[:-12] + "public_source/configure " + str(string.join(args, " ")) + " CFLAGS='" + cflags + "'"
print command_string

cmd = mypath[:-12] + "public_source/configure"

#print "command: " + cmd
#print "args: " + str(args)

os.system(command_string)
