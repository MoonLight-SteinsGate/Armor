# Armor
### Overview
Armor is the first anti-hardware tracing technique to protect the runtime information of software against the hardware tracing techniques. More details of Armor are introduced in our technical paper "Armor: Protecting Software Against Hardware Tracing Techniques", which has been published on TIFS.

### Installation and Usage
The installation and usage of Armor are the same as those of AFL-GCC. Just entering the dictionary of Armor and compiling it with `make`. Then, you can compile the protected software with `armor-gcc` or `armor-g++`. Current version of Armor only supports the ARM Juno R2 develop board. Porting Armor on other ARM platforms requires modifying the parameters in the assembly files (e.g., `armor.s`).