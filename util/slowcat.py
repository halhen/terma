#!/usr/bin/python2
# slowcat.py - print a file slowly
# author : dave w capella - http://grox.net/mailme
# date     : Sun Feb 10 21:57:42 PST 2008
############################################################

# Pipe a command through this script to print it slowly. Useful to see when
# the output goes bad from a long series of escape sequences like vttest(1).
# Capture the output of such a command with tee(1)

import sys, time

delay = 0.02

if len(sys.argv) > 1:
  arg = sys.argv[1]
  if arg != "-d":
    print "usage: %s [-d delay]" % (sys.argv[0])
    print "delay: delay in seconds"
    print "example: %s -d .02 < vtfile" % (sys.argv[0])
    sys.exit()
  if len(sys.argv) > 2:
    delay = float(sys.argv[2])

while 1:
  try:
    c = sys.stdin.read(1)
    sys.stdout.write(c);
    sys.stdout.flush()
  except:
    break
  time.sleep(delay)

######################################################################
# eof: slowcat.py
