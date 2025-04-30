project('bstoolbox', 'c')

srcs = [ 'bstoolbox.c' ]

if build_machine.kernel() == 'linux'
    srcs += 'linux.c'
endif

executable('bstoolbox', srcs, install : true)
