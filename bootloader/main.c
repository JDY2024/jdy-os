#include <efi.h>

static EFI_STATUS load_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID filesystem_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINT8 *buffer = NULL;
    UINTN info_size = 0;
    UINTN file_size;
    UINTN read_size;
    CHAR16 *stage = L"HandleProtocol(LoadedImage)";
    EFI_STATUS status;

    status = bs->HandleProtocol(image, &loaded_image_guid, (VOID **)&loaded_image);
    if (EFI_ERROR(status)) goto cleanup;

    stage = L"HandleProtocol(SimpleFileSystem)";
    status = bs->HandleProtocol(loaded_image->DeviceHandle, &filesystem_guid,
                                (VOID **)&fs);
    if (EFI_ERROR(status)) goto cleanup;

    stage = L"OpenVolume";
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) goto cleanup;

    stage = L"Open kernel.elf";
    status = root->Open(root, &file, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) goto cleanup;

    /* FileName is variable length: ask GetInfo for the required buffer size. */
    stage = L"GetInfo(size)";
    status = file->GetInfo(file, &file_info_guid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) {
        if (!EFI_ERROR(status)) status = EFI_DEVICE_ERROR;
        goto cleanup;
    }

    stage = L"AllocatePool(file info)";
    status = bs->AllocatePool(EfiLoaderData, info_size, (VOID **)&info);
    if (EFI_ERROR(status)) goto cleanup;

    stage = L"GetInfo(file info)";
    status = file->GetInfo(file, &file_info_guid, &info_size, info);
    if (EFI_ERROR(status)) goto cleanup;

    stage = L"Validate file size/type";
    if ((info->Attribute & EFI_FILE_DIRECTORY) || info->FileSize < 4 ||
        info->FileSize > (UINT64)(~(UINTN)0)) {
        status = EFI_LOAD_ERROR;
        goto cleanup;
    }
    file_size = (UINTN)info->FileSize;

    stage = L"AllocatePool(kernel buffer)";
    status = bs->AllocatePool(EfiLoaderData, file_size, (VOID **)&buffer);
    if (EFI_ERROR(status)) goto cleanup;

    /* Read updates read_size with the actual number of bytes transferred. */
    stage = L"Read kernel.elf";
    read_size = file_size;
    status = file->Read(file, &read_size, buffer);
    if (EFI_ERROR(status)) goto cleanup;
    if (read_size != file_size) {
        stage = L"Read kernel.elf (incomplete)";
        status = EFI_LOAD_ERROR;
        goto cleanup;
    }

    stage = L"Validate ELF magic";
    if (buffer[0] != 0x7f || buffer[1] != 0x45 ||
        buffer[2] != 0x4c || buffer[3] != 0x46) {
        status = EFI_LOAD_ERROR;
        goto cleanup;
    }

    st->ConOut->OutputString(st->ConOut, L"Kernel ELF loaded successfully\r\n");
    status = EFI_SUCCESS;

cleanup:
    if (EFI_ERROR(status)) {
        st->ConOut->OutputString(st->ConOut, stage);
        st->ConOut->OutputString(st->ConOut, L" failed\r\n");
    }
    /* Step 2-2 only verifies the file; no kernel execution uses this buffer. */
    if (buffer != NULL) bs->FreePool(buffer);
    if (info != NULL) bs->FreePool(info);
    if (file != NULL) file->Close(file);
    if (root != NULL) root->Close(root);
    return status;
}

EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {
    EFI_SIMPLE_TEXT_OUT_PROTOCOL *con = SystemTable->ConOut;

    con->OutputString(
        con,
        L"Hello World from My OS!\r\n"
    );

    EFI_STATUS status = load_kernel(ImageHandle, SystemTable);

    SystemTable->BootServices->Stall(5000000);

    return status;
}
