/******************************************************************************
 * Copyright (c) 2013 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Claudio Fontana (Linaro) - initial implementation
 *     based on Peter Maydell's risu_arm.c
 *****************************************************************************/

#include <stdio.h>
#include <ucontext.h>
#include <string.h>

#include "risu.h"
#include "risu_reginfo_aarch64.h"

struct reginfo master_ri, apprentice_ri;

uint8_t apprentice_memblock[MEMBLOCKLEN];

static int mem_used = 0;
static int packet_mismatch = 0;

/* SP must be 16 byte aligned */
__attribute__((aligned(16), used)) char cb_stack[16384];

#define INLINE_HOOK_LENGTH 12
#define INLINE_HOOK_ADDR_OFFSET 6
#define INLINE_HOOK_SP_OFFSET 8
#define INLINE_HOOK_RETURN_OFFSET 10
/*
   Since AArch64 does not have ANY PC relative
   store instruciton, the hook 100% needs to
   overwrite and discard some value (we can
   not store to stack because SP is random)

   Thus the only real solution is to reserve
   one register. To test all registers, we
   must thus run the test two times, while
   not reseving the same register.

   For now, I will just use x16 and x17
*/
__attribute__((naked)) void inline_hook_template()
{
   asm volatile(
      // Overwrite SP (for now)
      "ldr x16, #32           \n"
      "add x16, x16, #16384   \n"
      "mov sp, x16            \n"

      // Store return addr in x17
      "ldr x17, #28           \n"

      // Jump to cb
      "ldr x16, #8   \n" // Load the absole address
      "br x16        \n" // Jump to the address
      ".int 0        \n" // To be used for
      ".int 0        \n" // callback addr

      ".int 0        \n" // Used for SP
      ".int 0        \n" //

      ".int 0        \n" // Used for Return address
      ".int 0        \n" //
   );
}

/* Special hook that ends the test (sets return address to null) */
__attribute__((naked)) void final_inline_hook_template()
{
   asm volatile(
      // Overwrite SP (for now)
      "ldr x16, #32           \n"
      "add x16, x16, #16384   \n"
      "mov sp, x16            \n"

      // Return addr = 0 (tells risu to end the tests)
      "mov x17, #0            \n"

      // Jump to cb
      "ldr x16, #8   \n" // Load the absole address
      "br x16        \n" // Jump to the address
      ".int 0        \n" // To be used for
      ".int 0        \n" // callback addr

      ".int 0        \n" // Used for SP
      ".int 0        \n" //

      ".int 0        \n" // Used for Return address
      ".int 0        \n" //
   );
}

