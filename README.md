# Mac ROM SIMM Programmer Software
This is the Windows/Mac/Linux software that goes with the [Mac ROM SIMM Programmer](https://github.com/dougg3/mac-rom-simm-programmer). It allows you to read and write programmable ROM SIMMs, and also update the firmware of the programmer.

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
