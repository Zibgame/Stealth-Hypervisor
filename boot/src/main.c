#include <efi.h>
#include <efilib.h>

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

uint64_t g_guest_program_size = 0;
uint64_t g_guest_code_base = 0;

void set_color(EFI_SYSTEM_TABLE *SystemTable, UINTN color)
{
    SystemTable->ConOut->SetAttribute(SystemTable->ConOut, color);
}

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
} __attribute__((packed)) VMCB_STATE_SAVE_AREA;

typedef struct _VMCB
{
    VMCB_CONTROL_AREA ControlArea;
    VMCB_STATE_SAVE_AREA StateSaveArea;
} __attribute__((packed)) VMCB;

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

void* init_NPT(EFI_SYSTEM_TABLE *SystemTable)
{
    if (!check_1gb_pages_support())
    {
        Print(L"Cannot build NPT without 1GB page support.\n");
        return NULL;
    }

    void *pml4 = NULL;
    void *pdpt = NULL;
    EFI_STATUS status;

    // PML4: top level table, 1 page
    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        1,
        (EFI_PHYSICAL_ADDRESS *)&pml4
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate NPT PML4. Status: %d\n", status);
        return NULL;
    }

    // PDPT: second level table, 1 page
    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        1,
        (EFI_PHYSICAL_ADDRESS *)&pdpt
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate NPT PDPT. Status: %d\n", status);
        return NULL;
    }

    // clear both tables
    uint64_t *pml4_entries = (uint64_t*)pml4;
    uint64_t *pdpt_entries = (uint64_t*)pdpt;
    for (int i = 0; i < 512; i++)
    {
        pml4_entries[i] = 0;
        pdpt_entries[i] = 0;
    }

    // PML4[0] points to our PDPT. Flags: Present(0) + Writable(1) + User(2)
    pml4_entries[0] = ((uint64_t)(uintptr_t)pdpt) | 0x7;

    // PDPT[0..3]: four 1GB entries, identity-mapped, covering 0GB to 4GB
    // Flags: Present(0) + Writable(1) + User(2) + PageSize/1GB(7)
    for (int i = 0; i < 4; i++)
    {
        uint64_t phys_1gb_base = (uint64_t)i * 0x40000000ULL; // i * 1GB
        pdpt_entries[i] = phys_1gb_base | 0x87;
    }

    Print(L"NPT built. PML4 at: %p, PDPT at: %p\n", pml4, pdpt);

    return pml4;
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

void* init_HostSaveArea(EFI_SYSTEM_TABLE *SystemTable)
{
    void *host_save_area;
    EFI_STATUS status = 0;
    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages, // type of allocation
        EfiRuntimeServicesData, // memory type for runtime services
        1, // number of pages to allocate
        (EFI_PHYSICAL_ADDRESS *)&host_save_area // address of the allocated page
    ); // Allocate 1 page of 4KB for HOST SAVE AREA
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate HOST SAVE AREA. Status: %d\n", status);
        return NULL;
    }
    Print(L"HOST SAVE AREA allocated at address: %p\n", host_save_area);
    return host_save_area;
}

void* init_VMCB(EFI_SYSTEM_TABLE *SystemTable)
{
    void *vmcb;
    EFI_STATUS status = 0;
    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages, // type of allocation
        EfiRuntimeServicesData, // memory type for runtime services
        1, // number of pages to allocate
        (EFI_PHYSICAL_ADDRESS *)&vmcb // address of the allocated page
    ); // Allocate 1 page of 4KB for VMCB
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate VMCB. Status: %d\n", status);
        return NULL;
    }
    Print(L"VMCB allocated at address: %p\n", vmcb);
    return vmcb;
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

    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages, EfiRuntimeServicesData, 1,
        (EFI_PHYSICAL_ADDRESS *)&pml4
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate guest PML4. Status: %d\n", status);
        return NULL;
    }

    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages, EfiRuntimeServicesData, 1,
        (EFI_PHYSICAL_ADDRESS *)&pdpt
    );
    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate guest PDPT. Status: %d\n", status);
        return NULL;
    }

    uint64_t *pml4_entries = (uint64_t*)pml4;
    uint64_t *pdpt_entries = (uint64_t*)pdpt;
    for (int i = 0; i < 512; i++)
    {
        pml4_entries[i] = 0;
        pdpt_entries[i] = 0;
    }

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

