# ----------------------------------------------------------------------------
#         ATMEL Microcontroller Software Support 
# ----------------------------------------------------------------------------
# Copyright (c) 2008, Atmel Corporation
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
# this list of conditions and the disclaimer below.
#
# Atmel's name may not be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
# DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# ----------------------------------------------------------------------------


################################################################################
#  proc eeprom_env: Convert eeprom variables in a string ready to be flashed
#                   in the region reserved for environment variables
################################################################################
proc set_eeprom_env {nameOfLstOfVar} {
    upvar $nameOfLstOfVar lstOfVar
    
    set strEnv {}
    foreach {r} $lstOfVar {
puts "r: $r"
        lassign $r addr var
puts "addr: $addr"
puts "var: $var"
        # convert addr 
        set size [expr $addr + 0]
puts "size: $size"
        while {[string length $strEnv] < $size} {
            append strEnv "\0"
        }
        if {[string length $strEnv] > $size} {
            set strEnv [string range $strEnv 0 [expr $size - 1]]
        }
        append strEnv "$var"
    }

    return $strEnv
}

################################################################################
#  Main script: Load the linux demo in NandFlash,
#               Update the environment variables
################################################################################
set eepromEnvFile   "eepromEnv.bin"

set eepromEnvAddr	0x0

## Get device number (1 through 5) to map MAC address
set mac "70:5e:aa:00:00:0"
puts "Enter an MAC suffix (0-9,A-F): "
set suffix [gets stdin]
if {![string is integer -strict $suffix] || $suffix > 9 || $suffix < 0} {
    if {[string is integer -strict $suffix] || ($suffix != {A} && $suffix != {B} && $suffix != {C} && $suffix != {D} && $suffix != {E} && $suffix != {F} && $suffix != {a} && $suffix != {b} && $suffix != {c} && $suffix != {d} && $suffix != {e} && $suffix != {f})} {
        puts "bad suffix (must be 0 through 9 or A through F)"
        return
    }
}

puts "Enter target type (MIT/SIT/MAT/SAT/SES): "
set target_type [gets stdin]
if ($target_type == {}) {
    set target_type "none"
}

puts "Enter communication type (network/radio): "
set comm_type [gets stdin]
if ($comm_type == {}) {
    set comm_type "none"
}

set ip_address "127.0.0.1"
if {$comm_type != "radio" && $comm_type != "none"} {
    puts "Enter server IP address (default 192.168.1.1): "
    set ip_address [gets stdin]
    if ($ip_address == {}) {
        set ip_address "192.168.1.1"
    }
}

# eeprom variables:
# 0x00 : SIT/MIT/etc.
# 0x40 : mac address
# 0x80 : network/radio
# 0xC0 : ip address of server (127.0.0.1 for radio)
# 0x100 : mechanical hit sensor parameter area (or none)
# 0x140 : miles parameter area (or none)
# 0x180 : muzzle flash parameter area (or none)
# 0x1C0 : LOMAH parameter area (or none)
lappend eeprom_variables \
    "0x00 $target_type" \
    "0x40 ${mac}${suffix}" \
    "0x80 $comm_type" \
    "0xC0 $ip_address" \
    "0x100 none" \
    "0x140 none" \
    "0x180 none" \
    "0x1C0 none" \
    "0x200" ;# fill up to <-- with zeroes


puts "-I- === Initialize the EEPROM access ==="
EEPROM::Init 9

puts "-I- === Load the EEPROM environment variables ==="
set fh [open "$eepromEnvFile" w]
fconfigure $fh -translation binary
puts "eeprom_variables: $eeprom_variables"
set ee_data [set_eeprom_env eeprom_variables]
puts "data length: [string length $ee_data]"
puts -nonewline $fh "$ee_data"
close $fh
send_file {EEPROM AT24} "$eepromEnvFile" $eepromEnvAddr 0 

