#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#define ASM_NOP 0x90
#define ASM_HLT 0xF4

#define COLOR_DEFAULT EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK)
#define COLOR_GREEN   EFI_TEXT_ATTR(EFI_LIGHTGREEN, EFI_BLACK)
#define COLOR_RED     EFI_TEXT_ATTR(EFI_LIGHTRED, EFI_BLACK)
#define COLOR_YELLOW  EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLACK)
#define COLOR_CYAN    EFI_TEXT_ATTR(EFI_CYAN, EFI_BLACK)

#define ASM_VMMCALL_0 0x0F
#define ASM_VMMCALL_1 0x01
#define ASM_VMMCALL_2 0xD9

#define SIZE_HLT 1
#define SIZE_VMMCALL 3

#define EXIT_VMMCALL 0x81
#define EXIT_HLT     0x78

// en haut du fichier, global
uint64_t g_vmexit_count = 0;

// stub générique : fait VMMCALL puis RET
// VMMCALL met ExitCode=0x81, on retourne EFI_SUCCESS dans RAX
static uint8_t g_stub_code[] = {
    0x0F, 0x01, 0xD9,  // VMMCALL
    0xC3               // RET
};

// fausse EFI_BOOT_SERVICES : 76 pointeurs de fonctions (taille officielle)
// on pointe tout vers le même stub générique pour commencer
static uint64_t g_fake_boot_services[76];

// fausse EFI_SYSTEM_TABLE
static uint64_t g_fake_system_table[32];

typedef struct _PE_HEADERS
{
    uint64_t image_base_rva;   // where the PE wants to be loaded (relative)
    uint32_t entry_point_rva;  // offset of the real entry point inside the image
    uint32_t size_of_image;    // total size to reserve in memory
} PE_HEADERS;

int fill_VMCB(void *vmcb_ptr, EFI_SYSTEM_TABLE *SystemTable, EFI_HANDLE ImageHandle);
uint8_t* load_file(EFI_SYSTEM_TABLE *SystemTable, UINTN *out_size);
int      parse_pe_headers(uint8_t *file_buffer, PE_HEADERS *out);

uint64_t g_guest_program_size = 0;
uint64_t g_guest_code_base = 0;


typedef struct _VMCB_SEGMENT
{
    uint16_t Selector;
    uint16_t Attrib;
    uint32_t Limit;
    uint64_t Base;
} __attribute__((packed)) VMCB_SEGMENT;

typedef struct _VMCB_CONTROL_AREA
{
    uint16_t InterceptCrRead;
    uint16_t InterceptCrWrite;
    uint16_t InterceptDrRead;
    uint16_t InterceptDrWrite;
    uint32_t InterceptException;
    uint32_t InterceptMisc1;
    uint32_t InterceptMisc2;

    uint8_t  Reserved1[0x3c - 0x14];

    uint16_t PauseFilterThreshold;
    uint16_t PauseFilterCount;

    uint64_t IopmBasePa;
    uint64_t MsrpmBasePa;
    uint64_t TscOffset;

    uint32_t GuestAsid;
    uint8_t  TlbControl;
    uint8_t  Reserved2[3];

    uint8_t  Reserved3[0x70 - 0x60];

    uint64_t ExitCode;
    uint64_t ExitInfo1;
    uint64_t ExitInfo2;
    uint64_t ExitIntInfo;

    uint64_t NpEnable;

    uint8_t  Reserved5[0xb0 - 0x98]; // AVIC bar, GHCB, EVENTINJ — unused for now

    uint64_t NCr3; // physical address of the nested page tables (PML4)

    uint8_t  Reserved6[0x400 - 0xb8];
} __attribute__((packed)) VMCB_CONTROL_AREA;

typedef struct _VMCB_STATE_SAVE_AREA
{
    VMCB_SEGMENT Es;
    VMCB_SEGMENT Cs;
    VMCB_SEGMENT Ss;
    VMCB_SEGMENT Ds;
    VMCB_SEGMENT Fs;
    VMCB_SEGMENT Gs;
    VMCB_SEGMENT Gdtr;
    VMCB_SEGMENT Ldtr;
    VMCB_SEGMENT Idtr;
    VMCB_SEGMENT Tr;
    uint8_t  Reserved1[0xcb - 0xa0];
    uint8_t  Cpl;
    uint32_t Reserved2;
    uint64_t Efer;
    uint8_t  Reserved3[0x148 - 0xd8];
    uint64_t Cr4;
    uint64_t Cr3;
    uint64_t Cr0;
    uint64_t Dr7;
    uint64_t Dr6;
    uint64_t Rflags;
    uint64_t Rip;
    uint8_t  Reserved4[0x1d8 - 0x180];
    uint64_t Rsp;
    uint8_t  Reserved5[0x1f8 - 0x1e0];
    uint64_t Rax;
    uint64_t Star;
    uint64_t Lstar;
    uint64_t Cstar;
    uint64_t Sfmask;
    uint64_t KernelGsBase;
    uint64_t SysenterCs;
    uint64_t SysenterEsp;
    uint64_t SysenterEip;
    uint64_t Cr2;
    uint8_t  Reserved6[0x268 - 0x240];
    uint64_t GPat;
    uint64_t DbgCtl;
    uint64_t BrFrom;
    uint64_t BrTo;
    uint64_t LastExcepFrom;
    uint64_t LastExcepTo;
    uint8_t  Reserved7[0x3f8 - 0x298];
    uint64_t Rcx;
    uint64_t Rdx;
    uint64_t Rbx;
    uint64_t Reserved8;
    uint64_t Rbp;
    uint64_t Rsi;
    uint64_t Rdi;
    uint64_t R8;
    uint64_t R9;
    uint64_t R10;
    uint64_t R11;
    uint64_t R12;
    uint64_t R13;
    uint64_t R14;
    uint64_t R15;
} __attribute__((packed)) VMCB_STATE_SAVE_AREA;

typedef struct _VMCB
{
    VMCB_CONTROL_AREA ControlArea;
    VMCB_STATE_SAVE_AREA StateSaveArea;
} __attribute__((packed)) VMCB;

