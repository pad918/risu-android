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

void* writable_memory_block = NULL;

/* SP must be 16 byte aligned */
__attribute__((aligned(16), used)) char cb_stack[16384];

#define INLINE_HOOK_LENGTH 20
#define INLINE_HOOK_ADDR_OFFSET 11
#define INLINE_HOOK_SP_OFFSET 13
#define INLINE_HOOK_RETURN_OFFSET 15
/*
   Since AArch64 does not have ANY PC relative
   store instruciton, the hook 100% needs to
   overwrite and discard some value (we can
   not store to stack because SP is random)

   Thus the only real solution is to reserve
   one register. To test all registers, we
   must thus run the test two times, while
   not reseving the same register.

   For now, I will just use x16
*/
__attribute__((naked)) void inline_hook_template()
{
   asm volatile(
      // Load new SP (to x16)
      "ldr x16, #52           \n"
      "add x16, x16, #16384   \n"

      // Save old SP
      // Because of Arm shenanigans, you 
      // cannot use the SP as base register 
      // in str instructions, and I don't want to 
      // overwrite any other registers, so we temporarely
      // store x0 to the new sp (x16)
      "str x0, [x16, #-16]    \n" // Store original x0
      "mov x0, sp             \n"   
      // <== END UP HERE WITHOUT EXECUTING THE PREVIOUS INST!!!
      // HOW TF IS THAT POSSIBLE????!?!?
      "str x0, [x16, #-8]     \n" // Store original sp 
      "mov sp, x16            \n" // Update SP
      "ldr x0, [x16, #-16]    \n" // Load back original x0

      // Store return addr on stack
      "ldr x16, #32           \n"
      "str x16, [sp, #-16]    \n"

      // Jump to cb
      "ldr x16, #8   \n" // Load the absole address
      "br x16        \n" // Jump to the address
      ".int 0        \n" // To be used for
      ".int 0        \n" // callback addr

      ".int 0        \n" // Used for SP
      ".int 0        \n" //

      ".int 0        \n" // Used for Return address
      ".int 0        \n" //

      "ldr x16, [sp, #-8]     \n"
      "mov sp, x16            \n"
      // Set x16 = 0 to make it consistent!
      "mov x16, xzr           \n"
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
   writable_memory_block = original_binary;


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

   /* The 8:th instruction is always a branch instruction
      The file contains random data between that branch and
      its target that we must skip (some of the data could be
      identified as udf instructions, but we dont want to add
      hooks in the random data!)
   */
   /* B label ==> has 26 bit immidiate that is the pc relative 
      target position in number of instructions*/

   //fprintf(stderr, "B instr: %x\n", *(original_binary+7));
   uint64_t skip_next_n_instr = 0;
   //fprintf(stderr, "BRANCH OFFSET: %lx\n", branch_offset);
   //fprintf(stderr, "Skipping between x0%lx --> 0x%lx\n", random_data_sp, random_data_sp+branch_offset);
   while ((char *)curr_orig < (char *)original_binary + original_len)
   {
      uint64_t offset      = (uint64_t)curr_orig - (uint64_t)original_binary;
      uint64_t offset_new  = (uint64_t)curr_inlined - (uint64_t)tests_with_inline_hooks;
      uint32_t op_bits = (*curr_orig) >> 26;
      /* The imm is originally used to determine
         what type of test to execute, so we need to
         handle that as well */
      uint32_t imm = (*curr_orig) & 0xF;

      if (op_bits == 0 && skip_next_n_instr==0)
      {
         //fprintf(stderr, "FOUND SIGILL %lx, IMM: %x\t", offset, imm);
         uint64_t return_addr = (uint64_t)(curr_inlined + INLINE_HOOK_LENGTH - 3);
         uint32_t *template = NULL;
         return_addr |=  (uint64_t)imm<<60ull;
         template = (uint32_t *)inline_hook_template;

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
         *(uint64_t*)(curr_inlined + INLINE_HOOK_RETURN_OFFSET) = return_addr;
         

         for (int i = 0; i < INLINE_HOOK_LENGTH; i++)
         {
            //fprintf(stderr, "\tHook istr: %d : %x\n", i, *(curr_inlined + i));
         }
         curr_inlined += INLINE_HOOK_LENGTH;
      }
      else
      {
         //fprintf(stderr, "%lx ==> %lx: %x, op: %d\n", offset, offset_new, *curr_orig, op_bits);
         // If curr is branch, skip all instructions until the branch target instr
         if(!skip_next_n_instr && op_bits == 0b000101){
            uint32_t imm26 = (*curr_orig) & 0x03FFFFFF;
            skip_next_n_instr = imm26;
            //fprintf(stderr, "FOUND BRANCH, SKIPPING NEXT: 0x%x instr, INSTRUCTION: %08x\n ", skip_next_n_instr, *curr_orig);
         }

         // Normal instruction, just copy
         *curr_inlined = *curr_orig;
         curr_inlined++;
      }
      
      if(skip_next_n_instr) skip_next_n_instr--;

      curr_orig++;
   }

   /* Make the tests executable */
   mprotect(tests_with_inline_hooks, original_len * 5, PROT_EXEC | PROT_READ);
   close(fd);
   __builtin___clear_cache((char *)tests_with_inline_hooks, (char *)tests_with_inline_hooks + original_len * 5);
   return tests_with_inline_hooks;
}

static void set_x0(void *vuc, uint64_t x0)
{
   /* x0 is stored in the very start of the stack (uc) */
   uint64_t *uc = vuc;
   uc[0] = x0;
}

static int get_risuop(uint32_t insn)
{
   uint32_t op = insn & 0xf;
   return op;
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
      memblock = writable_memory_block;//(void *)ri.regs[0];
      fprintf(stderr, "Apprentice setting memblock %p\n", memblock);
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
   /* TODO: move. Set apprentice PC to master PC temporarely
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
   fprintf(stderr, "Got test with OP:%d\n", op);
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
      // Don't let it set it to the code memory! Instead
      // give it a value to writable memory!
      memblock = writable_memory_block;//(void *)master_ri.regs[0];
      fprintf(stderr, "Setting memblock: %p\n", memblock);
      break;
   case OP_GETMEMBLOCK:
      set_x0(uc, master_ri.regs[0] + (uintptr_t)memblock);
      fprintf(stderr, "Setting memblock: %p + 0x%lx\n", memblock, master_ri.regs[0]);
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
