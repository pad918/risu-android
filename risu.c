/******************************************************************************
 * Copyright (c) 2010 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 *****************************************************************************/


/* Random Instruction Sequences for Userspace */


#include <signal.h>
#include <ucontext.h>
#include <getopt.h>
#include <setjmp.h>
#include <assert.h>
#include <string.h>

#include "risu.h"

void *memblock = 0;

int apprentice_socket, master_socket;

sigjmp_buf jmpbuf;

/* Should we test for FP exception status bits? */
int test_fp_exc = 0;

void master_test(void* uc){
   fprintf(stderr, "CALLED HOOK IN MASTER\n");
   sleep(2);
   fprintf(stderr, "STATE: \n");
   for(int i=0; i<31; i++){
      fprintf(stderr, "R%x: %16lx\n", i, *((uint64_t*)uc+i));
   }
}

void end_tests(){
   fprintf(stderr, "Ending tests normally\n");
   sleep(2);
   exit(0);
}

/* risu corrupts sp */
__attribute__((naked))
void master_hook_cb(){
   asm volatile(
      "bti c                           \n"
      
      
      // SAVE REGS
      "sub sp, sp, #256          \n"
      "str x0, [sp, #0]         \n"
      "str x1, [sp, #8]         \n"
      "str x2, [sp, #16]         \n"
      "str x3, [sp, #24]         \n"
      "str x4, [sp, #32]         \n"
      "str x5, [sp, #40]         \n"
      "str x6, [sp, #48]         \n"
      "str x7, [sp, #56]         \n"
      "str x8, [sp, #64]         \n"
      "str x9, [sp, #72]         \n"
      "str x10, [sp, #80]         \n"
      "str x11, [sp, #88]         \n"
      "str x12, [sp, #96]         \n"
      "str x13, [sp, #104]         \n"
      "str x14, [sp, #112]         \n"
      "str x15, [sp, #120]         \n"
      "str x16, [sp, #128]         \n"
      "str x17, [sp, #136]         \n"
      "str x18, [sp, #144]         \n"
      "str x19, [sp, #152]         \n"
      "str x20, [sp, #160]         \n"
      "str x21, [sp, #168]         \n"
      "str x22, [sp, #176]         \n"
      "str x23, [sp, #184]         \n"
      "str x24, [sp, #192]         \n"
      "str x25, [sp, #200]         \n"
      "str x26, [sp, #208]         \n"
      "str x27, [sp, #216]         \n"
      "str x28, [sp, #224]         \n"
      "str x29, [sp, #232]         \n"
      "str x30, [sp, #240]         \n"
      
      // Call end function if x17 is 0
      "cbnz x17, skip_end           \n"
      "b end_tests                  \n"

      "skip_end:                    \n"

      "mov x29, #0                  \n" //TO BE REMOVED
      
      // TEMPORARY TO NOT DETECT THEM!
      "mov x16, #0                 \n"
      "mov x17, #0                 \n"

      // CALL master_test
      "adrp x16, master_test                \n"
      "add  x16, x16, :lo12:master_test     \n"
      // Give correct argument
      "mov x0, sp                      \n"
      "blr  x16                        \n"
      
      "mov x16, #0                 \n"
      "mov x17, #0                 \n"

      // Restore registers and jump back.
      
      "ldr x0, [sp, #0]         \n"
      "ldr x1, [sp, #8]         \n"
      "ldr x2, [sp, #16]         \n"
      "ldr x3, [sp, #24]         \n"
      "ldr x4, [sp, #32]         \n"
      "ldr x5, [sp, #40]         \n"
      "ldr x6, [sp, #48]         \n"
      "ldr x7, [sp, #56]         \n"
      "ldr x8, [sp, #64]         \n"
      "ldr x9, [sp, #72]         \n"
      "ldr x10, [sp, #80]         \n"
      "ldr x11, [sp, #88]         \n"
      "ldr x12, [sp, #96]         \n"
      "ldr x13, [sp, #104]         \n"
      "ldr x14, [sp, #112]         \n"
      "ldr x15, [sp, #120]         \n"
      "ldr x16, [sp, #128]         \n"
      "ldr x17, [sp, #136]         \n"
      "ldr x18, [sp, #144]         \n"
      "ldr x19, [sp, #152]         \n"
      "ldr x20, [sp, #160]         \n"
      "ldr x21, [sp, #168]         \n"
      "ldr x22, [sp, #176]         \n"
      "ldr x23, [sp, #184]         \n"
      "ldr x24, [sp, #192]         \n"
      "ldr x25, [sp, #200]         \n"
      "ldr x26, [sp, #208]         \n"
      "ldr x27, [sp, #216]         \n"
      "ldr x28, [sp, #224]         \n"
      "ldr x29, [sp, #232]         \n"
      "ldr x30, [sp, #240]         \n"

      "add sp, sp, #256        \n"

      "br x17                    \n"
   );
}