void *load_with_inline_hooks(const char *imgfile, void (*cb)(void))
{
   /* Remove pointer signature (PAC) */
   uint64_t raw_cb = ((uint64_t)cb) & 0x0000FFFFFFFFFFFFULL;

   /* Load image file into memory as executable */
   struct stat st;
   fprintf(stderr, "loading test image %s...\n", imgfile);
   fprintf(stderr, "Original CB: %p\n", cb);
   fprintf(stderr, "Stripped CB: 0x%lx\n", raw_cb);
   fprintf(stderr, "CB SP start: %p, END: %p\n", cb_stack, cb_stack + (1<<14));
   fflush(stderr);
   int fd = open(imgfile, O_RDONLY);
   if (fd < 0)
   {
      fprintf(stderr, "failed to open image file %s\n", imgfile);
      exit(1);
   }
   if (fstat(fd, &st) != 0)
   {
      perror("fstat");
      exit(1);
   }
   size_t original_len = st.st_size;
   uint32_t *original_binary;
   uint32_t *tests_with_inline_hooks;

   /* Map writable because we include the memory area for store
    * testing in the image.
    */
   original_binary = mmap(0, original_len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

   /* The binary modified with inline hooks can be up to 5 times as large */
   tests_with_inline_hooks = mmap(0, original_len * 5, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (original_binary == MAP_FAILED || tests_with_inline_hooks == MAP_FAILED)
   {
      perror("mmap");
      exit(1);
   }
   
   fprintf(stderr, "GOT MMAPS: %p and %p\n", original_binary, tests_with_inline_hooks);
   
   /* Copy and add the inline hooks */
   uint32_t *curr_orig = original_binary;
   uint32_t *curr_inlined = tests_with_inline_hooks;
   while ((char *)curr_orig < (char *)original_binary + original_len)
   {
      uint64_t offset = (uint64_t)curr_orig - (uint64_t)original_binary;
      uint32_t op_bits = (*curr_orig) >> 16;
      /* The imm is originally used to determine
         what type of test to execute, so we need to
         handle that as well */
      uint32_t imm = (*curr_orig) & 0xFFFF;
      // THE FIRST 140 instructions (0x230 bytes) should be copied normally
      if (op_bits == 0 && offset > 0x230)
      {
         fprintf(stderr, "FOUND SIGILL %lx, IMM: %x\n", offset, imm);

         // Undefined instruction, replace with inline hook
         uint32_t *template = NULL;
         switch(imm){
            case 0x5af1:
               template = (uint32_t *)final_inline_hook_template;
               break; 
            default:
               template = (uint32_t *)inline_hook_template;  
               break;
         }

         // Copy the template hook
         for (int i = 0; i < INLINE_HOOK_LENGTH; i++)
         {
            *(curr_inlined + i) = *(template + i);
         }

         // Add the address to the hook
         *(uint64_t *)(curr_inlined + INLINE_HOOK_ADDR_OFFSET) = raw_cb;

         // Add the SP to the hook
         *(uint64_t*)(curr_inlined + INLINE_HOOK_SP_OFFSET) = (uint64_t)cb_stack;

         // Add return address to the hook
         *(uint64_t*)(curr_inlined + INLINE_HOOK_RETURN_OFFSET) = (uint64_t)(curr_inlined + INLINE_HOOK_LENGTH);
         

         for (int i = 0; i < INLINE_HOOK_LENGTH; i++)
         {
            fprintf(stderr, "\tHook istr: %d : %x\n", i, *(curr_inlined + i));
         }

         curr_inlined += INLINE_HOOK_LENGTH;
      }
      else
      {
         fprintf(stderr, "FOUND NORMAL: %lx, %x, op: %d\n", offset, *curr_orig, op_bits);
         // Normal instruction, just copy
         *curr_inlined = *curr_orig;
         curr_inlined++;
      }

      curr_orig++;
   }

   /* Make the tests executable */
   mprotect(tests_with_inline_hooks, original_len * 5, PROT_EXEC | PROT_READ);
   close(fd);
   munmap(original_binary, original_len);
   __builtin___clear_cache((char *)tests_with_inline_hooks, (char *)tests_with_inline_hooks + original_len * 5);
   return tests_with_inline_hooks;
}

void advance_pc(void *vuc)
{
   ucontext_t *uc = vuc;
   uc->uc_mcontext.pc += 4;
}

static void set_x0(void *vuc, uint64_t x0)
{
   ucontext_t *uc = vuc;
   uc->uc_mcontext.regs[0] = x0;
}

static int get_risuop(uint32_t insn)
{
   /* Return the risuop we have been asked to do
    * (or -1 if this was a SIGILL for a non-risuop insn)
    */
   uint32_t op = insn & 0xf;
   uint32_t key = insn & ~0xf;
   uint32_t risukey = 0x00005af0;
   return (key != risukey) ? -1 : op;
}

int send_register_info(int sock, void *uc)
{
   struct reginfo ri;
   int op;
   reginfo_init(&ri, uc);
   op = get_risuop(ri.faulting_insn);

   switch (op)
   {
   case OP_COMPARE:
   case OP_TESTEND:
   default:
      /* Do a simple register compare on (a) explicit request
       * (b) end of test (c) a non-risuop UNDEF
       */
      return send_data_pkt(sock, &ri, sizeof(ri));
   case OP_SETMEMBLOCK:
      memblock = (void *)ri.regs[0];
      break;
   case OP_GETMEMBLOCK:
      set_x0(uc, ri.regs[0] + (uintptr_t)memblock);
      break;
   case OP_COMPAREMEM:
      return send_data_pkt(sock, memblock, MEMBLOCKLEN);
      break;
   }
   return 0;
}

int recv_and_compare(int sock, int op)
{
   int resp = 0;
   int recv_err = recv_data_pkt(sock, &apprentice_ri, sizeof(apprentice_ri));
   uint64_t pc_save = apprentice_ri.pc;
   /* Set apprentice PC to master PC temporarely
   to avoid comparing PC values */
   apprentice_ri.pc = master_ri.pc;
   if (recv_err)
   {
      packet_mismatch = 1;
      resp = 2;
   }
   else if (!reginfo_is_eq(&master_ri, &apprentice_ri))
   {
      /* register mismatch */
      resp = 2;
   }
   else if (op == OP_TESTEND)
   {
      resp = 1;
   }
   send_response_byte(sock, resp);
   apprentice_ri.pc = pc_save;
   return resp;
}

/* Read register info from the socket and compare it with that from the
 * ucontext. Return 0 for match, 1 for end-of-test, 2 for mismatch.
 * NB: called from a signal handler.
 *
 * We don't have any kind of identifying info in the incoming data
 * that says whether it is register or memory data, so if the two
 * sides get out of sync then we will fail obscurely.
 */
int recv_and_compare_register_info(int sock, void *uc)
{
   int resp = 0, op;

   reginfo_init(&master_ri, uc);
   op = get_risuop(master_ri.faulting_insn);

   switch (op)
   {
   case OP_COMPARE:
   case OP_TESTEND:
   default:
      /* Do a simple register compare on (a) explicit request
       * (b) end of test (c) a non-risuop UNDEF
       */
      resp = recv_and_compare(sock, op);
      break;
   case OP_SETMEMBLOCK:
      memblock = (void *)master_ri.regs[0];
      break;
   case OP_GETMEMBLOCK:
      set_x0(uc, master_ri.regs[0] + (uintptr_t)memblock);
      break;
   case OP_COMPAREMEM:
      mem_used = 1;
      if (recv_data_pkt(sock, apprentice_memblock, MEMBLOCKLEN))
      {
         packet_mismatch = 1;
         resp = 2;
      }
      else if (memcmp(memblock, apprentice_memblock, MEMBLOCKLEN) != 0)
      {
         /* memory mismatch */
         resp = 2;
      }
      send_response_byte(sock, resp);
      break;
   }

   return resp;
}

/* Print a useful report on the status of the last comparison
 * done in recv_and_compare_register_info(). This is called on
 * exit, so need not restrict itself to signal-safe functions.
 * Should return 0 if it was a good match (ie end of test)
 * and 1 for a mismatch.
 */
int report_match_status(void)
{
   int resp = 0;
   fprintf(stderr, "match status...\n");
   if (packet_mismatch)
   {
      fprintf(stderr, "packet mismatch (probably disagreement "
                      "about UNDEF on load/store)\n");
      /* We don't have valid reginfo from the apprentice side
       * so stop now rather than printing anything about it.
       */
      fprintf(stderr, "master reginfo:\n");
      reginfo_dump(&master_ri, stderr);
      return 1;
   }
   if (memcmp(&master_ri, &apprentice_ri, sizeof(master_ri)) != 0)
   {
      fprintf(stderr, "mismatch on regs!\n");
      resp = 1;
   }
   if (mem_used && memcmp(memblock, &apprentice_memblock, MEMBLOCKLEN) != 0)
   {
      fprintf(stderr, "mismatch on memory!\n");
      resp = 1;
   }
   if (!resp)
   {
      fprintf(stderr, "match!\n");
      return 0;
   }

   reginfo_dump_mismatch(&master_ri, &apprentice_ri, stderr);

   fprintf(stderr, "master reginfo:\n");
   reginfo_dump(&master_ri, stderr);
   fprintf(stderr, "apprentice reginfo:\n");
   reginfo_dump(&apprentice_ri, stderr);

   return resp;
}
