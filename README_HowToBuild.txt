Fshell must be built from the appropriate subdirectory of \sf\os\fshell\build. There is a separate set of build files for each platform that fshell supports. "Platform" in this context meaning the combination of baseline (eg S^3 PDK) and target device/board (eg planning to include fshell in the ROM you're building for a prototype development board).

Most app developers will probably be working on an S^3 PDK baseline, and targetting a production N8 device installing via Open Signed SIS file. In this case you'd want to use the s60\5th_edition platform:

    N:\fshell>cd build\s60\5th_edition
    N:\fshell\build\s60\5th_edition>sbs -c armv5_urel
    ...
    N:\fshell\build\s60\5th_edition>sbs -c winscw_udeb

For more information see http://developer.symbian.org/wiki/Fshell/Getting_Started
Open Signing of SIS files is described here: http://developer.symbian.org/wiki/Fshell/Getting_Started#Installing_fshell_at_runtime


The platforms are (at time of writing):

s60\3rd_edition     For any S60 3rd edition phone. Builds an open signed SIS file. For more info see:
s60\5th_edition     For any S60 5th edition or later phone, including the N8. Builds an open signed SIS file.
s60\tb92            Internal Nokia platform. Either you know what it is or you shouldn't be using it.
ncp                 Internal Nokia platform. Either you know what it is or you shouldn't be using it.
sf\2                Full GUI build for any ROM build using S^2
sf\3                Full GUI build for any ROM build using S^3 or later, including beagle board and QEMU
sf\3tshell          Textshell-only build for S^3 or later, supports QEMU and beagleboard
sf\minigui          Supports the minigui S^3 configuration. For more information see:
                    http://developer.symbian.org/wiki/Fshell/Getting_Started#Getting_started_on_QEMU_with_S.5E3_minigui

symtb92              Legacy platform for pre-symbian foundation builds. Not maintained.


Choose the platform closest to what you're working on. Generally this will be one of the s60 or sf platforms unless you're actually on a project that has its own dedicated platform dir, like NCP. The main distinction between the platforms is the set of macros defining what the platform supports. These are given in \fshell\build\whatever\platform.mmh. Most of the macros are of the form FSHELL_XXX_SUPPORT or FSHELL_NO_XXX_SUPPORT. The full set of available macros is documented in \epoc32\documentation\fshell\internal\common_mmh.html (auto-generated from \fshell\documentation\common_mmh.pod when you build fshell).

If you get any build errors they can often be temporarily worked around by tweaking the platform.mmh to disable the offending code, eg to add a '#define FSHELL_NO_SQL_SUPPORT' or somesuch. Please raise a bug if you have problems building on any of the supported platforms mentioned above, using the following link: http://developer.symbian.org/bugs/enter_bug.cgi?product=fshell


- tomsci
4-Nov-2010
