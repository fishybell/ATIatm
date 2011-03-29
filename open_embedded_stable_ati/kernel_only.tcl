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
set dir "N:/devel/atm/open_embedded_stable_ati/flash"
set ubootFile		[file join $dir "u-boot-at91sam9g20ek_2mmc-nandflash.bin"]
set kernelFile		[file join $dir "uImage-at91sam9g20ek_2mmc.bin"]

set ubootEnvFile	"ubootEnvtFileNandFlash.bin"

## NandFlash Mapping
set ubootAddr		0x00020000
set ubootEnvAddr    0x00060000
set kernelAddr		0x00200000

# u-boot variable
set kernelUbootAddr	0x00200000 
set kernelLoadAddr	0x22200000


## NandFlash Mapping
set kernelSize	[format "0x%08X" [file size $kernelFile]]

lappend u_boot_variables \
    "bootdelay=3" \
    "baudrate=115200" \
    "stdin=serial" \
    "stdout=serial" \
    "stderr=serial" \
    "bootargs=mem=64M console=ttyS0,115200 mtdparts=atmel_nand:4M(bootstrap/uboot/kernel)ro,60M(rootfs),-(data) root=/dev/mtdblock1 rw rootfstype=jffs2" \
    "bootcmd=nand read.jffs2 $kernelLoadAddr $kernelUbootAddr $kernelSize; bootm $kernelLoadAddr"


puts "-I- === Initialize the NAND access ==="
NANDFLASH::Init

puts "-I- === Load the u-boot environment variables ==="
set fh [open "$ubootEnvFile" w]
fconfigure $fh -translation binary
puts -nonewline $fh [set_uboot_env u_boot_variables]
close $fh
send_file {NandFlash} "$ubootEnvFile" $ubootEnvAddr 0 

puts "-I- === Load the Kernel image ==="
send_file {NandFlash} "$kernelFile" $kernelAddr 0

