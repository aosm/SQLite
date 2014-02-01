#!/usr/bin/python

import sys
import string
import os

def print_usage(): 
    print 'configure.py [-help] [-debug] [-pagesize] [-nolimits] [-replay] [-purgeable] [-preferproxy] [-oldafp] [-windows] [-cflags="..."]'

mypath = sys.argv[0]
if mypath[-12:] != "configure.py":
    print "Unrecognized command invocation, can't extract path"
    print_usage()
    sys.exit(1)

srcroot = mypath[:-20] 

# many unit tests blow up spectacularly if the default page size is changed
# if you want the new default pass '-pagesize' to configure.py
pagesize_cflags = " -DSQLITE_DEFAULT_PAGE_SIZE=4096"

# no need to include "-D_HAVE_SQLITE_CONFIG_H" 
common_cflags = "-DSQLITE_THREAD_OVERRIDE_LOCK=-1 " + \
                  "-DSQLITE_OMIT_LOAD_EXTENSION=1 " + \
                  "-DSQLITE_DEFAULT_MEMSTATUS=0 " + \
                  "-DSQLITE_THREADSAFE=2 "

macosx_cflags = "-DSQLITE_OS_UNIX=1 " + \
                     "-DSQLITE_ENABLE_LOCKING_STYLE=1 " + \
                     "-DUSE_PREAD=1 "

opt_cflags         = "-O3 "

limits_cflags      = "-DSQLITE_DEFAULT_CACHE_SIZE=1000 " + \
                     "-DSQLITE_MAX_LENGTH=2147483645 " + \
                     "-DSQLITE_MAX_VARIABLE_NUMBER=500000 "


extension_cflags = "-DSQLITE_ENABLE_RTREE=1 " + \
                     "-DSQLITE_ENABLE_FTS3=1 " + \
                     "-DSQLITE_ENABLE_FTS3_PARENTHESIS=1 " 
                     
enable_icu = 0
ldflags             = ""
if enable_icu:
    extension_cflags = extension_cflags + "-DSQLITE_ENABLE_ICU=1"
    ldflags = ldflags + "-licucore"
    
# these cflags must be manually sychronized to the Xcode 
# project OTHER_CFLAGS contents
#
normal_args = ['--enable-amalgamation', '--enable-shared']
normal_cflags = common_cflags + macosx_cflags + opt_cflags + extension_cflags

debug_args = ['--enable-amalgamation', '--enable-debug']
debug_cflags = common_cflags + macosx_cflags + "-g " + extension_cflags

add_cflags = ""
other_cflags = ""

args = normal_args
cflags = normal_cflags

if len(sys.argv) > 1:
    for arg in sys.argv[1:]:
        if arg[:2] == "-h":
            print_usage()
            sys.exit(0)
        elif arg == "-oldafp":
            other_cflags = other_cflags + " -DSQLITE_IGNORE_AFP_LOCK_ERRORS=1"
            print "Treating all AFP locking errors as SQLITE_BUSY"
        elif arg == "-preferproxy":
            other_cflags = other_cflags + " -DSQLITE_PREFER_PROXY_LOCKING=1"
            print "Overriding prefer proxy locking"
        elif arg == "-replay":
            other_cflags = other_cflags + " -DSQLITE_ENABLE_SQLRR=1"
            print "Enabling replay recording"
        elif arg == "-spi":
            other_cflags = other_cflags + " -DSQLITE_ENABLE_APPLE_SPI=1"
            print "Enabling replay recording"
        elif arg == "-windows":
            cflags = common_cflags
            print "Invoking windows-style sqlite configure command"
        elif arg == "-pagesize":
            other_cflags = other_cflags + pagesize_cflags
            print "Overriding default page size (makes unit tests blow up)"
        elif arg == "-purgeable":
            other_cflags = other_cflags + " -DSQLITE_ENABLE_PURGEABLE_PCACHE=1"
            print "Using purgeable pcache plugin"
        elif arg == "-nolimits":
            limits_cflags = ""
            print "Leaving default limits"
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

if limits_cflags:
    cflags = cflags + " " + limits_cflags
if add_cflags:
    cflags = cflags + " " + add_cflags

if other_cflags:
    cflags = cflags + " " + other_cflags
    
command_string = srcroot + "public_source/configure " + str(string.join(args, " ")) + " CFLAGS='" + cflags + "' " 

if ldflags:
    command_string = command_string + "LDFLAGS='" + ldflags + "'"

print command_string

cmd = srcroot + "public_source/configure"

#print "command: " + cmd
#print "args: " + str(args)

os.system(command_string)
