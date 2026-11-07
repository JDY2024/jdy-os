#include <efi.h>

EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {
    EFI_SIMPLE_TEXT_OUT_PROTOCOL *con = SystemTable->ConOut;

    con->OutputString(
        con,
        L"Hello World from My OS!\r\n"
    );

    SystemTable->BootServices->Stall(5000000);

    return EFI_SUCCESS;
}
