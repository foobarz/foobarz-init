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

#define FOOBARZ_INIT_VERSION "1.0.8"
#define _BSD_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sysexits.h>
#include <errno.h>
#include <string.h>

#define PARAM_REQ_NO 0
#define PARAM_REQ_YES 1
#define PARAM_SRC_DEFAULT 0
#define PARAM_SRC_LOCAL 1

/* Your initramfs-source should contain the following
 * cd /boot/initramfs-source
 * mkdir -p proc dev sys mnt bin sbin etc/zfs
 * touch etc/mtab
 * cp /etc/zfs/zpool.cache-initrd etc/zfs/zpool.cache
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
 */

void printk(char *fmt, ...) {
  FILE* f;
  va_list args;

  f = fopen("/dev/kmsg", "w");
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
  fflush(f);
  fclose(f);
  usleep(50000);
}

int main(int argc, char* argv[]) {
 /***** variables
  *
  */
 int i;
 int   fd = 0; /* file descriptor */
 unsigned long mountflags;

 /* kernel command line */
 off_t cmdline_size;
 char* cmdline; /* to be malloc 4096B */
 char* cmdline_end;
 char* temp_end;
 char* src_msg; /* default or local */
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
 };
 enum { iroot, irootfstype, imountopt, iinit, irunlevel, iconsole, ilastparam };
 
 /***** program
  *
  */
 printk("foobarz-init, version %s: booting initramfs.\n", FOOBARZ_INIT_VERSION);

 cmdline       = (char*) malloc(4096);
 miscproc_buff = (char*) malloc(4096);
 if( (cmdline == NULL) || (miscproc_buff == NULL) ) {
   printk("Unable to allocate buffer memory: malloc: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 }

 /* mount proc before dev since some devices
  * symlink into proc */
 printk("Attempting cmd: mount proc /proc\n");
 if( mount("proc", "/proc", "proc", 0, NULL) != 0 ) {
   printk("time to panic: mount: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 } else {
   printk("Mount proc successful.\n");  
 }

 /* mount devtmpfs as expected by an init program
  * and maybe required to mount zfs */
 printk("Attempting cmd: mount devtmpfs /dev\n");
 if( mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0 ) {
   printk("time to panic: mount: %s\n", strerror(errno));
   return EX_UNAVAILABLE;
 } else {
   printk("Mount devtmpfs successful.\n");
 }
 /* it is assumed that all required devices are in devtmpfs
  * and if not, there will be boot problems
  *
  * if you need a more complex device setup, then you might need
  * udevd and run it with a larger full initrd package from your distro
  */

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
     param[i].src = PARAM_SRC_LOCAL; /* value src: local */
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
     param[i].src = PARAM_SRC_DEFAULT; /* value src: default */
     if( param[i].req == PARAM_REQ_YES ) flag_param_missing = 1;
     switch(i) {
       case iroot      : param[i].v = "<missing required param>" ; break;
       case irootfstype: param[i].v = "<missing required param>" ; break;
       case imountopt  : param[i].v = "ro"        ; break;
       case iinit      : param[i].v = "/sbin/init"; break;
       case irunlevel  : param[i].v = "3"         ; break;
       case iconsole   : param[i].v = "console"   ; break;
       default         : param[i].v = "";
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
  *  now, examine specific params for defaults and correctness
  */

 /* param[iroot]: nothing to do, if user put bad device in then we fail */

 /* param[irootfstype]: can be checked against /proc/filesystems: */
 /* required, so should have value */
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
 
 if( strcmp(param[irootfstype].v, "zfs") == 0 ) {
   if( access("/etc/zfs/zpool.cache", F_OK) == 0 )
     printk("rootfstype=%s: /etc/zfs/zpool.cache is present in initramfs.\n", param[irootfstype].v);
   else
     printk("rootfstype=%s: /etc/zfs/zpool.cache not present in initramfs.\n", param[irootfstype].v);   
   if( access("/etc/hostid", F_OK) == 0 )
     printk("rootfstype=%s: /etc/hostid is present in initramfs.\n", param[irootfstype].v);
   else
     printk("rootfstype=%s: /etc/hostid not present in initramfs.\n", param[irootfstype].v);
 }


 if(      strcmp(param[imountopt].v, "ro") == 0 ) mountflags = MS_RDONLY;
 else if( strcmp(param[imountopt].v, "rw") == 0 ) mountflags = 0;
 else {
   printk("%s \"%s\": invalid parameter value; defaulting to \"ro\".\n", param[imountopt].n, param[imountopt].v);
   mountflags = MS_RDONLY;
 }

 printk("Attempting cmd: mount -t %s -o %s %s /mnt.\n", param[irootfstype].v, param[imountopt].v, param[iroot].v);
 if( mount(param[iroot].v, "/mnt", param[irootfstype].v, mountflags, NULL) != 0 ) {
  printk("time to panic: mount: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 printk("%s mounted successfully.\n", param[iroot].v);

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

 printk("(3) Attempting cmd: chdir /mnt \n");
 if( chdir("/mnt") != 0 ) {
  printk("time to panic: chdir: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }

 printk("(4) Attempting cmd: mount --move . / \n");
 if( mount(".", "/", NULL, MS_MOVE, NULL) != 0 ) {
  printk("time to panic: mount: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 
 printk("(5) Attempting cmd: chroot . \n");
 if( chroot(".") != 0 ) {
  printk("time to panic: chroot: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 
 printk("(6) Attempting cmd: chdir / \n");
 if( chdir("/") != 0 ) {
  printk("time to panic: chdir: %s\n", strerror(errno));
  return EX_UNAVAILABLE;
 }
 printk("Completed switch root procedure.\n");

 /* check for "console=" kernel parameter and switch
  *  stdin, stdout, and stderr to named console device
  */
 if( param[iconsole].src == PARAM_SRC_LOCAL ) {
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