void master_sigill(int sig, siginfo_t *si, void *uc)
{
   switch (recv_and_compare_register_info(master_socket, uc))
   {
      case 0:
         /* match OK */
         printf("MATCH!\n");
         advance_pc(uc);
         return;
      default:
         /* mismatch, or end of test */
         siglongjmp(jmpbuf, 1);
   }
}

void apprentice_sigill(int sig, siginfo_t *si, void *uc)
{
   switch (send_register_info(apprentice_socket, uc))
   {
      case 0:
         /* match OK */
         advance_pc(uc);
         return;
      case 1:
         /* end of test */
         exit(0);
      default:
         /* mismatch */
         exit(1);
   }
}

static void set_sigill_handler(void (*fn)(int, siginfo_t *, void *))
{
   struct sigaction sa;
   memset(&sa, 0, sizeof(struct sigaction));

   sa.sa_sigaction = fn;
   sa.sa_flags = SA_SIGINFO;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGILL, &sa, 0) != 0)
   {
      perror("sigaction");
      exit(1);
   }
}

typedef void entrypoint_fn(void);

uintptr_t image_start_address;
entrypoint_fn *image_start;

void load_image(const char *imgfile)
{
   fprintf(stderr, "CALLED LOAD IMAGE\n");
   image_start = load_with_inline_hooks(imgfile, master_hook_cb);
   image_start_address = (uintptr_t)image_start;
   fprintf(stderr, "LOADED IMAGE AT: %p\n", image_start);
   sleep(1);

}

int master(int sock)
{
   /* Sigsetjmp works like a goto. It returns if comming here on its own
      and 1 if it was jumped back with siglongjmp */
   if (sigsetjmp(jmpbuf, 1))
   {
      // Sleep to make sure prints are printed in order
      sleep(2); 
      return report_match_status();
   }
   master_socket = sock;
   set_sigill_handler(&master_sigill);
   fprintf(stderr, "starting image\n");
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}

int apprentice(int sock)
{
   apprentice_socket = sock;
   set_sigill_handler(&apprentice_sigill);
   fprintf(stderr, "starting image\n");
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}

int ismaster;

void usage (void)
{
   fprintf(stderr, "Usage: risu [--master] [--host <ip>] [--port <port>] <image file>\n\n");
   fprintf(stderr, "Run through the pattern file verifying each instruction\n");
   fprintf(stderr, "between master and apprentice risu processes.\n\n");
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "  --master          Be the master (server)\n");
   fprintf(stderr, "  -h, --host=HOST   Specify master host machine (apprentice only)\n");
   fprintf(stderr, "  -p, --port=PORT   Specify the port to connect to/listen on (default 9191)\n");
}

int main(int argc, char **argv)
{
   // some handy defaults to make testing easier
   uint16_t port = 9191;
   char *hostname = "localhost";
   char *imgfile;
   int sock;

   // TODO clean this up later
   
   for (;;)
   {
      static struct option longopts[] = 
         {
            { "help", no_argument, 0, '?'},
            { "master", no_argument, &ismaster, 1 },
            { "host", required_argument, 0, 'h' },
            { "port", required_argument, 0, 'p' },
            { "test-fp-exc", no_argument, &test_fp_exc, 1 },
            { 0,0,0,0 }
         };
      int optidx = 0;
      int c = getopt_long(argc, argv, "h:p:", longopts, &optidx);
      if (c == -1)
      {
         break;
      }
      
      switch (c)
      {
         case 0:
         {
            /* flag set by getopt_long, do nothing */
            break;
         }
         case 'h':
         {
            hostname = optarg;
            break;
         }
         case 'p':
         {
            // FIXME err handling
            port = strtol(optarg, 0, 10);
            break;
         }
         case '?':
         {
            usage();
            exit(1);
         }
         default:
            abort();
      }
   }

   imgfile = argv[optind];
   if (!imgfile)
   {
      fprintf(stderr, "Error: must specify image file name\n\n");
      usage();
      exit(1);
   }

   load_image(imgfile);
   
   if (ismaster)
   {
      fprintf(stderr, "master port %d\n", port);
      sock = master_connect(port);
      return master(sock);
   }
   else
   {
      fprintf(stderr, "apprentice host %s port %d\n", hostname, port);
      sock = apprentice_connect(hostname, port);
      return apprentice(sock);
   }
}

   
