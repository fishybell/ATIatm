# machine boots with it, works but was not tested too much
DEFAULT_PREFERENCE_at91sam9260ek   = "30"
DEFAULT_PREFERENCE_at91sam9261ek   = "30"
DEFAULT_PREFERENCE_at91sam9263ek   = "30"
DEFAULT_PREFERENCE_at91sam9rlek    = "30"
DEFAULT_PREFERENCE_at91sam9g20ek   = "30"
DEFAULT_PREFERENCE_at91sam9g20ek_2mmc = "30"
DEFAULT_PREFERENCE_at91sam9g10ek   = "30"
DEFAULT_PREFERENCE_at91sam9g45ekes = "30"
DEFAULT_PREFERENCE_at91sam9xeek    = "30"
DEFAULT_PREFERENCE_at91sam9m10ekes = "30"

at91sam_patch = " \
	ftp://ftp.linux4sam.org/pub/linux/2.6.30-at91/2.6.30-at91-exp.3.tar.gz \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0001-at91-dm9000-initialization-update.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0002-at91-add-gpio-button-support-for-at91sam9g20ek.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0003-at91-add-gpio-button-and-leds-support-for-at91sam9r.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0004-at91-at91sam9rlek-lcd-interface-correction.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0005-avr32-add-two-new-at91-to-cpu.h-definition.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0006-5438-1-AT91-manage-clock-by-functionality-instead.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0007-atmel_serial-fix-hang-in-set_termios-when-crtscts-i.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0008-USB-atmel_usba_udc-change-way-of-specifying-bias-f.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0009-Add-MACB-TX-Buffer-in-SRAM-support.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0010-5569-1-at91-Support-for-at91sam9g45-clocks-manage.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0011-5571-1-at91-Basic-support-for-at91sam9g45-series.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0012-5572-1-at91-Support-for-at91sam9g45-series-core-c.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0013-ARM-5622-1-at91-at91sam9g45-headers-DMA-peripher.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0014-5567-1-at91-Support-for-at91sam9g10-clocks-manage.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0015-5568-1-at91-Basic-support-for-at91sam9g10-header.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0016-5570-1-at91-Support-for-at91sam9g10-core-chip-b.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0017-5614-1-at91-atmel_lcdfb-add-at91sam9g10-support-t.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0018-USB-at91-Add-USB-EHCI-driver-for-at91sam9g45-serie.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0019-USB-at91-Add-USB-gadget-driver-selection-for-at91s.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0020-USB-at91-modify-OHCI-driver-to-allow-shared-interr.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0021-at91-USB-at91sam9g45-series-USB-host-integration.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0022-at91-USB-USB-drivers-modifications-for-at91sam9g10.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0023-dmaengine-at_hdmac-new-driver-for-the-Atmel-AHB-DM.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0024-dmaengine-at_hdmac-add-DMA-slave-transfers.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0025-at91-dmaengine-integration-of-at_hdmac-driver-in-at.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0026-at91-dmaengine-integration-of-at_hdmac-driver-in-at.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0027-atmel-mci-Integrate-AT91-specific-definition-in-hea.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0028-atmel-mci-add-MCI2-register-definitions.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0029-atmel-mci-Unified-Atmel-MCI-drivers-AVR32-AT91.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0030-AT91-atmel-mci-Platform-configuration-to-the-the-a.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0031-atmel-mci-change-use-of-dma-slave-interface.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0032-mmc-atmel-mci-New-MCI2-module-support-in-atmel-mci.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0033-at91-atmel-mci-inclusion-of-sd-mmc-driver-in-at91sa.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0034-MCI2-High-speed-mode-support.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0035-at91_mci-Enable-MMC_CAP_SDIO_IRQ-only-when-it-actua.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0036-at91_mci-Do-a-reset-after-each-request.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0037-at91_mci-introduce-per-mci-revision-conditional-cod.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0038-dmaengine-Move-all-map_sg-unmap_sg-for-slave-channe.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0039-AC97-atmel-add-support-for-AT91-common-with-AVR32.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0040-AT91-modify-the-support-of-AC97-on-the-at91sam9263.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0041-AT91-correct-AC97-reset-line-in-at91sam9263ek-board.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0042-at91-add-AC97-support-to-at91sam9rl-and-at91sam9rle.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0043-at91-add-AC97-support-to-at91sam9g45-series-and-at9.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0044-input-atmel_tsadcc-touchscreen-rework-setting-capa.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0045-input-atmel_tsadcc-touchscreen-use-platform-paramet.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0046-at91-input-platform-parameters-for-atmel_tsadcc-in.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0047-at91-input-touchscreen-support-for-at91sam9g45ekes.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0048-at91-extend-power-management-to-at91sam9g45-chip.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0049-at91-at91sam9g45-family-identify-several-chip-vers.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0050-atmel_lcdfb-New-alternate-pixel-clock-formula.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0051-mach-types-add-2MMC-slot-9g20-board.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0052-at91-at91sam9g20ek-add-card-detect-signal-to-sd-mmc.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0053-at91-at91sam9g20ek-modify-dual-slot-evaluation-kit.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0054-at91-add-gpio-buttons-to-at91sam9g20-ek-2mmc.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0055-Bug-workaround-for-channel-in-ac97.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0056-input-atmel_tsadcc-report-fake-pressure-measure.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0057-at91-No-more-SD-MMC-Write-Protect-pin-connected-on.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0058-net-macb-modify-SRAM-location-of-descriptor-buffer.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0059-at91-add-pwm-support-in-Kconfig-for-at91sam9g45.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0060-at91-update-at91sam9263ek-defconfig-file.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0061-at91-add-at91sam9g45ekes-defconfig-file.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0062-at91-update-at91sam9g20ek-defconfig-file.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0063-at91-update-at91sam9261ek-defconfig-file.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0064-at91-update-at91sam9260ek-defconfig-file.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0065-at91-update-at91sam9rlek-defconfig-file.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0066-sound-soc-update-soc-support-to-2.6.32-rc3-alsa.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0067-sound-at91sam9g20ek_2mmc-board-uses-same-audio-conn.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0068-at91-add-I2C-definitions-for-at91sam9g20ek-boards.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0069-at91-add-at91sam9g10ek-defconfig-file.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0070-at91-update-at91sam9260ek_defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0071-at91-update-at91sam9261ek_defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0072-at91-update-at91sam9263ek_defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0073-at91-update-at91sam9g20ek_defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0074-at91-update-at91sam9g20ek_2mmc_defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0075-at91-update-at91sam9g45ekes_defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0076-at91-update-at91sam9rlek_defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0077-at91-add-at91sam9xeek-defconfig.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0078-t91-add-at91sam9m10ekes-support.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0079-at91-at91sam9m10-add-dma-touchscreen-mmc-usb-host-a.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0080-at91-at91sam9m10-add-video-decoder-clock.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0081-at91sam9m10-add-on2-8170-gpl-modules.patch;patch=1 \
	file://${WORKDIR}/2.6.30-at91-exp.3/2.6.30-at91-exp.3-0082-Final-release-for-2.6.30-at91-exp.3.patch;patch=1 \
	"

