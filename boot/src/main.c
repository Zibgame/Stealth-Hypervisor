#include <efi.h>
#include <efilib.h>

int check_svm_support()
{
    uint32_t eax, ebx, ecx, edx;

    eax = 0x80000001;

    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(eax)
    );

    if (!(ecx & (1 << 2)))
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

uint64_t read_msr(uint32_t msr) {
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

void write_msr(uint32_t msr, uint64_t value) {
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

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
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

    while (1) 
    {
        __asm__("hlt"); // pause the CPU until the next interrupt.
    }
    return EFI_SUCCESS;
}