void *ft_memset(void *ptr, int value, UINTN size)
{
    UINT8 *p = (UINT8*)ptr;

    for (UINTN i = 0; i < size; i++)
        p[i] = (UINT8)value;

    return ptr;
}

void ft_bzero(void *ptr, UINTN size)
{
    uint64_t *p64 = (uint64_t*)ptr;
    UINTN count64 = size / 8;
    for (UINTN i = 0; i < count64; i++)
        p64[i] = 0;

    // handle remaining bytes
    uint8_t *p8 = (uint8_t*)(p64 + count64);
    UINTN rem = size % 8;
    for (UINTN i = 0; i < rem; i++)
        p8[i] = 0;
}

void set_color(EFI_SYSTEM_TABLE *SystemTable, UINTN color)
{
    SystemTable->ConOut->SetAttribute(SystemTable->ConOut, color);
}

EFI_STATUS allocate_runtime_pages(EFI_SYSTEM_TABLE *SystemTable, UINTN pages, void **address)
{
    return SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        pages,
        (EFI_PHYSICAL_ADDRESS *)address
    );
}

void set_vmcb_segment(VMCB_SEGMENT *segment, uint16_t selector, uint16_t attrib, uint32_t limit, uint64_t base)
{
    segment->Selector = selector;
    segment->Attrib = attrib;
    segment->Limit = limit;
    segment->Base = base;
}

uint8_t* load_pe(EFI_SYSTEM_TABLE *SystemTable, uint8_t *raw, UINTN raw_size, uint64_t *out_entry)
{
    if (raw[0] != 'M' || raw[1] != 'Z')
    {
        Print(L"load_pe: missing MZ signature.\n");
        return NULL;
    }

    uint32_t pe_offset     = *(uint32_t*)(raw + 0x3C);
    uint8_t  *pe           = raw + pe_offset;
    uint8_t  *opt          = pe + 4 + 20;

    uint64_t image_base    = *(uint64_t*)(opt + 24);
    uint32_t entry_rva     = *(uint32_t*)(opt + 16);
    uint32_t size_of_image = *(uint32_t*)(opt + 56);
    uint32_t headers_size  = *(uint32_t*)(opt + 60);
    uint16_t num_sections  = *(uint16_t*)(pe + 4 + 2);
    uint16_t opt_hdr_size  = *(uint16_t*)(pe + 4 + 16);

    Print(L"load_pe: SizeOfImage=0x%x sections=%d\n", size_of_image, (int)num_sections);

    // allocate mapped image — use AllocatePool, faster than page alloc for this
    uint8_t *base = NULL;
    EFI_STATUS status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData, size_of_image, (void**)&base
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"load_pe: AllocatePool failed.\n");
        return NULL;
    }

    // zero the whole image first using UEFI's own SetMem (very fast)
    SystemTable->BootServices->SetMem(base, size_of_image, 0);
    Print(L"load_pe: zeroed.\n");

    // copy headers
    SystemTable->BootServices->CopyMem(base, raw, headers_size);
    Print(L"load_pe: headers copied.\n");

    // copy each section using CopyMem
    uint8_t *sections = pe + 4 + 20 + opt_hdr_size;
    for (uint16_t i = 0; i < num_sections; i++)
    {
        uint8_t  *sec       = sections + i * 40;
        uint32_t virt_addr  = *(uint32_t*)(sec + 12);
        uint32_t raw_size   = *(uint32_t*)(sec + 16);
        uint32_t raw_offset = *(uint32_t*)(sec + 20);

        if (raw_size == 0)
            continue;

        SystemTable->BootServices->CopyMem(base + virt_addr, raw + raw_offset, raw_size);
    }
    Print(L"load_pe: sections copied.\n");

    // apply relocations if we didn't land at preferred ImageBase
    uint64_t delta = (uint64_t)(uintptr_t)base - image_base;
    if (delta != 0)
    {
        uint32_t reloc_rva  = *(uint32_t*)(opt + 128);
        uint32_t reloc_size = *(uint32_t*)(opt + 132);

        uint8_t *reloc     = base + reloc_rva;
        uint8_t *reloc_end = reloc + reloc_size;

        while (reloc < reloc_end)
        {
            uint32_t  page_rva   = *(uint32_t*)(reloc + 0);
            uint32_t  block_size = *(uint32_t*)(reloc + 4);
            if (block_size == 0) break;
            uint16_t *entries    = (uint16_t*)(reloc + 8);
            uint32_t  num_entries = (block_size - 8) / 2;

            for (uint32_t e = 0; e < num_entries; e++)
            {
                uint16_t entry  = entries[e];
                uint8_t  type   = entry >> 12;
                uint16_t offset = entry & 0x0FFF;

                // type 10 = DIR64: patch a 64-bit absolute pointer
                if (type == 10)
                {
                    uint64_t *ptr = (uint64_t*)(base + page_rva + offset);
                    *ptr += delta;
                }
            }
            reloc += block_size;
        }
        Print(L"load_pe: relocations done. delta=0x%lx\n", delta);
    }

    *out_entry = (uint64_t)(uintptr_t)base + entry_rva;
    Print(L"PE loaded at: %p entry: 0x%lx\n", base, *out_entry);
    return base;
}

