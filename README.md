# qemu-gui-manager

A VirtualBox-style manager for QEMU virtual machines on Linux, written with
Qt 6.

Each VM is its QEMU command line: a `vm.args` file in the VM's folder, one
option per line. The settings pages read and edit those same lines, and
keep everything they don't know about. So any QEMU option works, and you
can edit the file by hand at any time, in the manager or in any editor.

It goes with [qemu-gui](https://github.com/tomtomnet/qemu-gui), the QEMU
fork whose SDL window has a menu, but it runs any `qemu-system-x86_64`.

> This project was written by AI: Claude (Anthropic) wrote its code, tests
> and documentation in Claude Code, following its maintainer's requirements
> and feedback.

## Features

- A list of your VMs with their state. Start, pause, shut down, reset or
  force off. VMs keep running when you close the manager, and it picks them
  up again when it starts.
- New VMs get modern defaults: q35 with KVM and the host CPU, UEFI from your
  distribution's firmware (optionally with Secure Boot), virtio disk,
  network and GPU, SDL with OpenGL, a USB tablet, PipeWire sound and the
  shared clipboard.
- The selected VM's details and settings in tabs beside the list, no
  separate window: memory and CPUs, boot, display and 3D acceleration,
  disks, shared folders (virtiofs), PCI passthrough (vfio) and USB
  passthrough. Changes wait from tab to tab until you apply them, and the
  manager asks before it drops any.
- The full argument list in a plain-text editor, with highlighting, checks
  and completion.
- A QEMU reference: every option, device (with its properties), machine, CPU
  model and backend of the QEMU you run, searchable, with an Insert button.
  When QEMU runs from a build tree, it includes the full documentation from
  `qemu-options.hx`.
- Clone a VM, as virt-manager does: a new VM with the same settings and
  copies of its disks and firmware, instant on btrfs and XFS; CD/DVD images
  stay shared, and the network cards get new addresses.
- Import a launch script: the manager takes the QEMU command out of it.
- A warning in the status bar when the running VMs could take more memory
  than is free, before the kernel has to kill one.
- A different QEMU build per VM, if one needs it.

## Build (Fedora)

    sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel
    git clone https://github.com/tomtomnet/qemu-gui-manager.git
    cd qemu-gui-manager
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ./build/qemu-gui-manager

To install it to `/usr/local`, with its menu entry, run
`sudo cmake --install build`.

To run VMs, it also needs QEMU (build [qemu-gui](https://github.com/tomtomnet/qemu-gui),
or `sudo dnf install qemu-system-x86-core`) and:

    sudo dnf install qemu-img edk2-ovmf virtiofsd hwdata

Choose the QEMU binary in File > Preferences. It defaults to the
`qemu-system-x86_64` in your `PATH`.

## The vm.args file

    # Fedora with a shared folder
    -name Fedora
    -machine q35,accel=kvm,memory-backend=mem
    -cpu host
    -smp 8
    -object memory-backend-memfd,id=mem,size=8G,share=on
    -drive file=disk.qcow2,if=virtio,format=qcow2
    -device virtio-vga-gl
    -display sdl,gl=on
    #share tag=public,path=/home/me/Public

- One option per line. The value is the rest of the line, as is: no shell
  quoting, so spaces need nothing special.
- Lines starting with `#` are comments, except for the manager's own
  directives, which QEMU never sees:
  - `#share tag=...,path=...[,cache=auto|always|never][,readonly=on][,mount=DIR]`
    shares a host folder with virtiofs, which the guest's agent mounts at DIR.
  - `#qemu /path/to/qemu-system-x86_64` runs this VM with that QEMU instead
    of the one in the preferences.
- QEMU runs in the VM folder, so relative paths, like `disk.qcow2` above,
  point into it.

## Building QEMU and virglrenderer

File > Build QEMU downloads [qemu-gui](https://github.com/tomtomnet/qemu-gui),
builds just the emulator for your computer's architecture (x86-64, or
aarch64 on ARM such as Asahi Linux) and `qemu-img`, and makes the result the
QEMU of your VMs. Building again updates it first. It needs QEMU's build
dependencies: `sudo dnf builddep qemu`. To run another QEMU, choose it in
the preferences, or for one VM with a `#qemu` line.

Tick **DRM native context** to also build a virglrenderer with the
native context renderer of every GPU that has one. The guest's GPU driver
then talks to the host GPU's own driver, for 3D acceleration close to the
host's:

- Most distributions' virglrenderer has no native context renderer.
  Fedora's has none.
- The build has Intel (Xe included, through a patch from
  [xe-native-context-enablement](https://github.com/cmspam/xe-native-context-enablement)
  that isn't upstream yet), AMD, Qualcomm, Apple (Asahi), Arm Mali, and
  Venus for Vulkan.
- The patch goes on upstream virglrenderer, or on the newest release it
  applies to.
- virglrenderer installs into the manager's data folder, and the QEMU it
  builds loads it from there. The system's virglrenderer stays as it is,
  and a rebuilt one takes effect at the next start of a VM.

This needs virglrenderer's build dependencies too:
`sudo dnf builddep virglrenderer`. The guest needs native context support
in its Mesa (for Xe, the Mesa patch of the repository above). A VM uses it
with `-device virtio-vga-gl,blob=on,hostmem=4G,drm_native_context=on`
(`virtio-gpu-gl-pci` on ARM), and with `-accel kvm,honor-guest-pat=on` on
Intel.

## Shared folders

For each `#share`, the manager starts a `virtiofsd` as you, and adds the
virtiofs device to QEMU. virtiofs needs the guest RAM in shared memory. The
Shared Folders page sets that up when you add a folder.

The guest can mount a folder by itself at each start, where the share's
`mount=` says (`/mnt/<name>` by default for new folders). For that it
needs the QEMU guest agent, which starts by itself once installed:

    sudo dnf install qemu-guest-agent

The manager gives the VM the agent's channel, and when the agent comes
up at boot, it has it create the folder and mount the share there, as
root. It says so in the status bar, or tells you what went wrong. To mount
a folder by hand instead:

    sudo mount -t virtiofs public /mnt/public

or in `/etc/fstab`:

    public  /mnt/public  virtiofs  defaults,nofail  0  0

## PCI passthrough

The PCI page lists your devices by IOMMU group, and says what is still
missing for each one:

- The IOMMU must be on, in the firmware (VT-d or AMD-Vi) and in the kernel.
  AMD kernels turn it on by themselves. On Intel, add `intel_iommu=on` to
  the kernel command line if `/sys/kernel/iommu_groups` stays empty.
- The device must be bound to `vfio-pci`, along with every device in its
  IOMMU group (bridges excepted), for example with driverctl
  (`sudo dnf install driverctl`):
  `sudo driverctl set-override 0000:03:00.0 vfio-pci`.
- You need read and write access to `/dev/vfio/<group>`, for example through
  a udev rule.
- QEMU locks all the guest RAM in memory, so your locked memory limit
  (`ulimit -l`) must cover it. Raise it with `DefaultLimitMEMLOCK=` in
  `/etc/systemd/system.conf` for your desktop session, and `memlock` in
  `/etc/security/limits.conf` for terminal logins, then log in again.

## USB passthrough

The USB page passes devices through by vendor and product ID. QEMU needs
read and write access to the device's node in `/dev/bus/usb`, and without it
QEMU silently doesn't attach the device. So when you start a VM, the manager
asks for access to its plugged-in devices that you can't open yet. Your
desktop shows its password dialog (polkit), like the menu of qemu-gui does.
The access lasts until the device is unplugged; if it isn't given, the
manager tells you which devices the VM won't get.

To give a device access for good, add a udev rule:

    # /etc/udev/rules.d/70-qemu-usb.rules
    SUBSYSTEM=="usb", ATTR{idVendor}=="046d", ATTR{idProduct}=="c52b", TAG+="uaccess"

## Files

| What | Where |
| --- | --- |
| VMs, one folder each | `~/.local/share/qemu-gui-manager/vms/` |
| Settings | `~/.config/qemu-gui-manager/settings.conf` |
| Sockets of running VMs | `$XDG_RUNTIME_DIR/qemu-gui-manager/` |
| QEMU reference index | `~/.cache/qemu-gui-manager/` |

Removing a VM moves its folder, disks included, to the trash.

## Tests

    ctest --test-dir build

Some tests run QEMU itself. Point them at a binary with
`QGM_TEST_QEMU=/path/to/qemu-system-x86_64`; otherwise they use the one in
your `PATH`, or skip.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).

The icon is the QEMU logo by Benoît Canet, under
[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/), with its colors
inverted.
