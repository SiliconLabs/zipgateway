import sys
import re
import subprocess

f = open(sys.argv[1])


symbols = dict()
for l in f:
    # match a line like this
    #   0201382AH   PUBLIC    HDATA    ---       nvmHostApplicationDescriptor
    x = re.match("\s*([0-9A-F]+)H\s+PUBLIC\s+HDATA\s+(\S*)\s+(\S*).*", l)
    if(x):
        (offset, datatype, name) = x.groups()
        offset = int(offset, 16) & 0xffff
        symbols[offset] = (name, datatype, "[1]")


for s in symbols:
    try:
        out = subprocess.check_output(
            ["/opt/local/bin/git", "-C", "/Users/anesbens/git/sdk670", "grep", symbols[s][0]])
        for l in out.split('\n'):
            #x = re.match(".*:\s*extern\s*(\S+)\s*far\s*([A-Za-z0-9_]+)(\[\S+\])*\s*",l)
            x = re.match(".*:\s*(\S+)\s*far\s*([A-Za-z0-9_]+)(\[.+\])*\s*", l)

            if(x):
                (datatype, name, array_size) = x.groups()
                if(datatype != symbols[s][1]):
                    if(symbols[s][1] == "---"):
                        if(not array_size):
                            array_size = "[1]"
                        symbols[s] = (symbols[s][0], datatype, array_size)

    except subprocess.CalledProcessError as e:
        print(e)


for s in sorted(symbols.keys()):
    if(symbols[s][2] == "[1]"):
        print("%s %s; // offset 0x%04x" % (symbols[s][1], symbols[s][0], s))
    else:
        print("%s %s%s; // offset 0x%04x" %
              (symbols[s][1], symbols[s][0], symbols[s][2], s))
