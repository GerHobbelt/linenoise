# -*- tcl -*-
# Tcl package index file, version 1.1
#
if {[package vsatisfies [package provide Tcl] 9.0-]} {
    package ifneeded linenoise 1.0 \
	    [list load [file join $dir linenoise.so]]
} else {
    package ifneeded linenoise 1.0 \
	    [list load [file join $dir linenoise.so]]
}