int Starting_Screen(EFI_SYSTEM_TABLE *SystemTable)
{
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 0, 0);

    set_color(SystemTable, EFI_TEXT_ATTR(EFI_LIGHTBLUE, EFI_BLACK));

    Print(L"\n");
    Print(L"   =============================================================================================\n");
    Print(L"  ||                                                                                            ||\n");

    set_color(SystemTable, EFI_TEXT_ATTR(EFI_CYAN, EFI_BLACK));

    Print(L"  ||   ██╗  ██╗██╗   ██╗██████╗ ███████╗██████╗ ██╗   ██╗██╗███████╗ ██████╗ ██████╗            ||\n");
    Print(L"  ||   ██║  ██║╚██╗ ██╔╝██╔══██╗██╔════╝██╔══██╗██║   ██║██║██╔════╝██╔═══██╗██╔══██╗           ||\n");
    Print(L"  ||   ███████║ ╚████╔╝ ██████╔╝█████╗  ██████╔╝██║   ██║██║███████╗██║   ██║██████╔╝           ||\n");
    Print(L"  ||   ██╔══██║  ╚██╔╝  ██╔═══╝ ██╔══╝  ██╔══██╗╚██╗ ██╔╝██║╚════██║██║   ██║██╔══██╗           ||\n");
    Print(L"  ||   ██║  ██║   ██║   ██║     ███████╗██║  ██║ ╚████╔╝ ██║███████║╚██████╔╝██║  ██║          ||\n");
    Print(L"  ||   ╚═╝  ╚═╝   ╚═╝   ╚═╝     ╚══════╝╚═╝  ╚═╝  ╚═══╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝           ||\n");

    set_color(SystemTable, EFI_TEXT_ATTR(EFI_LIGHTBLUE, EFI_BLACK));

    Print(L"  ||                                                                                           ||\n");
    Print(L"  ||                                                                                           ||\n");
    Print(L"  ||                                                                                           ||\n");
    Print(L"  ||                                                                                           ||\n");
    Print(L"   =============================================================================================\n");

    Print(L"\n");

    set_color(SystemTable, COLOR_DEFAULT);
    Print(L"      Initializing");

    for (int i = 0; i < 3; i++)
    {
        Print(L".");
        SystemTable->BootServices->Stall(350000);
    }

    Print(L"\n\n");

    set_color(SystemTable, EFI_TEXT_ATTR(EFI_LIGHTBLUE, EFI_BLACK));
    Print(L"      [");

    for (int i = 0; i < 40; i++)
    {
        Print(L"#");
        SystemTable->BootServices->Stall(40000);
    }

    Print(L"]\n");

    set_color(SystemTable, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
    Print(L"\n      Status: Ready\n");

    SystemTable->BootServices->Stall(100000);

    set_color(SystemTable, COLOR_DEFAULT);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

    return 0;
}

int check_svm_support() //SVM — Secure Virtual Machine
{
    uint32_t eax, ebx, ecx, edx;

    eax = 0x80000001;

    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(eax)
    );

    if (!(ecx & (1 << 2))) // read bit SVMDIS
    {
        Print(L"SVM is not supported on this CPU.\n");
        return 0;
    }
    else
    {
        Print(L"SVM is supported on this CPU.\n");
        return 1;
    }
}

uint64_t read_msr(uint32_t msr) //  MSR (Model-Specific Register)
{ 
    uint32_t low, high;
    __asm__ __volatile__(
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );
    return ((uint64_t)high << 32) | low;
}

int check_svm_locked()
{
    uint64_t vm_cr = read_msr(0xC0010114);

    if (vm_cr & (1 << 4))
    {
        Print(L"SVM is locked on this CPU.\n");
        return 0;
    }
    else
    {
        Print(L"SVM is not locked on this CPU.\n");
        return 1;
    }
}

void write_msr(uint32_t msr, uint64_t value) //  MSR (Model-Specific Register)
{
    uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ __volatile__(
        "wrmsr"
        :
        : "c"(msr), "a"(low), "d"(high)
    );
}

void enable_svm()
{
    uint64_t efer = read_msr(0xC0000080);
    efer |= (1ULL << 12);
    write_msr(0xC0000080, efer);

    uint64_t check = read_msr(0xC0000080);
    if (check & (1ULL << 12))
    {
        Print(L"SVM enabled successfully (EFER.SVME set).\n");
    }
    else
    {
        Print(L"Failed to enable SVM.\n");
    }
}

void* init_VMCB(EFI_SYSTEM_TABLE *SystemTable)
{
    void *vmcb;
    EFI_STATUS status = 0;
    // type of allocation
    // memory type for runtime services
    // number of pages to allocate
    // address of the allocated page
    status = allocate_runtime_pages(SystemTable, 1, &vmcb); // Allocate 1 page of 4KB for VMCB
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate VMCB. Status: %d\n", status);
        return NULL;
    }
    Print(L"VMCB allocated at address: %p\n", vmcb);
    return vmcb;
}

void* init_HostSaveArea(EFI_SYSTEM_TABLE *SystemTable)
{
    void *host_save_area;
    EFI_STATUS status = 0;
    // type of allocation
    // memory type for runtime services
    // number of pages to allocate
    // address of the allocated page
    status = allocate_runtime_pages(SystemTable, 1, &host_save_area); // Allocate 1 page of 4KB for HOST SAVE AREA
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate HOST SAVE AREA. Status: %d\n", status);
        return NULL;
    }
    Print(L"HOST SAVE AREA allocated at address: %p\n", host_save_area);
    return host_save_area;
}

uint64_t* init_GDT(EFI_SYSTEM_TABLE *SystemTable)
{
    void *gdt = NULL;
    EFI_STATUS status;

    status = allocate_runtime_pages(SystemTable, 1, &gdt);
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate GDT. Status: %d\n", status);
        return NULL;
    }

    uint64_t *entries = (uint64_t*)gdt;

    entries[0] = 0x0000000000000000ULL; // null descriptor, always required at index 0
    entries[1] = 0x00209A0000000000ULL; // 64-bit code segment, ring 0, executable/readable
    entries[2] = 0x0000920000000000ULL; // 64-bit data segment, ring 0, writable

    Print(L"GDT built at: %p\n", gdt);
    return entries;
}

uint64_t* init_IDT(EFI_SYSTEM_TABLE *SystemTable)
{
    void *idt = NULL;
    EFI_STATUS status;

    status = allocate_runtime_pages(SystemTable, 1, &idt);
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate IDT. Status: %d\n", status);
        return NULL;
    }

    // 256 entries x 16 bytes = 4096 bytes = exactly 1 page
    // all zeroed = all entries "not present", any exception without
    // a real handler will cause a clean #VMEXIT instead of undefined behavior
    ft_bzero(idt, 4096);

    Print(L"IDT built at: %p\n", idt);
    return (uint64_t*)idt;
}

