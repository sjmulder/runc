# RunC

Run compiled language files as if they were shell scripts.

### Example

    $ cat main.c
    #include <stdio.h>

    int main()
    {
    	printf("Hello, World!\n");
    	return 0;
    }

    $ runc main.c
    Hello, World!

To debug with gdb:

    $ runc -d main.c

### How it works

RunC compiles the source file using some default compiler flags and invokes the built executable, passing on any other arguments. The executable is cached in `~/.runc/caches/<hash>`, where `hash` is a SHA-1 hash of the full file.

The default compilation command line is:

    clang -Wall -std=c99 <filename> -o ~/.runc/caches/<hash>

RunC is not limited to C programs. Use a hint comment to supply a different command line:

    /*! mcs /ref:SomeLib.dll */

    class Program
    {
    	public static void Main()
    	{
    		System.Console.WriteLine("Hello, World!");
    	}
    }

This file would be compiled like so:

    mcs /ref:SomeLib.dll <filename> -o <filename> ~/.runc/caches/<hash>

This will work as long as the compiler accepts the `-o` option and the resulting executable can be ran directly by the shell.

It is also possible to supply only cflags:

    /*! -lcrypto -g */

Hints will be interpreted as solely cflags when the first non-whitespace character is a `-`. For this example, the resulting command would be:

    clang -Wall -std=c99 -lcrypto -g <filename> -o ~/.runc/caches/<hash>

### Credits

Written by Sijmen Mulder <sjmulder@gmail.com>.

### Licence

Copyright © 2011, Sijmen Mulder
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
 * The name of Sijmen Mulder may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL SIJMEN MULDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.