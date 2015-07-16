/*
   main.c : Initializes the loader and launches the menu
   Copyright (C) 2015  hgoel0974

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */
#include <psp2/types.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/pss.h>
#include <stdio.h>
#include "utils/bithacks.h"
#include "vhl.h"
#include "nid_table.h"
#include "nidcache.h"
#include "elf_parser.h"
#include "state_machine.h"
#include "fs_hooks.h"
#include "loader.h"
#include "stub.h"

static globals_t *globals;

int __attribute__ ((section (".text.start")))
_start(UVL_Context *ctx)
{
        const uintptr_t vhlStubTop = (uintptr_t)getVhlStubTop();
        const uintptr_t vhlStubCtxBtm = vhlStubTop + vhlStubCtxSize;
        const uintptr_t vhlStubPrimaryBtm = vhlStubCtxBtm + vhlStubPrimarySize;

        const SceModuleImports * cachedImports[CACHED_IMPORTED_MODULE_NUM];
        nidTable_entry libkernelBase;
        SceModuleInfo *libkernelInfo;
        SceUID uid;
        void *p;
        int err;

        ctx->funcs.logline("Starting VHL...");

        // pss_* and puts won't work before this.
        ctx->funcs.logline("Resolving VHL Context Imports");
        nid_table_resolveVhlCtxImports((void *)vhlStubTop, vhlStubCtxSize, ctx);

        DEBUG_LOG_("Searching for SceLibKernel");
        if (nid_table_analyzeStub(ctx->ptrs.libkernel_anchor, 0, &libkernelBase) != ANALYZE_STUB_OK) {
                DEBUG_LOG_("Failed to find the base of SceLibKernel");
                return -1;
        }

        libkernelBase.value.i = B_UNSET(libkernelBase.value.i, 0);

        libkernelInfo = nid_table_findModuleInfo(libkernelBase.value.p, KERNEL_MODULE_SIZE, "SceLibKernel");
        if (libkernelInfo == NULL) {
                DEBUG_LOG_("Failed to find the module information of SceLibKernel");
                return -1;
        }

        DEBUG_LOG_("Searching for cached imports");
        nidCacheFindCachedImports(libkernelInfo, cachedImports);

        DEBUG_LOG_("Resolving VHL Primary Imports");
        nid_table_resolveVhlPrimaryImports((void *)vhlStubCtxBtm, vhlStubPrimarySize,
                                          libkernelInfo, cachedImports);

        DEBUG_LOG_("Initializing table");
        nid_storage_initialize();

        DEBUG_LOG_("Searching and Adding stubs to table...");
        nid_table_addAllStubs();

        DEBUG_LOG_("Resolving VHL Secondary Imports");
        nid_table_resolveVhlSecondaryImports((void *)vhlStubPrimaryBtm, vhlStubSecondarySize,
                                          libkernelInfo, cachedImports);

        DEBUG_LOG_("Adding stubs to table with cache");
        if (nid_table_addNIDCacheToTable(libkernelInfo, cachedImports) < 0)
                return -1;

        DEBUG_LOG_("Adding hooks to table");
        nid_table_addAllHooks();

        //TODO find a way to free unused memory

        uid = sceKernelAllocMemBlock("vhlGlobals", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                     FOUR_KB_ALIGN(sizeof(globals_t)), NULL);
        if (uid < 0) {
                DEBUG_LOG("Failed to allocate memory block 0x%08X", uid);
                return uid;
        }

        err = sceKernelGetMemBlockBase(uid, &p);

        if (err < 0) {
                DEBUG_LOG("Failed to retrive memory block 0x%08X", err);
                return uid;
        }

        pss_code_mem_unlock();
        globals = p;
        pss_code_mem_lock();

        block_manager_initialize();  //Initialize the elf block slots

        //TODO decide how to handle plugins

        config_initialize();

        DEBUG_LOG_("Loading menu...");

        if(elf_parser_load(globals->allocatedBlocks, "pss0:/top/Documents/homebrew.self", NULL) < 0) {
                internal_printf("Load failed!");
                return -1;
        }
        puts("Load succeeded! Launching!");

        elf_parser_start(globals->allocatedBlocks, 0);

        while(1) {
                //Delay thread and check for flags and things to update every once in a while, check for exit combination
                //calls.LogLine("Menu exited! Relaunching...");
                sceKernelDelayThread(16000);  //Update stuff once every 16 ms
        }

        return 0;
}

globals_t *getGlobals()
{
        return globals;
}