uint64_t init_rip(void *rip_ptr)
{
    uint8_t *code = (uint8_t*)rip_ptr;

    uint64_t offset = 0;

    code[offset++] = ASM_NOP;
    code[offset++] = ASM_NOP;

    code[offset++] = ASM_VMMCALL_0;
    code[offset++] = ASM_VMMCALL_1;
    code[offset++] = ASM_VMMCALL_2;

    code[offset++] = ASM_NOP;
    code[offset++] = ASM_NOP;
    code[offset++] = ASM_NOP;

    code[offset++] = ASM_HLT;

    code[offset++] = ASM_VMMCALL_0;
    code[offset++] = ASM_VMMCALL_1;
    code[offset++] = ASM_VMMCALL_2;

    code[offset++] = ASM_HLT;

    return offset; 
}

void* init_GuestPageTables(EFI_SYSTEM_TABLE *SystemTable)
{
    void *pml4 = NULL;
    void *pdpt = NULL;
    EFI_STATUS status;

    status = allocate_runtime_pages(SystemTable, 1, &pml4);
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate guest PML4. Status: %d\n", status);
        return NULL;
    }

    status = allocate_runtime_pages(SystemTable, 1, &pdpt);
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate guest PDPT. Status: %d\n", status);
        return NULL;
    }

    uint64_t *pml4_entries = (uint64_t*)pml4;
    uint64_t *pdpt_entries = (uint64_t*)pdpt;
    ft_bzero(pml4_entries, 512 * sizeof(uint64_t));
    ft_bzero(pdpt_entries, 512 * sizeof(uint64_t));

    // same identity-map pattern as NPT: guest virtual == guest physical
    pml4_entries[0] = ((uint64_t)(uintptr_t)pdpt) | 0x7;

    for (int i = 0; i < 4; i++)
    {
        uint64_t phys_1gb_base = (uint64_t)i * 0x40000000ULL;
        pdpt_entries[i] = phys_1gb_base | 0x87;
    }

    Print(L"Guest page tables built. PML4 at: %p\n", pml4);
    return pml4;
}

int check_1gb_pages_support()
{
    uint32_t eax, ebx, ecx, edx;
    eax = 0x80000001;

    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(eax)
    );

    if (!(edx & (1 << 26))) // Page1GB feature bit
    {
        Print(L"1GB pages not supported on this CPU.\n");
        return 0;
    }
    else
    {
        Print(L"1GB pages supported on this CPU.\n");
        return 1;
    }
}

uint64_t get_highest_physical_address(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;

    // First call: just asks for the required buffer size (fails on purpose)
    status = SystemTable->BootServices->GetMemoryMap(
        &map_size, memory_map, &map_key, &descriptor_size, &descriptor_version
    );

    // add some margin, the map can grow between the two calls
    map_size += 2 * descriptor_size;

    status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData, map_size, (void**)&memory_map
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate memory map buffer. Status: %d\n", status);
        return 0;
    }

    // Second call: actually fills the buffer this time
    status = SystemTable->BootServices->GetMemoryMap(
        &map_size, memory_map, &map_key, &descriptor_size, &descriptor_version
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to get memory map. Status: %d\n", status);
        return 0;
    }

    uint64_t highest = 0;
    UINTN entry_count = map_size / descriptor_size;

    for (UINTN i = 0; i < entry_count; i++)
    {
        EFI_MEMORY_DESCRIPTOR *entry = (EFI_MEMORY_DESCRIPTOR*)(
            (uint8_t*)memory_map + i * descriptor_size
        );

        uint64_t entry_end = entry->PhysicalStart + (entry->NumberOfPages * 4096ULL);
        if (entry_end > highest)
        {
            highest = entry_end;
        }
    }

    Print(L"Highest physical address found: 0x%lx\n", highest);
    return highest;
}

void* init_NPT(EFI_SYSTEM_TABLE *SystemTable)
{
    if (!check_1gb_pages_support())
    {
        Print(L"Cannot build NPT without 1GB page support.\n");
        return NULL;
    }

    uint64_t highest_addr = get_highest_physical_address(SystemTable);
    if (highest_addr == 0)
    {
        Print(L"Failed to determine RAM size, aborting NPT build.\n");
        return NULL;
    }

    // round up to the next full GB, then convert to a number of 1GB entries
    uint64_t gb_to_map = (highest_addr + 0x40000000ULL - 1) / 0x40000000ULL;
    Print(L"Mapping %d GB via NPT.\n", (int)gb_to_map);

    void *pml4 = NULL;
    void *pdpt = NULL;
    EFI_STATUS status;

    status = allocate_runtime_pages(SystemTable, 1, &pml4);
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate NPT PML4. Status: %d\n", status);
        return NULL;
    }

    status = allocate_runtime_pages(SystemTable, 1, &pdpt);
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate NPT PDPT. Status: %d\n", status);
        return NULL;
    }

    uint64_t *pml4_entries = (uint64_t*)pml4;
    uint64_t *pdpt_entries = (uint64_t*)pdpt;
    ft_bzero(pml4_entries, 512 * sizeof(uint64_t));
    ft_bzero(pdpt_entries, 512 * sizeof(uint64_t));

    pml4_entries[0] = ((uint64_t)(uintptr_t)pdpt) | 0x7;

    // map exactly as many 1GB entries as needed to cover all real RAM
    for (uint64_t i = 0; i < gb_to_map && i < 512; i++)
    {
        uint64_t phys_1gb_base = i * 0x40000000ULL;
        pdpt_entries[i] = phys_1gb_base | 0x87;
    }

    Print(L"NPT built. PML4 at: %p, PDPT at: %p\n", pml4, pdpt);

    return pml4;
}