void fill_VMCB(void *vmcb_ptr, EFI_SYSTEM_TABLE *SystemTable)
{
    VMCB *vmcb = (VMCB*)vmcb_ptr;

    // Clear the VMCB to ensure a clean state
    uint8_t *raw = (uint8_t*)vmcb;
    for (int i = 0; i < 4096; i++)
    {
        raw[i] = 0;
    }

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

    // CS: code segment, executable + readable + present
    vmcb->StateSaveArea.Cs.Selector = 0x0000;
        // Attrib 0x0A9B = same as before (present, code, exec/read) but with L bit set (long mode)
    // AMD manual: only D, L, R bits of CS are observed by hardware — this is the one that matters here
    vmcb->StateSaveArea.Cs.Attrib   = 0x0A9B;
    vmcb->StateSaveArea.Cs.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Cs.Base     = 0x00000000;

    // SS: stack segment, data + writable + present
    vmcb->StateSaveArea.Ss.Selector = 0x0000;
    vmcb->StateSaveArea.Ss.Attrib   = 0x0093;
    vmcb->StateSaveArea.Ss.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Ss.Base     = 0x00000000;

    // DS: data segment, same attributes as SS
    vmcb->StateSaveArea.Ds.Selector = 0x0000;
    vmcb->StateSaveArea.Ds.Attrib   = 0x0093;
    vmcb->StateSaveArea.Ds.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Ds.Base     = 0x00000000;

    // ES: extra data segment, same attributes as SS/DS
    vmcb->StateSaveArea.Es.Selector = 0x0000;
    vmcb->StateSaveArea.Es.Attrib   = 0x0093;
    vmcb->StateSaveArea.Es.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Es.Base     = 0x00000000;

    // FS: extra segment, same attributes as SS/DS
    vmcb->StateSaveArea.Fs.Selector = 0x0000;
    vmcb->StateSaveArea.Fs.Attrib   = 0x0093;
    vmcb->StateSaveArea.Fs.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Fs.Base     = 0x00000000;

    // GS: extra segment, same attributes as SS/DS
    vmcb->StateSaveArea.Gs.Selector = 0x0000;
    vmcb->StateSaveArea.Gs.Attrib   = 0x0093;
    vmcb->StateSaveArea.Gs.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Gs.Base     = 0x00000000;

    // GDTR: no GDT set up yet for this minimal guest, left empty
    vmcb->StateSaveArea.Gdtr.Selector = 0x0000;
    vmcb->StateSaveArea.Gdtr.Attrib   = 0x0000;
    vmcb->StateSaveArea.Gdtr.Limit    = 0x0000;
    vmcb->StateSaveArea.Gdtr.Base     = 0x00000000;

    // IDTR: no IDT set up yet for this minimal guest, left empty
    vmcb->StateSaveArea.Idtr.Selector = 0x0000;
    vmcb->StateSaveArea.Idtr.Attrib   = 0x0000;
    vmcb->StateSaveArea.Idtr.Limit    = 0x0000;
    vmcb->StateSaveArea.Idtr.Base     = 0x00000000;

    // TR: task register, present + 32-bit TSS type, not really used by this guest
    vmcb->StateSaveArea.Tr.Selector = 0x0000;
    vmcb->StateSaveArea.Tr.Attrib   = 0x008B;
    vmcb->StateSaveArea.Tr.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Tr.Base     = 0x00000000;

    // CPL: guest starts at ring 0 (most privileged)
    vmcb->StateSaveArea.Cpl = 0;

    // CR4/CR3: no paging, no extended features yet (NPT disabled, guest uses no page tables)
    // CR4.PAE (bit5) is mandatory for long mode
    vmcb->StateSaveArea.Cr4 = (1ULL << 5);
    vmcb->StateSaveArea.Cr3 = 0;

    // DR6/DR7: default reset values defined by the x86 spec for debug registers
    vmcb->StateSaveArea.Dr6 = 0xFFFF0FF0;
    vmcb->StateSaveArea.Dr7 = 0x00000400;

    // allocate memory for rip
    void *guest_code = NULL;

    EFI_STATUS status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        1,
        (EFI_PHYSICAL_ADDRESS *)&guest_code
    );

    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate guest code page. Status: %d\n", status);
        return;
    }

    Print(L"Guest code allocated at address: %p\n", guest_code);

    g_guest_program_size = init_rip(guest_code);
    g_guest_code_base = (uint64_t)(uintptr_t)guest_code;

    // In 64-bit mode (CS.L=1), segment bases are ignored by the CPU (except FS/GS).
    // RIP and RSP must be real linear addresses directly, not offsets from a base.
    vmcb->StateSaveArea.Cs.Base = 0;
    vmcb->StateSaveArea.Rip = (uint64_t)(uintptr_t)guest_code;

    vmcb->StateSaveArea.Ss.Base = 0;
    vmcb->StateSaveArea.Rsp = (uint64_t)(uintptr_t)guest_code + 0x1000 - 0x10;

    void *guest_pt = init_GuestPageTables(SystemTable);
    if (guest_pt == NULL)
    {
        Print(L"Failed to build guest page tables, aborting VMCB fill.\n");
        return;
    }
    vmcb->StateSaveArea.Cr3 = (uint64_t)(uintptr_t)guest_pt;
    Print(L"Guest RIP set to: %p\n", guest_code);

    // IOPM: I/O Permission Map, 3 pages, required by VMRUN even with no I/O intercepted
    void *iopm = NULL;

    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        3,
        (EFI_PHYSICAL_ADDRESS *)&iopm
    );

    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate IOPM. Status: %d\n", status);
        return;
    }

    // zeroed IOPM = no I/O port is intercepted
    uint8_t *iopm_raw = (uint8_t*)iopm;
    for (int i = 0; i < 3 * 4096; i++)
    {
        iopm_raw[i] = 0;
    }

    vmcb->ControlArea.IopmBasePa = (uint64_t)(uintptr_t)iopm;
    Print(L"IOPM allocated at address: %p\n", iopm);

    // MSRPM: MSR Permission Map, 2 pages, required by VMRUN even with no MSR intercepted
    void *msrpm = NULL;

    status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        2,
        (EFI_PHYSICAL_ADDRESS *)&msrpm
    );

    if (status != EFI_SUCCESS)
    {
        Print(L"Failed to allocate MSRPM. Status: %d\n", status);
        return;
    }

    // zeroed MSRPM = no MSR access is intercepted
    uint8_t *msrpm_raw = (uint8_t*)msrpm;
    for (int i = 0; i < 2 * 4096; i++)
    {
        msrpm_raw[i] = 0;
    }

    vmcb->ControlArea.MsrpmBasePa = (uint64_t)(uintptr_t)msrpm;
    Print(L"MSRPM allocated at address: %p\n", msrpm);

    // Nested Paging setup: identity-map the first 4GB so the guest sees
    // the same physical memory as the host, via NPT translation
    void *npt_pml4 = init_NPT(SystemTable);
    if (npt_pml4 == NULL)
    {
        Print(L"Failed to build NPT, aborting VMCB fill.\n");
        return;
    }

    vmcb->ControlArea.NpEnable = 1;
    vmcb->ControlArea.NCr3 = (uint64_t)(uintptr_t)npt_pml4;
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

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    void *vmcb = NULL;
    void *host_save_area = NULL;

    set_color(SystemTable, COLOR_DEFAULT);

    InitializeLib(ImageHandle, SystemTable);
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

    fill_VMCB(vmcb , SystemTable);
    Print(L"VMCB filled.\n");

    // indicate the place of host save area and VMCB to the user
    write_msr(0xC0010117, (uint64_t)(uintptr_t)host_save_area); // MSR_VM_HSAVE_PA
    Print(L"VM_HSAVE_PA configured.\n");

    VMCB *v = (VMCB*)vmcb;

    Print(L"Before VMRUN.\n");

    while (v->StateSaveArea.Rip < g_guest_code_base + g_guest_program_size)
    {
        Print(L"\nBefore VMRUN, RIP: 0x%lx / size: 0x%lx\n",
            v->StateSaveArea.Rip,
            g_guest_program_size);

        svm_vmrun(vmcb);

        Print(L"ExitCode: 0x%lx\n", v->ControlArea.ExitCode);
        Print(L"Guest RIP after VMEXIT: 0x%lx\n", v->StateSaveArea.Rip);

        if (v->ControlArea.ExitCode == EXIT_VMMCALL)
        {
            set_color(SystemTable, COLOR_CYAN);
            Print(L"VMMCALL intercepted.\n");
            set_color(SystemTable, COLOR_DEFAULT);

            v->StateSaveArea.Rip += SIZE_VMMCALL;

            Print(L"Guest RIP after skip VMMCALL: 0x%lx\n",
                v->StateSaveArea.Rip);
        }
        else if (v->ControlArea.ExitCode == EXIT_HLT)
        {
            set_color(SystemTable, COLOR_GREEN);
            Print(L"HLT intercepted.\n");
            set_color(SystemTable, COLOR_DEFAULT);

            v->StateSaveArea.Rip += SIZE_HLT;

            Print(L"Guest RIP after skip HLT: 0x%lx\n",
                v->StateSaveArea.Rip);
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