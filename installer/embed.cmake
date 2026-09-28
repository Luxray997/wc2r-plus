file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hexLength)
math(EXPR size "${hexLength} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUT}"
    "#pragma once\n"
    "// Generated from version.dll at build time. Do not edit.\n"
    "static const unsigned char kPayload[] = {\n${bytes}\n};\n"
    "static const unsigned long kPayloadSize = ${size}u;\n")