int fill_VMCB(void *vmcb_ptr, EFI_SYSTEM_TABLE *SystemTable, EFI_HANDLE ImageHandle)
{
    VMCB *vmcb = (VMCB*)vmcb_ptr;

    // Clear the VMCB to ensure a clean state
    ft_bzero(vmcb, 4096);

    vmcb->ControlArea.InterceptMisc1 = (1 << 24);
    vmcb->ControlArea.InterceptMisc2 = (1 << 0) | (1 << 1);

    // ASID obligatoire
    vmcb->ControlArea.GuestAsid = 1;
    vmcb->ControlArea.TlbControl = 1;

    // etat initial du guest (State Save Area)

    // RFLAGS: bit 1 always set to 1 (reserved by x86), no other flag active
    vmcb->StateSaveArea.Rflags = 0x2;

    // CR0: keep previous bits (ET, CD, NW), add PE (protected mode) + PG (paging)
    vmcb->StateSaveArea.Cr0 = 0x60000010 | 0x80000001;

    // EFER: SVME (bit12, mandatory) + LME (bit8, long mode enable) + LMA (bit10, long mode active)
    // LMA must be set explicitly here since VMRUN loads guest EFER as-is, it does not
    // auto-compute LMA from PG+LME like a real mode transition would
    vmcb->StateSaveArea.Efer = (1ULL << 12) | (1ULL << 8) | (1ULL << 10);

    vmcb->ControlArea.InterceptException = (1 << 13) | (1 << 14);

    // CS: code segment, executable + readable + present
    // Attrib 0x0A9B = same as before (present, code, exec/read) but with L bit set (long mode)
    // AMD manual: only D, L, R bits of CS are observed by hardware — this is the one that matters here
    set_vmcb_segment(&vmcb->StateSaveArea.Cs, 0x0000, 0x0A9B, 0xFFFF, 0x00000000);

    // SS: stack segment, data + writable + present
    set_vmcb_segment(&vmcb->StateSaveArea.Ss, 0x0000, 0x0093, 0xFFFF, 0x00000000);

    // DS: data segment, same attributes as SS
    set_vmcb_segment(&vmcb->StateSaveArea.Ds, 0x0000, 0x0093, 0xFFFF, 0x00000000);

    // ES: extra data segment, same attributes as SS/DS
    set_vmcb_segment(&vmcb->StateSaveArea.Es, 0x0000, 0x0093, 0xFFFF, 0x00000000);

    // FS: extra segment, same attributes as SS/DS
    set_vmcb_segment(&vmcb->StateSaveArea.Fs, 0x0000, 0x0093, 0xFFFF, 0x00000000);

    // GS: extra segment, same attributes as SS/DS
    set_vmcb_segment(&vmcb->StateSaveArea.Gs, 0x0000, 0x0093, 0xFFFF, 0x00000000);

    // GDTR: real GDT built via init_GDT(), with code (0x08) and data (0x10) segments
    uint64_t *gdt = init_GDT(SystemTable);
    if (gdt == NULL)
    {
        Print(L"Failed to build GDT, aborting VMCB fill.\n");
        return 0;
    }
    vmcb->StateSaveArea.Cs.Selector = 0x08; // index 1 in GDT (code segment)
    vmcb->StateSaveArea.Ds.Selector = 0x10;
    vmcb->StateSaveArea.Ss.Selector = 0x10; // index 2 in GDT (data segment)
    vmcb->StateSaveArea.Gdtr.Attrib   = 0x0000;
    vmcb->StateSaveArea.Gdtr.Limit    = (3 * 8) - 1; // 3 entries of 8 bytes each, limit = size-1
    vmcb->StateSaveArea.Gdtr.Base     = (uint64_t)(uintptr_t)gdt;

    // IDTR: 256 entries of 16 bytes each (64-bit IDT gate size), all present=0 for now
    uint64_t *idt = init_IDT(SystemTable);
    if (idt == NULL)
    {
        Print(L"Failed to build IDT, aborting VMCB fill.\n");
        return 0;
    }
    vmcb->StateSaveArea.Idtr.Selector = 0x0000;
    vmcb->StateSaveArea.Idtr.Attrib   = 0x0000;
    vmcb->StateSaveArea.Idtr.Limit    = (256 * 16) - 1;
    vmcb->StateSaveArea.Idtr.Base     = (uint64_t)(uintptr_t)idt;

    // TR: task register, present + 32-bit TSS type, not really used by this guest
    vmcb->StateSaveArea.Tr.Selector = 0x0000;
    vmcb->StateSaveArea.Tr.Attrib   = 0x008B;
    vmcb->StateSaveArea.Tr.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Tr.Base     = 0x00000000;

    // CPL: guest starts at ring 0 (most privileged)
    vmcb->StateSaveArea.Cpl = 0;

    // CR4/CR3: no paging, no extended features yet (NPT disabled, guest uses no page tables)
    // CR4.PAE (bit5) is mandatory for long mode
    vmcb->StateSaveArea.Cr4 = (1ULL << 5) | (1ULL << 9) | (1ULL << 10);
    vmcb->StateSaveArea.Cr3 = 0;

    // DR6/DR7: default reset values defined by the x86 spec for debug registers
    vmcb->StateSaveArea.Dr6 = 0xFFFF0FF0;
    vmcb->StateSaveArea.Dr7 = 0x00000400;

    void *guest_stack = NULL;
    EFI_STATUS status;

    status = allocate_runtime_pages(SystemTable, 4, &guest_stack);
    if (status != EFI_SUCCESS || guest_stack == NULL)
    {
        Print(L"Failed to allocate guest stack. Status: %d\n", status);
        return 0;
    }
    Print(L"Guest stack allocated at: %p\n", guest_stack);
    SystemTable->BootServices->SetMem(guest_stack, 4 * 4096, 0);

    // stack grows downward — RSP points to the top
    vmcb->StateSaveArea.Ss.Base = 0;
    vmcb->StateSaveArea.Rsp = (uint64_t)(uintptr_t)guest_stack + 4 * 4096 - 0x10;

    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate guest code. Status: %d\n", status);
        return 0;
    }

    UINTN file_size = 0;
    uint8_t *raw_pe = load_file(SystemTable, &file_size);
    if (raw_pe == NULL)
    {
        Print(L"Failed to load bootmgfw.efi.\n");
        return 0;
    }

    uint64_t entry_point = 0;
    uint8_t *mapped_pe = load_pe(SystemTable, raw_pe, file_size, &entry_point);
    if (mapped_pe == NULL)
    {
        Print(L"Failed to map PE.\n");
        return 0;
    }

    g_guest_program_size = file_size;
    g_guest_code_base    = (uint64_t)(uintptr_t)mapped_pe;
    vmcb->StateSaveArea.Rip = entry_point;

    // In 64-bit mode, segment bases are ignored by the CPU (except FS/GS).
    vmcb->StateSaveArea.Cs.Base = 0;

    vmcb->StateSaveArea.Ss.Base = 0;

    void *guest_pt = init_GuestPageTables(SystemTable);
    if (guest_pt == NULL)
    {
        Print(L"Failed to build guest page tables, aborting VMCB fill.\n");
        return 0;
    }
    vmcb->StateSaveArea.Cr3 = (uint64_t)(uintptr_t)guest_pt;
    
    // IOPM: I/O Permission Map, 3 pages, required by VMRUN even with no I/O intercepted
    void *iopm = NULL;

    status = allocate_runtime_pages(SystemTable, 3, &iopm);

    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate IOPM. Status: %d\n", status);
        return 0;
    }

    // zeroed IOPM = no I/O port is intercepted
    ft_bzero(iopm, 3 * 4096);

    vmcb->ControlArea.IopmBasePa = (uint64_t)(uintptr_t)iopm;
    Print(L"IOPM allocated at address: %p\n", iopm);

    // MSRPM: MSR Permission Map, 2 pages, required by VMRUN even with no MSR intercepted
    void *msrpm = NULL;

    status = allocate_runtime_pages(SystemTable, 2, &msrpm);

    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate MSRPM. Status: %d\n", status);
        return 0;
    }

    // zeroed MSRPM = no MSR access is intercepted
    ft_bzero(msrpm, 2 * 4096);

    vmcb->ControlArea.MsrpmBasePa = (uint64_t)(uintptr_t)msrpm;
    Print(L"MSRPM allocated at address: %p\n", msrpm);

    // Nested Paging setup: identity-map the first 4GB so the guest sees
    // the same physical memory as the host, via NPT translation
    void *npt_pml4 = init_NPT(SystemTable);
    if (npt_pml4 == NULL)
    {
        Print(L"Failed to build NPT, aborting VMCB fill.\n");
        return 0;
    }

    vmcb->ControlArea.NpEnable = 1;
    vmcb->ControlArea.NCr3 = (uint64_t)(uintptr_t)npt_pml4;

    // bootmgfw.efi entry point follows UEFI calling convention:
    // RCX = ImageHandle, RDX = pointer to EFI_SYSTEM_TABLE
    // we pass our own host handles for now — simple but functional as a first step
    void *stub_page = NULL;
    status = allocate_runtime_pages(SystemTable, 1, &stub_page);
    if (status != EFI_SUCCESS) { Print(L"stub alloc failed\n"); return 0; }
    SystemTable->BootServices->CopyMem(stub_page, g_stub_code, sizeof(g_stub_code));
    uint64_t stub_addr = (uint64_t)(uintptr_t)stub_page;
    Print(L"Stub at: %p\n", stub_page);

    // 2. Fausse EFI_BOOT_SERVICES : tous les pointeurs → stub
    void *fbs_page = NULL;
    status = allocate_runtime_pages(SystemTable, 1, &fbs_page);
    if (status != EFI_SUCCESS) { Print(L"fbs alloc failed\n"); return 0; }
    uint64_t *fbs = (uint64_t*)fbs_page;
    ft_bzero(fbs, 4096);
    for (int i = 0; i < 512; i++)
        fbs[i] = stub_addr;

    // 3. Fausse EFI_SYSTEM_TABLE
    void *fst_page = NULL;
    status = allocate_runtime_pages(SystemTable, 1, &fst_page);
    if (status != EFI_SUCCESS) { Print(L"fst alloc failed\n"); return 0; }
    uint64_t *fst = (uint64_t*)fst_page;
    ft_bzero(fst, 4096);
    // EFI_SYSTEM_TABLE : BootServices est à offset 0x60 = index 12
    fst[12] = (uint64_t)(uintptr_t)fbs;
    Print(L"Fake ST at: %p, Fake BS at: %p\n", fst_page, fbs_page);

    // 4. Passer la fausse table au guest
    vmcb->StateSaveArea.Rcx = (uint64_t)(uintptr_t)ImageHandle;
    vmcb->StateSaveArea.Rdx = (uint64_t)(uintptr_t)fst_page;

    return 1;
}

