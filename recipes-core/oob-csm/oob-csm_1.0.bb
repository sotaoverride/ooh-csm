SUMMARY = "Out-of-Band Current Spike Mitigator Daemon for AI Racks"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://oob_csm.c"

# Add a hard dependency on systemd library packages inside the image environment
DEPENDS += "systemd"

UNPACKDIR = "${WORKDIR}/unpack-sources"
S = "${UNPACKDIR}"

do_compile() {
    # Explicitly append -lsystemd compile flags configuration parameters
    ${CC} ${CFLAGS} ${LDFLAGS} ${S}/oob_csm.c -o ${B}/oob-csm -lsystemd
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/oob-csm ${D}${bindir}/oob-csm
}

FILES:${PN} += "${bindir}/oob-csm"

