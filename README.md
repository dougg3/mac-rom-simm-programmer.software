# Mac ROM SIMM Programmer Software
This is the Windows/Mac/Linux software that goes with the [Mac ROM SIMM Programmer](https://github.com/dougg3/mac-rom-simm-programmer). It allows you to read and write programmable ROM SIMMs, and also update the firmware of the programmer.

This software is also compatible with the [bigmessowires programmer](http://www.bigmessowires.com/mac-rom-inator-ii-programming/) and the [CayMac Vintage programmer](https://ko-fi.com/s/6f9e9644e4). They are all based on my original design and share the same firmware and software.

In order for this software to work correctly, make sure you pick the correct size that matches your ROM SIMM in the "SIMM capacity" dropdown at the top of the main window.

## Compiling

This project makes use of the [Qt](https://www.qt.io/) open source widget toolkit. It also uses [qextserialport](https://github.com/qextserialport/qextserialport), which is checked out as a submodule. Note: Older versions of this program required [my special fork of qextserialport](https://github.com/dougg3/doug-qextserialport-linuxnotifications) to be cloned next to this project in order for it to build.

To check out the project, type the following command:

`git clone --recursive https://github.com/dougg3/mac-rom-simm-programmer.software.git`

To compile the project, either open the .pro file in Qt Creator and build it there, or run the following commands inside the directory:

```
qmake
make
```

This will generate a SIMMProgrammer executable that you can run.

## Binaries

Precompiled binaries are available in the [Releases section](https://github.com/dougg3/mac-rom-simm-programmer.software/releases) of this project.

## Linux notes

There are a few special things to mention about the Linux version of this software. First of all, you need to install libudev-dev as a prerequisite before it will build:

`sudo apt install libudev-dev`

By default, the programmer will probably not be accessible to your user account, and ModemManager may attempt to see if it's a modem, which will result in strange behavior like the programmer not operating properly. For example, the programmer software may get stuck waiting forever to communicate with the programmer. The easiest solution is to install a udev rule file for the programmer that sets proper permissions and disables ModemManager from accessing the device. This rule file is called `99-simm-programmer.rules` and it is available in this repository. To install it:

```
sudo cp 99-simm-programmer.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

After running these commands, unplug and replug your programmer board.