void svm_vmrun(void *vmcb)
{
    __asm__ __volatile__(
        ".byte 0x0F, 0x01, 0xD8"   // VMRUN
        :
        : "a"((uint64_t)(uintptr_t)vmcb)
        : "memory"
    );
}

void svm_stgi()
{
    __asm__ __volatile__(
        ".byte 0x0F, 0x01, 0xDC"
        :
        :
        : "memory"
    );
}

uint8_t* load_file(EFI_SYSTEM_TABLE *SystemTable, UINTN *out_size)
{
    EFI_STATUS status;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = 0;
    uint8_t *buffer = NULL;

    // 1. Accéder au système de fichiers
    status = SystemTable->BootServices->LocateProtocol(
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        (void**)&fs
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"LocateProtocol(SimpleFileSystem) failed: %d\n", status);
        return NULL;
    }

    // 2. Ouvrir le volume racine
    status = fs->OpenVolume(fs, &root);
    if (status != EFI_SUCCESS)
    {
        Print(L"OpenVolume failed: %d\n", status);
        return NULL;
    }

    // 3. Ouvrir le fichier cible
    status = root->Open(
        root,
        &file,
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
        EFI_FILE_MODE_READ,
        0
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"Open(bootmgfw.efi) failed: %d\n", status);
        return NULL;
    }

    // 4. Lire la taille du fichier
    status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, NULL);
    if (status == EFI_BUFFER_TOO_SMALL)
    {
        SystemTable->BootServices->AllocatePool(EfiLoaderData, info_size, (void**)&info);
        status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    }
    if (status != EFI_SUCCESS)
    {
        Print(L"GetInfo failed: %d\n", status);
        return NULL;
    }

    UINTN file_size = info->FileSize;
    Print(L"bootmgfw.efi size: %d bytes\n", (int)file_size);

    // 5. Allouer la RAM et lire les octets
    SystemTable->BootServices->AllocatePool(EfiLoaderData, file_size, (void**)&buffer);
    status = file->Read(file, &file_size, buffer);
    if (status != EFI_SUCCESS)
    {
        Print(L"Read failed: %d\n", status);
        return NULL;
    }

    file->Close(file);
    *out_size = file_size;

    Print(L"bootmgfw.efi loaded at: %p\n", buffer);
    return buffer;
}

