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

int ismaster;

/* Should we test for FP exception status bits? */
int test_fp_exc = 0;

void fail_tests(){
   fprintf(stderr, "TESTS FAILED\n");
   report_match_status();
   exit(1);
}

void end_tests(){
   fprintf(stderr, "Ending tests normally\n");
   exit(0);
}

void master_test(void* uc){
   switch (recv_and_compare_register_info(master_socket, uc))
   {
      case 0:
         /* match OK */
         fprintf(stderr, "Match in master\n");
         break;
      case 1:
         end_tests();
         break;
      default:
         /* mismatch, or end of test */
         fail_tests();
   }
   
}

void apprentice_test(void* uc){
   switch (send_register_info(apprentice_socket, uc))
   {
      case 0:
         /* match OK */
         fprintf(stderr, "Match in apprentice\n");
         return;
      case 1:
         end_tests();
      default:
         /* mismatch */
         fail_tests();
   }
}

void compare_and_test(void* uc){
   if(ismaster){
      master_test(uc);
   }
   else{
      apprentice_test(uc);
   }
   //fprintf(stderr, "Returning to the next instr at: %lx\n", return_addr);
}

/* risu corrupts sp */
__attribute__((naked))
void master_hook_cb(){
   asm volatile(
      
      // SAVE REGS
      "sub sp, sp, #1024          \n"
      "str X0, [sp, #0]         \n"
      "str X1, [sp, #8]         \n"
      "str X2, [sp, #16]         \n"
      "str X3, [sp, #24]         \n"
      "str X4, [sp, #32]         \n"
      "str X5, [sp, #40]         \n"
      "str X6, [sp, #48]         \n"
      "str X7, [sp, #56]         \n"
      "str X8, [sp, #64]         \n"
      "str X9, [sp, #72]         \n"
      "str X10, [sp, #80]         \n"
      "str X11, [sp, #88]         \n"
      "str X12, [sp, #96]         \n"
      "str X13, [sp, #104]         \n"
      "str X14, [sp, #112]         \n"
      "str X15, [sp, #120]         \n"
      "add sp, sp, #128           \n"
      "str X16, [sp, #0]         \n"
      "str X17, [sp, #8]         \n"
      "str X18, [sp, #16]         \n"
      "str X19, [sp, #24]         \n"
      "str X20, [sp, #32]         \n"
      "str X21, [sp, #40]         \n"
      "str X22, [sp, #48]         \n"
      "str X23, [sp, #56]         \n"
      "str X24, [sp, #64]         \n"
      "str X25, [sp, #72]         \n"
      "str X26, [sp, #80]         \n"
      "str X27, [sp, #88]         \n"
      "str X28, [sp, #96]         \n"
      "str X29, [sp, #104]         \n"
      "str X30, [sp, #112]         \n"
      "str Q0, [sp, #120]         \n"
      "add sp, sp, #128           \n"
      "str Q1, [sp, #8]         \n"
      "str Q2, [sp, #24]         \n"
      "str Q3, [sp, #40]         \n"
      "str Q4, [sp, #56]         \n"
      "str Q5, [sp, #72]         \n"
      "str Q6, [sp, #88]         \n"
      "str Q7, [sp, #104]         \n"
      "str Q8, [sp, #120]         \n"
      "add sp, sp, #128           \n"
      "str Q9, [sp, #8]         \n"
      "str Q10, [sp, #24]         \n"
      "str Q11, [sp, #40]         \n"
      "str Q12, [sp, #56]         \n"
      "str Q13, [sp, #72]         \n"
      "str Q14, [sp, #88]         \n"
      "str Q15, [sp, #104]         \n"
      "str Q16, [sp, #120]         \n"
      "add sp, sp, #128           \n"
      "str Q17, [sp, #8]         \n"
      "str Q18, [sp, #24]         \n"
      "str Q19, [sp, #40]         \n"
      "str Q20, [sp, #56]         \n"
      "str Q21, [sp, #72]         \n"
      "str Q22, [sp, #88]         \n"
      "str Q23, [sp, #104]         \n"
      "str Q24, [sp, #120]         \n"
      "add sp, sp, #128           \n"
      "str Q25, [sp, #8]         \n"
      "str Q26, [sp, #24]         \n"
      "str Q27, [sp, #40]         \n"
      "str Q28, [sp, #56]         \n"
      "str Q29, [sp, #72]         \n"
      "str Q30, [sp, #88]         \n"
      "str Q31, [sp, #104]         \n"
      "mrs X0, NZCV                 \n"
      "mrs X1, FPCR                 \n"
      "mrs X2, FPSR                 \n"
      "str X0, [sp, #120]          \n"
      "str X0, [sp, #128]          \n"
      "str X0, [sp, #136]          \n"

      // Sub back so that there is 1024 reserved on stack
      "sub sp, sp, #640           \n" // 5*128
      

      // Store faulting instruction
      // The type of test is stored in the upper 4 bits of the return address
      "add sp, sp, #1024          \n"
      "ldr x17, [sp, #-16]          \n" // Load return addr
      
      "lsr x0, x17, #60             \n" 
      "str x0, [sp, #-240]          \n" // Store test id (what test should run)
      "sub sp, sp, #1024          \n"
      
      // Call end function if x17 is 0
      "cbnz x17, skip_end           \n"
      "b end_tests                  \n"

      "skip_end:                    \n"
      
      // CALL compare_and_test
      "adrp x16, compare_and_test                \n"
      "add  x16, x16, :lo12:compare_and_test     \n"
      "mov x0, sp                      \n"
      "blr  x16                        \n"

      // Restore registers and jump back.
      "add sp, sp, #640           \n"
      "ldr X0, [sp, #120]          \n"
      "ldr X1, [sp, #128]          \n"
      "ldr X2, [sp, #136]          \n"
      "msr NZCV, x0                 \n"
      "msr FPCR, x1                 \n"
      "msr FPSR, x2                 \n"
      "sub sp, sp, #640           \n"
      "ldr X0, [sp, #0]         \n"
      "ldr X1, [sp, #8]         \n"
      "ldr X2, [sp, #16]         \n"
      "ldr X3, [sp, #24]         \n"
      "ldr X4, [sp, #32]         \n"
      "ldr X5, [sp, #40]         \n"
      "ldr X6, [sp, #48]         \n"
      "ldr X7, [sp, #56]         \n"
      "ldr X8, [sp, #64]         \n"
      "ldr X9, [sp, #72]         \n"
      "ldr X10, [sp, #80]         \n"
      "ldr X11, [sp, #88]         \n"
      "ldr X12, [sp, #96]         \n"
      "ldr X13, [sp, #104]         \n"
      "ldr X14, [sp, #112]         \n"
      "ldr X15, [sp, #120]         \n"
      "add sp, sp, #128            \n"
      "ldr X16, [sp, #0]         \n"
      "ldr X17, [sp, #8]         \n"
      "ldr X18, [sp, #16]         \n"
      "ldr X19, [sp, #24]         \n"
      "ldr X20, [sp, #32]         \n"
      "ldr X21, [sp, #40]         \n"
      "ldr X22, [sp, #48]         \n"
      "ldr X23, [sp, #56]         \n"
      "ldr X24, [sp, #64]         \n"
      "ldr X25, [sp, #72]         \n"
      "ldr X26, [sp, #80]         \n"
      "ldr X27, [sp, #88]         \n"
      "ldr X28, [sp, #96]         \n"
      "ldr X29, [sp, #104]         \n"
      "ldr X30, [sp, #112]         \n"
      "ldr Q0, [sp, #120]         \n"
      "add sp, sp, #128            \n"
      "ldr Q1, [sp, #8]         \n"
      "ldr Q2, [sp, #24]         \n"
      "ldr Q3, [sp, #40]         \n"
      "ldr Q4, [sp, #56]         \n"
      "ldr Q5, [sp, #72]         \n"
      "ldr Q6, [sp, #88]         \n"
      "ldr Q7, [sp, #104]         \n"
      "ldr Q8, [sp, #120]         \n"
      "add sp, sp, #128            \n"
      "ldr Q9, [sp, #8]         \n"
      "ldr Q10, [sp, #24]         \n"
      "ldr Q11, [sp, #40]         \n"
      "ldr Q12, [sp, #56]         \n"
      "ldr Q13, [sp, #72]         \n"
      "ldr Q14, [sp, #88]         \n"
      "ldr Q15, [sp, #104]         \n"
      "ldr Q16, [sp, #120]         \n"
      "add sp, sp, #128            \n"
      "ldr Q17, [sp, #8]         \n"
      "ldr Q18, [sp, #24]         \n"
      "ldr Q19, [sp, #40]         \n"
      "ldr Q20, [sp, #56]         \n"
      "ldr Q21, [sp, #72]         \n"
      "ldr Q22, [sp, #88]         \n"
      "ldr Q23, [sp, #104]         \n"
      "ldr Q24, [sp, #120]         \n"
      "add sp, sp, #128            \n"
      "ldr Q25, [sp, #8]         \n"
      "ldr Q26, [sp, #24]         \n"
      "ldr Q27, [sp, #40]         \n"
      "ldr Q28, [sp, #56]         \n"
      "ldr Q29, [sp, #72]         \n"
      "ldr Q30, [sp, #88]         \n"
      "ldr Q31, [sp, #104]         \n"
      // Set back to 1024 reserved
      "sub sp, sp, #640           \n"

      // Load original return addr
      "ldr x16, [sp, #1008]       \n"
      // Remove the upper bits of the return address
      "lsl x16, x16, #16         \n"
      "lsr x16, x16, #16         \n"
      "add sp, sp, #1024        \n"

      //return
      "br x16                    \n"
   );
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
   //sleep(0.1);
}

int master(int sock)
{

   master_socket = sock;
   fprintf(stderr, "starting image\n");
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}

int apprentice(int sock)
{
   apprentice_socket = sock;
   fprintf(stderr, "starting image\n");
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}



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

   
