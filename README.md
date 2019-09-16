# Order-Preserving Solid-State Drives (OP-SSDs)

This repository contains a prototype of order-preserving solid-state drives (OP-SSDs).
You can find more details about OP-SSDs (e.g., its guarantees, applications and mechanisms) in our [ATC '19 paper](https://www.usenix.org/system/files/atc19-chang_0.pdf).

This is a research prototype. Please don't use it for production.

## Tested environment

* **Host**: Ubuntu Linux 16.04
* **Hardware platform**: Jasmine OpenSSD with 128GB flash memory
* Connect the hardware platform to the host with SATA (required) and UART for debugging (optional)

## File structure

* **ftl_optr**: order-preserving firmware source code
* **vst**: simulation framework and related scripts
* **build_gnu**: script to build the firmware
* **installer**: install the compiled firmware on the Jasmine OpenSSD platform
* **include** and **target_spw**: firmware library

In the following description, let `ROOT_OPTR` be the root directory of this repository.

## Run on simulation framework

The source code and related scripts for the VST simulation framework (details in our [ICCAD '17](https://dl.acm.org/citation.cfm?id=3199738) paper) are in the `vst` directory.

```
cd ${ROOT_OPTR}/vst
```

### Build simulation framework and firmware

```
make
```

This should generate an executable for the VST simulation framework (`vst-jasmine`) and a compiled firmware in the form of a shared object (`ftl.so`).

### Download trace file

We used [MSR Cambridge Block I/O Traces](http://iotta.snia.org/traces/388) to drive our tests.
There are two subtraces on the website, please download both of them.
We only select 12 write-heavy traces in our own tests.

* hm_0
* mds_0
* prn_0
* proj_0
* prxy_0
* rsrch_0
* src1_2
* src2_0
* src2_2
* stg_0
* ts_0
* wdev_0

Please create a directory `${ROOT_OPTR}/vst/traces`, and put the trace files in the directory so that the testing script below can find the trace files.

### Test

After 1) building the simulation framework and firmware, and 2) downloading the trace files, you can enter the following command to run a test suite.

```
./check-op.sh
```

The test suite consists of three tests: **functional correctness**, **order-preserving semantics (without flushes)** and **(with flushes)**.
You can test each of them with the following commands:

#### Functional correctness

```
./vst-jasmine <trace_file> ./ftl.so -a
```

* `-a` will repeat the trace file `trace_file` until writting 1TB of data.

#### Order-preserving semantics (without flushes)

```
rm -rf output-order && mkdir output-order
./vst-jasmine <trace_file> ./ftl.so -a -s -j <n_jobs> -d ./output-order
```

* `-s` will simulate crashes.
* `-j <n_jobs>` will create `n_jobs` threads that run recovery procedure and check order-preserving semantics in a parallel manner.
* `-d <dirname>` will put recovery information and crash images in `dirname` directory (crash images are generated only when recovery results fail to preserve order-preserving semantics).

#### Order-preserving semantics (with flushes)

```
rm -rf output-flush && mkdir output-flush
./vst-jasmine <trace_file> ./ftl.so -a -s -j <n_jobs> -d ./output-flush -f 1000
```

* `-f <wr_interval>` will simulate a flush command issued to the firmware every `wr_interval` writes.

#### Debug with crash images

If the recovery results fail to preserve order-preserving semantics, the simulation framework will automatically generate the crash image that results in such failure as a counterexample.
You can then run the following command for debugging:

```
./vst-jasmine <trace_file> ./ftl.so -a -p -i <crash_img>
```

where `crash_img` is the file path of the crash image.
Note that `trace_file` should be the same as the one used to generate crash images.

* `-i` will run the recovery procedure on the crash image you specify.
* `-p` will check order-preserving semantics immediately after recovery procedure is done.

Moreover, you can run with `gdb` for more debugging information (you might have to change the compiler optimization option from `-O2` to `-O0` in `Makefile` for easier debugging):

```
gdb --args ./vst-jasmine <trace_file> ./ftl.so -a -p -i <crash_img>
```

As an example, you can try to remove the `flush-before-gc` constraint by commenting out  `$ROOT_OPTR/ftl_optr/ftl.c:321` and run trace `hm_0`. You should be able to see a few failure cases.

## Run on real hardware

### Dependency

```
sudo apt install gcc-arm-none-eabi
```

### Build firmware

```
cd ${ROOT_OPTR}/build_gnu
make
```

This should generate a binary file `firmware.bin`, which will be installed on the SSD platform.

### Install firmware

Make sure your SSD platform is in the [factory mode](http://www.openssd-project.org/wiki/Jasmine_FAQs#Working_in_the_factory_mode).

```
cd ${ROOT_OPTR}/installer
make
sudo ./installer <dev_file> 0
```

where the `<dev_file>` is the path of the device file of the SSD platform (should be something like `/dev/sdx`, you may find `lsblk` useful).

### Test

If the installation is successful, you should be able to test the SSD platform as if it is a normal SSD.
Make sure your OpenSSD platform is in the [normal mode](http://www.openssd-project.org/wiki/Jasmine_FAQs#How_can_I_return_to_the_normal_.28non-factory.29_mode.3F).

### Collect disk-level information

To collect disk-level information, such as # of reads/writes/flushes, distribution of read/write size, # of checkpoints, # of GCs (check `${ROOT_OPTR}/ftl_optr/stat.c` for more details), you can use the following command:

```
hdparm -y <dev_file>
```

which issues a `SATA standby` command to the SSD, and the firmware will output the disk-level information to the host through UART.

## Acknowledgement

The Jasmine OpenSSD firmware library, installer, and build scripts are borrowed from [Jasmine OpenSSD](http://www.openssd-project.org/wiki/Downloads).