// reserved for future NPT setup (reading guest/host CR state for page table work)
uint64_t read_cr0()
{
    uint64_t value;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(value));
    return value;
}

uint64_t read_cr3()
{
    uint64_t value;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(value));
    return value;
}

uint64_t read_cr4()
{
    uint64_t value;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(value));
    return value;
}

int parse_pe_headers(uint8_t *file_buffer, PE_HEADERS *out)
{
    // DOS header: first 2 bytes must be "MZ" (0x5A4D)
    if (file_buffer[0] != 'M' || file_buffer[1] != 'Z')
    {
        Print(L"Not a valid PE file (missing MZ signature).\n");
        return 0;
    }

    // offset to the real PE header is stored at offset 0x3C in the DOS header
    uint32_t pe_offset = *(uint32_t*)(file_buffer + 0x3C);
    uint8_t *pe_header = file_buffer + pe_offset;

    // "PE\0\0" signature check
    if (pe_header[0] != 'P' || pe_header[1] != 'E')
    {
        Print(L"Not a valid PE file (missing PE signature).\n");
        return 0;
    }

    // Optional Header starts right after: COFF header (24 bytes) + PE signature (4 bytes)
    uint8_t *optional_header = pe_header + 4 + 20;

    // For PE32+ (64-bit), these fields are at fixed offsets in the Optional Header
    out->entry_point_rva = *(uint32_t*)(optional_header + 16); // AddressOfEntryPoint
    out->image_base_rva  = *(uint64_t*)(optional_header + 24); // ImageBase
    out->size_of_image   = *(uint32_t*)(optional_header + 56); // SizeOfImage

    Print(L"PE parsed: EntryPoint RVA=0x%x, ImageBase=0x%lx, SizeOfImage=0x%x\n",
        out->entry_point_rva, out->image_base_rva, out->size_of_image);

    return 1;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    void *vmcb = NULL;
    void *host_save_area = NULL;

    set_color(SystemTable, COLOR_DEFAULT);

    InitializeLib(ImageHandle, SystemTable);
    //Starting_Screen(SystemTable);
    Print(L"EFI loaded.\n");

    if (!check_svm_support())
    {
        Print(L"Exiting due to lack of SVM support.\n");
        return EFI_UNSUPPORTED;
    }

    if (!check_svm_locked())
    {
        Print(L"Exiting due to SVM being locked.\n");
        return EFI_UNSUPPORTED;
    }

    enable_svm();

    vmcb = init_VMCB(SystemTable);
    if (vmcb == NULL)
    {
        Print(L"Exiting due to VMCB allocation failure.\n");
        return EFI_OUT_OF_RESOURCES;
    }

    host_save_area = init_HostSaveArea(SystemTable);
    if (host_save_area == NULL)
    {
        Print(L"Exiting due to HOST SAVE AREA allocation failure.\n");
        return EFI_OUT_OF_RESOURCES;
    }

    if (!fill_VMCB(vmcb, SystemTable, ImageHandle))
    {
        Print(L"Exiting due to VMCB setup failure.\n");
        return EFI_ABORTED;
    }

    Print(L"VMCB filled.\n");

    // indicate the place of host save area and VMCB to the user
    write_msr(0xC0010117, (uint64_t)(uintptr_t)host_save_area); // MSR_VM_HSAVE_PA
    Print(L"VM_HSAVE_PA configured.\n");

    VMCB *v = (VMCB*)vmcb;

    Print(L"Before VMRUN.\n");

    while (1)
    {
        Print(L"\nBefore VMRUN, RIP: 0x%lx / size: 0x%lx\n",
            v->StateSaveArea.Rip,
            g_guest_program_size);

        svm_vmrun(vmcb);
        g_vmexit_count++;
        Print(L"[#%d] ExitCode=0x%lx RIP=0x%lx\n",
            (int)g_vmexit_count,
            v->ControlArea.ExitCode,
            v->StateSaveArea.Rip);

        uint64_t exit_code = v->ControlArea.ExitCode;
        Print(L"ExitCode: 0x%lx\n", exit_code);
        Print(L"Guest RIP after VMEXIT: 0x%lx\n", v->StateSaveArea.Rip);

        if (exit_code == EXIT_VMMCALL)
        {
            // bootmgfw a appelé un Boot Service via notre stub
            // RCX = premier arg, RDX = deuxième arg, etc.
            set_color(SystemTable, COLOR_CYAN);
            Print(L"BootService call: RCX=0x%lx RDX=0x%lx R8=0x%lx\n",
                v->StateSaveArea.Rcx,
                v->StateSaveArea.Rdx,
                v->StateSaveArea.R8);
            set_color(SystemTable, COLOR_DEFAULT);
            v->StateSaveArea.Rax = 0; // EFI_SUCCESS par défaut
            v->StateSaveArea.Rip += SIZE_VMMCALL;
        }
        else if (exit_code == EXIT_HLT)
        {
            set_color(SystemTable, COLOR_GREEN);
            Print(L"HLT intercepted.\n");
            set_color(SystemTable, COLOR_DEFAULT);
            // skip past the 1-byte HLT instruction
            v->StateSaveArea.Rip += SIZE_HLT;
        }
        else if (exit_code >= 0x00 && exit_code <= 0x1F)
        {
            // guest touched a control register (CR0, CR3, CR4...)
            // bits 3-0 of ExitInfo1 = which CR, bit 4 = read(0) or write(1)
            // CPU already advanced RIP before exiting, no manual skip needed
            set_color(SystemTable, COLOR_YELLOW);
            Print(L"CR access: CR%d, write=%d — skipping.\n",
                (int)(v->ControlArea.ExitInfo1 & 0xF),
                (int)((v->ControlArea.ExitInfo1 >> 4) & 1));
            set_color(SystemTable, COLOR_DEFAULT);
        }
        else if (exit_code == 0x7C || exit_code == 0x7D)
        {
            // 0x7C = RDMSR, 0x7D = WRMSR
            // RDMSR/WRMSR are 2 bytes (0F 32 / 0F 30), CPU does NOT skip RIP for us
            set_color(SystemTable, COLOR_YELLOW);
            Print(L"MSR %s: MSR=0x%lx — skipping.\n",
                exit_code == 0x7C ? L"read" : L"write",
                v->StateSaveArea.Rax);
            set_color(SystemTable, COLOR_DEFAULT);
            v->StateSaveArea.Rip += 2;
        }
        else if (exit_code == 0x400)
        {
            // guest tried to access a physical address not covered by our NPT
            // ExitInfo2 holds the faulty address — this is a real problem, stop here
            set_color(SystemTable, COLOR_RED);
            Print(L"#NPF at guest physical: 0x%lx — stopping.\n",
                v->ControlArea.ExitInfo2);
            set_color(SystemTable, COLOR_DEFAULT);
            break;
        }
        else if (exit_code == 0x7F)
        {
            uint8_t *rip_bytes = (uint8_t*)(uintptr_t)v->StateSaveArea.Rip;
            Print(L"#UD bytes: %02x %02x %02x %02x at RIP=0x%lx\n",
                rip_bytes[0], rip_bytes[1], rip_bytes[2], rip_bytes[3],
                v->StateSaveArea.Rip);

            if (rip_bytes[0] == 0xF3 && rip_bytes[1] == 0x0F &&
                rip_bytes[2] == 0x1E && rip_bytes[3] == 0xFA)
            {
                v->StateSaveArea.Rip += 4;
            }
            else if (v->StateSaveArea.Rip < 0x1000)
            {
                uint64_t *guest_rsp = (uint64_t*)(uintptr_t)v->StateSaveArea.Rsp;
                uint64_t ret_addr   = *guest_rsp;
                v->StateSaveArea.Rsp += 8;
                v->StateSaveArea.Rip  = ret_addr;
                v->StateSaveArea.Rax  = 0x8000000000000003ULL;
                Print(L"Null call → returning to 0x%lx\n", ret_addr);
            }
            else
            {
                set_color(SystemTable, COLOR_RED);
                Print(L"#UD at RIP=0x%lx — stopping.\n", v->StateSaveArea.Rip);
                set_color(SystemTable, COLOR_DEFAULT);
                break;
            }
        }
        else if (exit_code == 0x4D)
        {
            uint64_t pe_start  = g_guest_code_base;
            uint64_t pe_end    = g_guest_code_base + g_guest_program_size;
            uint64_t rip       = v->StateSaveArea.Rip;
            uint8_t *rip_bytes = (uint8_t*)(uintptr_t)rip;

            set_color(SystemTable, COLOR_RED);
            Print(L"#GP RIP=0x%lx bytes: %02x %02x %02x %02x\n",
                rip, rip_bytes[0], rip_bytes[1], rip_bytes[2], rip_bytes[3]);
            Print(L"    RAX=0x%lx RCX=0x%lx RDX=0x%lx RSP=0x%lx\n",
                v->StateSaveArea.Rax,
                v->StateSaveArea.Rcx,
                v->StateSaveArea.Rdx,
                v->StateSaveArea.Rsp);
            set_color(SystemTable, COLOR_DEFAULT);

            if (rip >= pe_start && rip < pe_end)
            {
                // ← AJOUTE ICI
                uint64_t rcx_val = v->StateSaveArea.Rcx;
                uint8_t *rcx_ptr = (uint8_t*)(uintptr_t)rcx_val;
                Print(L"  [RCX+0x00]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    rcx_ptr[0], rcx_ptr[1], rcx_ptr[2], rcx_ptr[3],
                    rcx_ptr[4], rcx_ptr[5], rcx_ptr[6], rcx_ptr[7]);
                Print(L"  [RCX+0x50]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    rcx_ptr[0x50], rcx_ptr[0x51], rcx_ptr[0x52], rcx_ptr[0x53],
                    rcx_ptr[0x54], rcx_ptr[0x55], rcx_ptr[0x56], rcx_ptr[0x57]);

                Print(L"#GP inside PE — stopping.\n");
                uint64_t rcx_val2 = v->StateSaveArea.Rcx;
                uint64_t *rcx_ptr2 = (uint64_t*)(uintptr_t)rcx_val2;
                Print(L"  [RCX+0x758]: 0x%lx\n", rcx_ptr2[0x758 / 8]);
                Print(L"  [RCX+0x760]: 0x%lx\n", rcx_ptr2[0x760 / 8]);
                break;
            }

            uint64_t *rsp = (uint64_t*)(uintptr_t)v->StateSaveArea.Rsp;
            uint64_t ret  = *rsp;
            set_color(SystemTable, COLOR_YELLOW);
            Print(L"  → boot service call RCX=0x%lx RDX=0x%lx ret=0x%lx\n",
                v->StateSaveArea.Rcx,
                v->StateSaveArea.Rdx,
                ret);
            set_color(SystemTable, COLOR_DEFAULT);
            v->StateSaveArea.Rsp += 8;
            v->StateSaveArea.Rip  = ret;
            v->StateSaveArea.Rax = 0; // EFI_SUCCESS
        }
        else if (exit_code == 0x4E)
        {
            set_color(SystemTable, COLOR_YELLOW);
            Print(L"#PF at RIP=0x%lx fault_addr=0x%lx\n",
                v->StateSaveArea.Rip,
                v->ControlArea.ExitInfo2);
            set_color(SystemTable, COLOR_DEFAULT);
            break;
        }
        else
        {
            set_color(SystemTable, COLOR_RED);
            Print(L"Unexpected exit code: 0x%lx\n",
                v->ControlArea.ExitCode);
            set_color(SystemTable, COLOR_DEFAULT);
            break;
        }
    }

    svm_stgi();
    __asm__ __volatile__("sti");

    Print(L"\nPress any key to shutdown...\n");

    UINTN index;
    EFI_INPUT_KEY key;

    SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);

    SystemTable->BootServices->WaitForEvent(
        1,
        &SystemTable->ConIn->WaitForKey,
        &index
    );

    SystemTable->ConIn->ReadKeyStroke(
        SystemTable->ConIn,
        &key
    );

    SystemTable->RuntimeServices->ResetSystem(
        EfiResetShutdown,
        EFI_SUCCESS,
        0,
        NULL
    );

    return EFI_SUCCESS;
}