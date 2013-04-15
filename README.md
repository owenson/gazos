Welcome to GazOS
GazOS is developed by Gareth Owen (drgowen@gmail.com)

Here I attempt to tell you how to create a working floppy
from the source code presented here so that you can fiddle with
and modify the source code for yourself.

First of all, in the main directory you need to type:
make
That will create a kernel.elf file in the main directory.
Once you have done this, change to the gazfs directory and
type:
./format 52 2 readme.txt help.txt

That will format the floppy in /dev/fd0 with a GazFS filing system
allowing 52 sectors for the kernel and putting the two files above
on the file system too.

Now, change to the boot directory, there you will find a small bash
shell script that will compile the bootsector and write both the bootsector
and the kernel to the floppy disk in /dev/fd0.

Now, you are all set and can just reboot your computer with the floppy in
the drive, from there GazOS should boot.

When you have made modifications to GazOS, assuming you do not want to
change the files on the filing system, you can skip the 'format' step and
just do:

make
cd boot
./c

And that will write the new kernel to the floppy drive.
You must make sure that it the floppy is already formatted to GazFS.

If you modify the code in anyway, I would appreciate it if you would share
your modifications with me, and perhaps I will include them in the next
release. Also, if you use any code in GazOS for you OS or another project I
would be grateful if you would tell me too, just so I know about it.

Thanks
Gareth

