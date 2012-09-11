/* software product name: foobarz-init.c
 * suggested binary name: /init (in initramfs-source, rootfs)
 * license              : BSD
 * license text:
Copyright (c) 2012, foobarz
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Your initramfs-source should contain the following
 * 
 * cd /boot/initramfs-source
 * mkdir -p proc dev sys mnt bin sbin etc/zfs
 * touch etc/mtab
 * cp /etc/zfs/zpool.cache-initrd etc/zfs/zpool.cache
 * # zpool.cache is optional - zpool_import_ kernel params can be used instead
 * #  see below for details on using zpool_import
 * mknod dev/console c 5 1   # system console
 * mknod dev/kmsg    c 1 11  # lines printed to kmsg enter kernel messages buffer
 * mknod dev/loop0   b 7 0
 * mknod dev/tty     c 5 0   # current tty
 * mknod dev/tty0    c 4 0   # current virtual term
 * mknod dev/tty1    c 4 1   # login virtual term 1 (F1)
 * mknod dev/ttyS0   c 4 64  # COM1
 * mknod dev/ttyS1   c 4 65  # COM2
 * mknod dev/ttyS2   c 4 66  # COM3
 * mknod dev/ttyS3   c 4 67  $ COM4
 * 
 * and this program compiled to /boot/initramfs-source/init
 * 
 * Set kernel config option
 *  CONFIG_INITRAMFS_SOURCE=/boot/initramfs-source
 * to build the initramfs into your kernel image
 *  that also has builtin drivers (spl and zfs, etc).
 */

#define FOOBARZ_INIT_VERSION "1.1.1"
#define _BSD_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sysexits.h>
#include <errno.h>
#include <string.h>
/* support for zpool import
 * 
 * If -DINCLUDE_ZPOOL_IMPORT, then support to import a zpool is
 * enabled in the program. Enabling it will add many dependencies to the
 * compile and link; for example:
 * 
 * gcc -static -DINCLUDE_ZPOOL_IMPORT \
 *    foobarz-init.c -include /usr/src/zfs-0.6.0-rc10/3.2.28/zfs_config.h \
 *   -o init \
 *   -I /usr/include/libspl -I /usr/include/libzfs \
 *   -lzfs -lnvpair -lzpool -luutil -luuid -lrt -lz -lm -lpthread \
 *   -I /usr/include/tirpc \
 *   -ltirpc
 * 
 * Note that libtirpc is a drop-in replacement for the SunRPC functions that
 * used to be in glibc. No additional includes are needed, just the gcc -I and -l
 * options for tirpc.
 * 
 * Otherwise, with -UINCLUDE_ZPOOL_IMPORT, the compile is just:
 * gcc -static foobarz-init.c -o init
 */
#if defined(INCLUDE_ZPOOL_IMPORT)
#include <libzfs.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#endif

#define PARAM_REQ_NO 0
#define PARAM_REQ_YES 1
#define PARAM_SRC_DEFAULT 0
#define PARAM_SRC_CMDLINE 1

void printk(char *fmt, ...) {
  FILE* f;
  int fd;
  va_list args;

  f = fopen("/dev/kmsg", "w");
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
  fflush(f);
  fclose(f);
  /* avoid flooding kmsg and having msgs suppressed; 20msgs/sec */
  usleep(50000);
}

