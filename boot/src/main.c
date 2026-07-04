#include <efi.h>
#include <efilib.h>

#define ASM_NOP 0x90
#define ASM_HLT 0xF4

#define ASM_VMRUN_0 0x0F
#define ASM_VMRUN_1 0x01
#define ASM_VMRUN_2 0xD8

#define ASM_VMMCALL_0 0x0F
#define ASM_VMMCALL_1 0x01
#define ASM_VMMCALL_2 0xD9

#define ASM_JMP_REL8 0xEB

#define SIZE_NOP 1
#define SIZE_HLT 1
#define SIZE_VMRUN 3
#define SIZE_VMMCALL 3
#define SIZE_JMP_REL8 2

#define EXIT_VMMCALL 0x81
#define EXIT_HLT     0x78

uint64_t g_guest_program_size = 0;

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

    uint8_t  Reserved4[0x400 - 0x98];
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

uint64_t init_rip(void *rip_ptr, EFI_SYSTEM_TABLE *SystemTable)
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

    return offset; 
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
    vmcb->StateSaveArea.Rflags = 0x2;
    vmcb->StateSaveArea.Cr0 = 0x60000010;
    vmcb->StateSaveArea.Efer = (1ULL << 12);

    vmcb->StateSaveArea.Cs.Selector = 0x0000;
    vmcb->StateSaveArea.Cs.Attrib   = 0x009B;
    vmcb->StateSaveArea.Cs.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Cs.Base     = 0x00000000;

    vmcb->StateSaveArea.Ss.Selector = 0x0000;
    vmcb->StateSaveArea.Ss.Attrib   = 0x0093;
    vmcb->StateSaveArea.Ss.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Ss.Base     = 0x00000000;

    vmcb->StateSaveArea.Ds.Selector = 0x0000;
    vmcb->StateSaveArea.Ds.Attrib   = 0x0093;
    vmcb->StateSaveArea.Ds.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Ds.Base     = 0x00000000;

    vmcb->StateSaveArea.Es.Selector = 0x0000;
    vmcb->StateSaveArea.Es.Attrib   = 0x0093;
    vmcb->StateSaveArea.Es.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Es.Base     = 0x00000000;

    vmcb->StateSaveArea.Fs.Selector = 0x0000;
    vmcb->StateSaveArea.Fs.Attrib   = 0x0093;
    vmcb->StateSaveArea.Fs.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Fs.Base     = 0x00000000;

    vmcb->StateSaveArea.Gs.Selector = 0x0000;
    vmcb->StateSaveArea.Gs.Attrib   = 0x0093;
    vmcb->StateSaveArea.Gs.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Gs.Base     = 0x00000000;

    vmcb->StateSaveArea.Gdtr.Selector = 0x0000;
    vmcb->StateSaveArea.Gdtr.Attrib   = 0x0000;
    vmcb->StateSaveArea.Gdtr.Limit    = 0x0000;
    vmcb->StateSaveArea.Gdtr.Base     = 0x00000000;

    vmcb->StateSaveArea.Idtr.Selector = 0x0000;
    vmcb->StateSaveArea.Idtr.Attrib   = 0x0000;
    vmcb->StateSaveArea.Idtr.Limit    = 0x0000;
    vmcb->StateSaveArea.Idtr.Base     = 0x00000000;

    vmcb->StateSaveArea.Tr.Selector = 0x0000;
    vmcb->StateSaveArea.Tr.Attrib   = 0x008B;
    vmcb->StateSaveArea.Tr.Limit    = 0xFFFF;
    vmcb->StateSaveArea.Tr.Base     = 0x00000000;

    vmcb->StateSaveArea.Cpl = 0;

    vmcb->StateSaveArea.Cr4 = 0;
    vmcb->StateSaveArea.Cr3 = 0;
    
    vmcb->StateSaveArea.Dr6 = 0xFFFF0FF0;
    vmcb->StateSaveArea.Dr7 = 0x00000400;

    // alouer memoir pour rip
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

    g_guest_program_size = init_rip(guest_code, SystemTable);

    vmcb->StateSaveArea.Cs.Base = (uint64_t)(uintptr_t)guest_code;
    vmcb->StateSaveArea.Rip = 0;

    vmcb->StateSaveArea.Ss.Base = (uint64_t)(uintptr_t)guest_code;
    vmcb->StateSaveArea.Rsp = 0x1000 - 0x10;

    Print(L"Guest RIP set to: %p\n", guest_code);
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

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    void *vmcb = NULL;
    void *host_save_area = NULL;

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

    uint64_t i = 0;

    while (v->StateSaveArea.Rip < g_guest_program_size)
    {
        Print(L"\nBefore VMRUN, RIP: 0x%lx / size: 0x%lx\n",
            v->StateSaveArea.Rip,
            g_guest_program_size);

        svm_vmrun(vmcb);

        Print(L"ExitCode: 0x%lx\n", v->ControlArea.ExitCode);
        Print(L"Guest RIP after VMEXIT: 0x%lx\n", v->StateSaveArea.Rip);

        if (v->ControlArea.ExitCode == EXIT_VMMCALL)
        {
            Print(L"VMMCALL intercepted.\n");

            // VMMCALL = 0F 01 D9 = 3 octets
            v->StateSaveArea.Rip += SIZE_VMMCALL;

            Print(L"Guest RIP after skip VMMCALL: 0x%lx\n",
                v->StateSaveArea.Rip);
        }
        else if (v->ControlArea.ExitCode == EXIT_HLT)
        {
            Print(L"HLT intercepted.\n");

            // HLT = F4 = 1 octet
            v->StateSaveArea.Rip += SIZE_HLT;

            Print(L"Guest RIP after skip HLT: 0x%lx\n",
                v->StateSaveArea.Rip);
        }
        else
        {
            Print(L"Unexpected exit code: 0x%lx\n",
                v->ControlArea.ExitCode);
            break;
        }
    }

    while (1) 
    {
        __asm__("hlt"); // pause the CPU until the next interrupt.
    }
    return EFI_SUCCESS;
}