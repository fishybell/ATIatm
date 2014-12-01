#!/usr/bin/tclsh
# Watches for a specific udp packet and then prints the IP address of the sender
package require udp

proc udpEventHandler {sock} {
    set pkt [read $sock]
    set peer [fconfigure $sock -peer]
    if {$pkt == {Base Station}} {
        puts -nonewline "[lindex [split $peer] 0]"
        set ::notforever 1
    }
}

proc udp_listen {port} {
    set srv [udp_open $port]
    fconfigure $srv -buffering none -translation binary
    fileevent $srv readable [list ::udpEventHandler $srv]
    return $srv
}

set sock [udp_listen 53530]
vwait ::notforever
close $sock

