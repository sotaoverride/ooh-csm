FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://power_spike_mitigator.hpp"

# Hooking into the patch phase guarantees clean native Python script parsing
python do_patch:append() {
    import os
    import shutil

    # 1. Fetch system workspace variables configurations
    unpack_dir = d.getVar('UNPACKDIR')
    source_dir = d.getVar('S')
    
    # 2. Dynamically scan the unpacked tree to locate the target index header file
    target_hpp = None
    for root, dirs, files in os.walk(source_dir):
        if "redfish.hpp" in files:
            target_hpp = os.path.join(root, "redfish.hpp")
            break

    if not target_hpp:
        bb.error("VoltSentinel: Could not locate redfish.hpp in the source tree structure!")
        return

    target_dir = os.path.dirname(target_hpp)

    # 3. Copy your header code asset file smoothly into the include folder
    src_header = os.path.join(unpack_dir, "power_spike_mitigator.hpp")
    shutil.copy(src_header, target_dir)

    # 4. Read and adjust the header file strings dynamically
    with open(target_hpp, 'r') as f:
        lines = f.readlines()

    new_lines = []
    for line in lines:
        if "namespace redfish" in line:
            new_lines.append('#include "power_spike_mitigator.hpp"\n')
        new_lines.append(line)
        if "requestRoutesSystemLogServiceCollection(app);" in line:
            new_lines.append('    redfish::registerPowerSpikeMitigatorRoutes(app);\n')

    with open(target_hpp, 'w') as f:
        f.writelines(new_lines)

    bb.note("VoltSentinel: Successfully patched redfish.hpp out-of-band via python hook!")
}