int main(int argc, char* argv[]) {
 /*** variables */

 int i;
 int   fd = 0; /* file descriptor */
 unsigned long mountflags;

 /* kernel command line */
 off_t cmdline_size;
 char* cmdline; /* to be malloc 4096B */
 char* cmdline_end;
 char* temp_end;
 char* src_msg; /* default or cmdline */
 int flag_param_missing = 0;

 /* use to hold contents of a misc /proc/<file> */
 char* miscproc_buff; /* to be malloc 4096B */
 off_t miscproc_size;

 /* note about environ, argv, and kernel cmdline for init:
  *   environ is not defined for init
  *   only argv[0] is set for init
  *   kernel command line parameters are accessed
  *   at /proc/cmdline
  */ 

 /* kernel parameters expected to be name=value */
 /* do not use quotes or spaces in parameters   */
 /* you can add more params somwhere after root= */
 struct nv { char* n; char* v; char* v_end; int req; int src; };
 struct nv param[] = {
   { "root=",       NULL, NULL, PARAM_REQ_YES, PARAM_SRC_DEFAULT },
   { "rootfstype=", NULL, NULL, PARAM_REQ_YES, PARAM_SRC_DEFAULT },
   { "mountopt=",   NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT },
   { "init=",       NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT },
   { "runlevel=",   NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT },
   { "console=",    NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT }
#if defined(INCLUDE_ZPOOL_IMPORT)
   ,
   { "zpool_import_name=",    NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT },
   { "zpool_import_guid=",    NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT },
   { "zpool_import_newname=", NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT },
   { "zpool_import_force=",   NULL, NULL, PARAM_REQ_NO , PARAM_SRC_DEFAULT }
#endif
 };
 enum {
	 iroot,
	 irootfstype,
	 imountopt,
	 iinit,
	 irunlevel,
	 iconsole,
#if defined(INCLUDE_ZPOOL_IMPORT)
	 izpool_import_name,
	 izpool_import_guid,
	 izpool_import_newname,
	 izpool_import_force,
#endif
	 ilastparam
 };
 
#if defined(INCLUDE_ZPOOL_IMPORT) 
 libzfs_handle_t* libzfs = NULL;
 importargs_t iargs = { 0 };
 nvlist_t* pools = NULL;
 nvpair_t* pool = NULL;
 nvlist_t* config = NULL;
#endif

 /*** program */

 printk("foobarz-init, version %s: booting initramfs.\n", FOOBARZ_INIT_VERSION);

 cmdline       = (char*) malloc(4096);
 miscproc_buff = (char*) malloc(4096);
 if( (cmdline == NULL) || (miscproc_buff == NULL) ) {
   printk("Unable to allocate buffer memory: malloc: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 }

 /* mount proc /proc
  *  note: some /dev devices symlink into /proc
  *  proc contains info about processes, including cmdline etc. */
 printk("Attempting cmd: mount proc /proc\n");
 if( mount("proc", "/proc", "proc", 0, NULL) != 0 ) {
   printk("time to panic: mount: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 } else {
   printk("Mount proc successful.\n");  
 }

 /* mount devtmpfs /dev
  *  note: This simple init program works if your root device is made from devices
  *  that are available by default in devtmpfs, such as /dev/sd*
  * 
  *  For zfs, your root zfs pool should be created with default device nodes and
  *  then it should be mountable by this simple init program.
  *
  *  udev may be needed to configure device nodes and symlinks required
  *  to access a root device configuration made with such nodes and symlinks.
  *  If you need udevd, you can include it into your initramfs-source and
  *  modify this program to run it before attempting to mount your root device.
  *  However, if udevd is needed, a significant number of userspace programs may also be
  *  required by rules in /lib/udev/. You could install busybox + udev (about 5MB) or
  *  coreutils + util-linux + bash + udev (about 25MB) into initramfs-source. But, at that
  *  point you'd have ash or bash and many tools that are easier to use than this
  *  simple init program; it would then be easy to have /init as #!/bin/<b>ash script. */
 printk("Attempting cmd: mount devtmpfs /dev\n");
 if( mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0 ) {
   printk("time to panic: mount: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 } else {
   printk("Mount devtmpfs successful.\n");
 }

 /* mount sysfs /sys
  *  note: some kernel modules try to access /sys with userspace helpers to echo values into /sys variables;
  *  such modules expect a minimal userspace that contains coreutils or busybox */
 printk("Attempting cmd: mount sysfs /sys\n");
 if( mount("sysfs", "/sys", "sysfs", 0, NULL) != 0 ) {
   printk("time to panic: mount: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 } else {
   printk("Mount sysfs successful.\n");
 }

 /* process kernel command line */
 fd = open("/proc/cmdline", O_RDONLY);
 if( fd == -1 ) {
   printk("Cannot open /proc/cmdline: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 }
 /* note, on /proc fs:
  *       lseek likely always returns error
  *       stat likely always returns st_size = 0
  *   so determining size of /proc file means just reading it;
  *   you have to read /proc files according to their documented
  *   maximum sizes; this is probably for performance reasons */
 cmdline_size = read(fd, cmdline, 4095);
 if( cmdline_size == -1 ) {
   printk("Failed to read /proc/cmdline: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 }
 close(fd);

 /* cmdline may be newline + null terminated, but make it null + null */
 cmdline[cmdline_size] = '\0';
 if( cmdline[cmdline_size-1] == '\n' ) {
   cmdline[cmdline_size-1] = '\0';
   cmdline_size--;
   cmdline_end = cmdline + cmdline_size;
 }
 printk("Kernel cmdline size: %i\n", cmdline_size);
 printk("Kernel cmdline: \"%s\"\n", cmdline);

 /* find v and v_end of values in cmdline */
 for( i=iroot; i<ilastparam; i++ ) {
   param[i].v = strstr(cmdline, param[i].n);
   if( param[i].v != NULL ) {
     param[i].src = PARAM_SRC_CMDLINE;
     while( *(param[i].v) != '=' ) param[i].v++;
     param[i].v++;
     temp_end = param[i].v;
     while( !( (*temp_end == ' ') ||
               (*temp_end == '\n') ||
               (temp_end == cmdline_end)
	     ) ) temp_end++;
     if( temp_end == param[i].v ) {
       printk("Kernel parameter %s: value missing.\n", param[i].n);
       param[i].v = NULL;
     } else param[i].v_end = temp_end;
   }
 }

 for( i=iroot; i<ilastparam; i++ ) {
   /* terminate value strings */
   if( param[i].v_end != NULL ) *(param[i].v_end) = '\0';
   /* set defaults if no value on cmdline */
   if( param[i].v == NULL ) {
     param[i].src = PARAM_SRC_DEFAULT;
     if( param[i].req == PARAM_REQ_YES ) flag_param_missing = 1;
     switch(i) {
       case iroot      : param[i].v = "<missing required param>" ; break;
       case irootfstype: param[i].v = "<missing required param>" ; break;
       case imountopt  : param[i].v = "ro"        ; break;
       case iinit      : param[i].v = "/sbin/init"; break;
       case irunlevel  : param[i].v = "3"         ; break;
       case iconsole   : param[i].v = "console"   ; break;
       default         : param[i].v = NULL;
     }
   }
   if(param[i].src == PARAM_SRC_DEFAULT) src_msg = "default";
   else src_msg = "cmdline";
   printk("Using %s \"%s\" (source: %s)\n", param[i].n, param[i].v, src_msg);
 }

 if( flag_param_missing ) {
   printk("Aborting boot process: missing required kernel parameter(s).\n");
   return EX_USAGE;
 }

 /* generic nv pair kernel cmdline processing finished
  *  now, examine specific params for defaults and correctness */

 /* param[irootfstype]: can be checked against /proc/filesystems: */ 
 fd = open("/proc/filesystems", O_RDONLY);
 if( fd == -1 ) {
   printk("Cannot open /proc/filesystems: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 }
 miscproc_size = read(fd, miscproc_buff, 4095);
 if( miscproc_size == -1 ) {
   printk("Failed to read /proc/filesystems: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 }
 close(fd);
 if( strstr(miscproc_buff, param[irootfstype].v) == NULL ) {
   printk("%s \"%s\": filesystem type not available.\n", param[irootfstype].n, param[irootfstype].v);
   return EX_UNAVAILABLE;
 }

 /* zfs-specific */
 if( strcmp(param[irootfstype].v, "zfs") == 0 ) {
   if( access("/etc/zfs/zpool.cache", F_OK) == 0 )
     printk("rootfstype=%s: /etc/zfs/zpool.cache is present in initramfs.\n", param[irootfstype].v);
   else
     printk("rootfstype=%s: /etc/zfs/zpool.cache not present in initramfs.\n", param[irootfstype].v);

   if( access("/etc/hostid", F_OK) == 0 )
     printk("rootfstype=%s: /etc/hostid is present in initramfs.\n", param[irootfstype].v);
   else
     printk("rootfstype=%s: /etc/hostid not present in initramfs.\n", param[irootfstype].v);

#if defined(INCLUDE_ZPOOL_IMPORT)
   /* zpool import */
   if( (param[izpool_import_name].v != NULL) || (param[izpool_import_guid].v != NULL) ) {
	printk("zpool_import: import requested.\n");
	if( (param[izpool_import_name].v != NULL) && (param[izpool_import_guid].v != NULL) ) {
		printk("zpool_import: given both pool name and guid; using guid.\n");
		param[izpool_import_name].v = NULL;
	}
	if( param[izpool_import_name].v != NULL )
		printk("zpool_import: pool name: %s\n", param[izpool_import_name].v );
	else
		printk("zpool_import: pool guid: %s\n", param[izpool_import_guid].v );

	iargs.path = NULL;
	iargs.paths = 0;
	iargs.poolname = param[izpool_import_name].v;
	if( param[izpool_import_guid].v != NULL )
		iargs.guid = strtoull(param[izpool_import_guid].v, NULL, 10);
	else
		iargs.guid = 0;
	iargs.cachefile = NULL;
	if( (param[izpool_import_force].v != NULL) && (strcmp(param[izpool_import_force].v, "1") == 0) ) {
		iargs.can_be_active = 1;
		printk("zpool_import: import forced.\n");
	} else {
		iargs.can_be_active = 0;
		printk("zpool_import: import not forced.\n");
	}
	iargs.unique = 1;
	iargs.exists = 1;

	printk("zpool_import: init libzfs.\n");
	libzfs = libzfs_init();
	printk("zpool_import: searching for pool.\n");
	pools = zpool_search_import(libzfs, &iargs);
	if( (pools == NULL) || nvlist_empty(pools) )
		printk("zpool_import: pool not available for import, or already imported by cachefile.\n");
	else {
		printk("zpool_import: getting pool information.\n");
		pool = nvlist_next_nvpair(pools, pool);
		printk("zpool_import: getting pool configuration.\n");
		nvpair_value_nvlist(pool, &config);
		printk("zpool_import: attempting pool import.\n");
		if( zpool_import(libzfs, config, param[izpool_import_newname].v, NULL) != 0 ) {
			printk("zpool_import: import failed.\n");
			printk("zpool_import: error description: %s\n", libzfs_error_description(libzfs) );
			printk("zpool_import: error action: %s\n", libzfs_error_action(libzfs) );
		} else  printk("zpool_import: import successful.\n");
	}
	printk("zpool_import: fini libzfs.\n");
	libzfs_fini(libzfs);
   }
#endif /* zpool_import */
 }

 if(      strcmp(param[imountopt].v, "ro") == 0 ) mountflags = MS_RDONLY;
 else if( strcmp(param[imountopt].v, "rw") == 0 ) mountflags = 0;
 else {
   printk("%s \"%s\": invalid parameter value; defaulting to \"ro\".\n", param[imountopt].n, param[imountopt].v);
   mountflags = MS_RDONLY;
 }

 /* param[iroot]: nothing to check; if user gives bad root=device then mount fails */

 /* try to mount root=device at /mnt
  *
  * note: for zfs, if a copy of /etc/zfs/zpool.cache (when pool is imported) is put in initramfs-source, then
  * the zfs module can read it and automatically import the pools described in the cache file; the imported
  * pools can be available to mount here if they were created using standard device names, otherwise
  * udevd may be required to run before mounting the pool */
 printk("Attempting cmd: mount -t %s -o %s %s /mnt.\n", param[irootfstype].v, param[imountopt].v, param[iroot].v);
 if( mount(param[iroot].v, "/mnt", param[irootfstype].v, mountflags, NULL) != 0 ) {
  printk("time to panic: mount: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 printk("%s mounted successfully.\n", param[iroot].v);

 /* check to see if the mounted root filesystem has an executable init program */
 chdir("/mnt");
 if( access(param[iinit].v+1, X_OK) != 0 ) {
   chdir("/");
   printk("access X_OK: %s\n", strerror(errno));
   printk("The init program /mnt/%s is not present or not executable.\n", param[iinit].v+1);
   printk("Aborting boot process: no init program.\n");
   printk("Unmounting %s.\n", param[iroot].v);
   if( umount("/mnt") == -1 ) {
     printk("umount: %s\n", strerror(errno));
     printk("Failed to umount %s.\n", param[iroot].v);
   } else printk("Successfully unmounted %s.\n", param[iroot].v);
   return EX_UNAVAILABLE;
 }
 chdir("/");
 printk("Init program /mnt/%s is present and executable.\n", param[iinit].v+1);

 /* switch the root / from initramfs to the mounted new root device at /mnt.
  * 
  * note: after this switch, it is not possible to access the initramfs files anymore,
  * yet they consume ram memory unless they are deleted here before switching.
  * Any programs that are run after clearing the initramfs and switching root must exist on the new root.
  * This program may safely delete itself (/init) since it is already in ram and executing.
  * If you have installed additional files and programs in initramfs that consume significant ram,
  * then you need to insert additional code here to delete those files (carefully). */

 /* delete files off of initramfs to free ram memory */
 printk("Freeing memory from initramfs...\n");
 if( unlink(argv[0]) != 0 ) printk("unlink %s: %s\n", argv[0], strerror(errno));
 else printk("%s %s", argv[0], "deleted from initramfs.\n");

 /* switch root */
 printk("Beginning switch root procedure.\n");

 printk("(1) Attempting cmd: mount --move /dev /mnt/dev \n");
 if( mount("/dev", "/mnt/dev", NULL, MS_MOVE, NULL) != 0 ) {
  printk("time to panic: mount: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }

 printk("(2) Attempting cmd: mount --move /proc /mnt/proc \n");
 if( mount("/proc", "/mnt/proc", NULL, MS_MOVE, NULL) != 0 ) {
  printk("time to panic: mount: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 
 printk("(3) Attempting cmd: mount --move /sys /mnt/sys \n");
 if( mount("/sys", "/mnt/sys", NULL, MS_MOVE, NULL) != 0 ) {
  printk("time to panic: mount: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }

 printk("(4) Attempting cmd: chdir /mnt \n");
 if( chdir("/mnt") != 0 ) {
  printk("time to panic: chdir: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }

 printk("(5) Attempting cmd: mount --move . / \n");
 if( mount(".", "/", NULL, MS_MOVE, NULL) != 0 ) {
  printk("time to panic: mount: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 
 printk("(6) Attempting cmd: chroot . \n");
 if( chroot(".") != 0 ) {
  printk("time to panic: chroot: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 
 printk("(7) Attempting cmd: chdir / \n");
 if( chdir("/") != 0 ) {
  printk("time to panic: chdir: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 printk("Completed switch root procedure.\n");

 /* check for "console=" kernel parameter and switch
  *  stdin, stdout, and stderr to named console device
  */
 if( param[iconsole].src == PARAM_SRC_CMDLINE ) {
   printk("Console redirection to device %s requested.\n", param[iconsole].v);
   /* expect only basename of console device (e.g., ttyS0), so chdir /dev */
   chdir("/dev");
   if( access(param[iconsole].v, F_OK ) == 0 ) {
     printk("Opening stdin, stdout, and stderr on %s.\n", param[iconsole].v);
     close(0);
     open(param[iconsole].v, O_RDWR);
     dup2(0, 1);
     dup2(0, 2);
   } else {
     printk("access F_OK: %s\n", strerror(errno));
     printk("Could not access device: %s!\n", param[iconsole].v);
     printk("Console redirection to device %s aborted!\n", param[iconsole].v);
   }
   chdir("/");
 }

 printk("Execing: \"%s %s\" to boot mounted root system.\n", param[iinit].v, param[irunlevel].v);

 /* free resources held to this point */
 free(cmdline);
 free(miscproc_buff);

 if( execl(param[iinit].v, param[irunlevel].v, (char *) NULL ) != 0 ) {  
  printk("time to panic: execl: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
}
