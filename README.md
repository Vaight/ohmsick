# <img style="filter:invert(1);" align="left" width="40" height="40" src="ASSETS/chip.svg" alt="Logo"><p style="color:white;"> &nbsp; Ohmsick </p>
<!-- data placeholders -->
<p align="center">
    Version: <b>EXP 0.1.8</b>
    &nbsp;&nbsp;&nbsp; / &nbsp;&nbsp;&nbsp;
    Branch: <b>main</b>
    &nbsp;&nbsp;&nbsp; / &nbsp;&nbsp;&nbsp;
    Released: <b>No</b>
</p>
<b>Ohmsick</b> (a play on the word homesick) is a compatibility layer between microcontrollers like Arduinos and VST3 host applications.
<br><br>
This project contains 3 distinct systems. The 1st is the <b><i>firmware</i></b>, which is the binary that is flashed onto the microcontroller device itself. This handles communication between the hardware and the communication channel. The 2nd system is the <b><i>communication backend</i></b> which handles sending and recieving information on the communication channel. The final system is the <b>VST3 translator & frontend</b> which allows incoming and outgoing data to be integrated into host applications and configured.
<br><br>

# Installation

This software is in an experimental (EXP) state. This means that there is not an official build of the software to download. Follow the following steps to build it yourself!
<br><br>

First, clone the repository onto your machine.

```shell
git clone https://github.com/Vaight/ohmsick.git
```

Ensure <b>CMake</b> is installed on your machine and your C++ compiler of choice. If you would like to use the premade build scripts, please have <code>G++</code> or <code>Visual Studio 18 2026</code> installed.

Now, ensuring you are within the folder, navigate to the <code>/scripts/</code> directory.

```shell
cd scripts
```

Use the script for your current platform to execute the build!

```shell
./build-linux.sh
./build-windows.ps1
```

If the provided scripts do not work or if you have your own build configuration, use cmake in your terminal directly.

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

<br><br>

The compiled <b>VST3</b> is currently found at...

```
build-$target$/vst3arduinothing_vst3_artefacts/Release/VST3/VST3 Arduino Thing.vst
```

Included in this project is a really small VST3 debug host application. This currently can be found at...

```
build-$target$/debug_host_artefacts/Release/VST3 Arduino Thing Debug Host
```

<br><br>

Thanks for taking the time to read this! Message me if you have any issues or problems with this project and i'll be happy to help!