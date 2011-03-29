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
#  proc uboot_env: Convert u-boot variables in a string ready to be flashed
#                  in the region reserved for environment variables
################################################################################
proc set_uboot_env {nameOfLstOfVar} {
    upvar $nameOfLstOfVar lstOfVar
    
    # sector size is the size defined in u-boot CFG_ENV_SIZE
    set sectorSize [expr 0x20000 - 5]

    set strEnv [join $lstOfVar "\0"]
    while {[string length $strEnv] < $sectorSize} {
        append strEnv "\0"
    }
    # \0 between crc and strEnv is the flag value for redundant environment
    set strCrc [binary format i [::vfs::crc $strEnv]]
    return "$strCrc\0$strEnv"
}

################################################################################
#  Main script: Load the linux demo in NandFlash,
#               Update the environment variables
################################################################################
set day "fail"
set latest 0
set dir "N:/devel/atm/open_embedded_stable_ati/flash"
set bootstrapFile 	[file join $dir "at91bootstrap.bin"]
set ubootFile		[file join $dir "u-boot-at91sam9g20ek_2mmc-nandflash.bin"]
set kernelFile		[file join $dir "uImage-at91sam9g20ek_2mmc.bin"]
set rootfsFile		[file join $dir "console-image-at91sam9g20ek_2mmc.jffs2"]

set ubootEnvFile	"ubootEnvtFileNandFlash.bin"
set eepromEnvFile   "eepromEnv.bin"

## NandFlash Mapping
set bootStrapAddr	0x00000000
set eepromEnvAddr	0x0
set ubootAddr		0x00020000
set ubootEnvAddr    0x00060000
set kernelAddr		0x00200000
set rootfsAddr		0x00400000

# u-boot variable
set kernelUbootAddr	0x00200000 
set kernelLoadAddr	0x22200000


## NandFlash Mapping
set kernelSize	[format "0x%08X" [file size $kernelFile]]

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

puts "Enter target type (MIT/SIT/MAT/HSAT/LSAT): "
set target_type [gets stdin]

puts "Enter communication type (network/radio): "
set comm_type [gets stdin]

set ip_address "127.0.0.1"
if {$comm_type != "radio" && $comm_type !="none"} {
    puts "Enter server IP address (default 192.168.1.1): "
    set ip_address [gets stdin]
}

lappend u_boot_variables \
    "bootdelay=3" \
    "baudrate=115200" \
    "stdin=serial" \
    "stdout=serial" \
    "stderr=serial" \
    "bootargs=mem=64M console=ttyS0,115200 mtdparts=atmel_nand:4M(bootstrap/uboot/kernel)ro,60M(rootfs),-(data) root=/dev/mtdblock1 rw rootfstype=jffs2" \
    "bootcmd=nand read.jffs2 $kernelLoadAddr $kernelUbootAddr $kernelSize; bootm $kernelLoadAddr"


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

puts "-I- === Initialize the NAND access ==="
NANDFLASH::Init

puts "-I- === Erase all the NAND flash blocs and test the erasing ==="
NANDFLASH::EraseAllNandFlash

puts "-I- === Load the bootstrap: nandflash_at91sam9-ek in the first sector ==="
NANDFLASH::sendBootFile $bootstrapFile

puts "-I- === Load the u-boot in the next sectors ==="
send_file {NandFlash} "$ubootFile" $ubootAddr 0 

puts "-I- === Load the u-boot environment variables ==="
set fh [open "$ubootEnvFile" w]
fconfigure $fh -translation binary
puts -nonewline $fh [set_uboot_env u_boot_variables]
close $fh
send_file {NandFlash} "$ubootEnvFile" $ubootEnvAddr 0 

puts "-I- === Load the Kernel image ==="
send_file {NandFlash} "$kernelFile" $kernelAddr 0

puts "-I- === Load the linux file system ==="
send_file {NandFlash} "$rootfsFile" $rootfsAddr 0