SRC_URI_append_at91sam9260ek = ${at91sam_patch}
SRC_URI_append_at91sam9261ek = ${at91sam_patch}
SRC_URI_append_at91sam9263ek = ${at91sam_patch}
SRC_URI_append_at91sam9rlek = ${at91sam_patch} 
SRC_URI_append_at91sam9g20ek = ${at91sam_patch}
SRC_URI_append_at91sam9g20ek_2mmc = " \
	${at91sam_patch} \
	file://mingpio.patch;patch=1 \
	file://mac.patch;patch=1 \
	file://heartbeat.patch;patch=1 \
	file://no_wm8731.patch;patch=1 \
	file://horn.patch;patch=1 \
	"
#	file://mma7455l_i2c.patch;patch=1 \
#	file://mma7455l.patch;patch=1 \
#	file://no_wm8731.patch;patch=1 \
#	file://noleds.patch;patch=1 \
#	file://pwm.patch;patch=1 \
#	file://mmc.patch;patch=1 \
SRC_URI_append_at91sam9g10ek = ${at91sam_patch}
SRC_URI_append_at91sam9xeek = ${at91sam_patch}
SRC_URI_append_at91sam9g45ekes = ${at91sam_patch}
SRC_URI_append_at91sam9m10ekes = ${at91sam_patch}

