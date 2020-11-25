#!/usr/bin/env python
#
# Interleaves a colormap image and heightmap image into a terrain map
#
# Compatible with Python 2 and Python 3
#

import sys
import png  # Run `python -m pip install pypng` if not found

def is_pow_of_2(n):
    return (n & (n - 1)) == 0

def fatal(message):
    print(message)
    exit(1)

if len(sys.argv) != 4:
     fatal('usage: ' + sys.argv[0] + ' colormap heightmap binfile')

# Read colormap
r = png.Reader(sys.argv[1])
(cmapWidth, cmapHeight, cmapRows, info) = r.read()
if not is_pow_of_2(cmapWidth):
    fatal(sys.argv[1] + ': width must be a power of two')
if not is_pow_of_2(cmapHeight):
    fatal(sys.argv[1] + ': height must be a power of two')
if info['bitdepth'] != 8:
    fatal(sys.argv[1] + ': bit depth must be 8')
    
# Read heightmap
r = png.Reader(sys.argv[2])
(hmapWidth, hmapHeight, hmapRows, info) = r.read()
if not is_pow_of_2(hmapWidth):
    fatal(sys.argv[2] + ': width must be a power of two')
if not is_pow_of_2(hmapHeight):
    fatal(sys.argv[2] + ': height must be a power of two')
if not info['greyscale']:
    fatal('heightmap must be a grayscale image')
if info['bitdepth'] != 8:
    fatal(sys.argv[2] + ': bit depth must be 8')

if hmapWidth != cmapWidth or hmapHeight != cmapHeight:
    fatal('heightmap and colormap must have the same dimensions')

with open(sys.argv[3], 'wb') as f:
    for y in range(0, hmapHeight):
        hmapRow = next(hmapRows)
        cmapRow = next(cmapRows)
        for x in range(0, hmapWidth):
            f.write(bytearray([cmapRow[x], hmapRow[x]]))
            #f.write(cmapRow[x])
            #sys.stdout.write('0x%02X, 0x%02X, ' % (int(hmapRow[x]), int(cmapRow[x])))
        #sys.stdout.write('\n')

