#include <efi.h>
#include <efilib.h>

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    Print(L"Hello, World!\n");
    while (1) 
    {
        __asm__("hlt"); // pause the CPU until the next interrupt.
    }
    return EFI_SUCCESS;
}