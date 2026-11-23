/* Declare the SysV entry type before GNU-EFI masks __attribute__ on MSVC. */
#include "framebuffer_config.h"
#include <efi.h>
#include "elf.h"

typedef struct {
    UINT64 start, end; /* Page-aligned, exclusive end. */
} LOAD_RANGE;

static void print_hex(EFI_SYSTEM_TABLE *st, UINT64 value)
{
    CHAR16 text[19];
    text[0] = L'0';
    text[1] = L'x';
    for (UINTN i = 0; i < 16; ++i)
        text[2 + i] = L"0123456789ABCDEF"[(value >> (60 - 4 * i)) & 15];
    text[18] = 0;
    st->ConOut->OutputString(st->ConOut, text);
}

static void read_phdr(EFI_BOOT_SERVICES *bs, const UINT8 *buffer,
                      const ELF64_HEADER *eh, UINTN index,
                      ELF64_PROGRAM_HEADER *ph)
{
    /* Copy avoids unaligned structure accesses in an untrusted file. */
    bs->CopyMem(ph, (VOID *)(buffer + eh->phoff + index * eh->phentsize),
                sizeof(*ph));
}

static EFI_STATUS load_segments(EFI_SYSTEM_TABLE *st, const UINT8 *buffer,
                                UINTN file_size, UINT64 *entry)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    ELF64_HEADER eh;
    ELF64_PROGRAM_HEADER ph, previous;
    LOAD_RANGE *ranges = NULL;
    UINTN count = 0, allocated = 0, segment = ~(UINTN)0;
    BOOLEAN entry_valid = FALSE;
    EFI_STATUS status = EFI_LOAD_ERROR;
    CHAR16 *stage = L"Validate ELF header size";
    const UINT64 max_address = 0x00007FFFFFFFFFFFULL;

    if (file_size < sizeof(eh)) goto cleanup;
    bs->CopyMem(&eh, (VOID *)buffer, sizeof(eh));
    stage = L"Validate ELF identity/type/machine/version";
    if (eh.ident[0] != 0x7f || eh.ident[1] != 'E' ||
        eh.ident[2] != 'L' || eh.ident[3] != 'F' ||
        eh.ident[4] != 2 || eh.ident[5] != 1 || eh.ident[6] != 1 ||
        eh.type != 2 || eh.machine != 62 || eh.version != 1 ||
        eh.ehsize != sizeof(eh)) goto cleanup;

    stage = L"Validate program header table";
    if (eh.phentsize != sizeof(ph) || eh.phnum == 0 || eh.phnum == 0xffff ||
        eh.phoff < sizeof(eh) || eh.phoff > file_size ||
        eh.phnum > (file_size - eh.phoff) / sizeof(ph)) goto cleanup;

    stage = L"AllocatePool(load ranges)";
    status = bs->AllocatePool(EfiLoaderData, eh.phnum * sizeof(*ranges),
                              (VOID **)&ranges);
    if (EFI_ERROR(status)) goto cleanup;
    status = EFI_LOAD_ERROR;

    /* Validate every segment before reserving or writing destination memory. */
    for (UINTN i = 0; i < eh.phnum; ++i) {
        segment = i;
        read_phdr(bs, buffer, &eh, i, &ph);
        stage = L"Unsupported dynamic/interpreter segment";
        if (ph.type == ELF_PT_DYNAMIC || ph.type == ELF_PT_INTERP) goto cleanup;
        if (ph.type != ELF_PT_LOAD) continue;
        stage = L"Validate PT_LOAD file range/size";
        if (ph.filesz > ph.memsz || ph.offset > file_size ||
            ph.filesz > file_size - ph.offset) goto cleanup;
        stage = L"Validate PT_LOAD fixed address/range";
        /* Support lower-half identity addresses only; no relocation/mapping. */
        if (ph.paddr != ph.vaddr || ph.paddr > max_address ||
            ph.memsz > max_address - ph.paddr) goto cleanup;
        stage = L"Validate PT_LOAD alignment";
        if (ph.align > 1 && ((ph.align & (ph.align - 1)) != 0 ||
            (ph.vaddr & (ph.align - 1)) != (ph.offset & (ph.align - 1))))
            goto cleanup;
        if (ph.memsz == 0) continue;
        stage = L"Validate PT_LOAD nonzero page/address rounding";
        if (ph.paddr < EFI_PAGE_SIZE ||
            ph.paddr + ph.memsz > max_address - (EFI_PAGE_SIZE - 1))
            goto cleanup;
        stage = L"Validate PT_LOAD byte overlap";
        for (UINTN j = 0; j < i; ++j) {
            read_phdr(bs, buffer, &eh, j, &previous);
            if (previous.type == ELF_PT_LOAD && previous.memsz != 0 &&
                ph.paddr < previous.paddr + previous.memsz &&
                previous.paddr < ph.paddr + ph.memsz) goto cleanup;
        }
        if ((ph.flags & ELF_PF_X) && eh.entry >= ph.vaddr &&
            eh.entry - ph.vaddr < ph.memsz) entry_valid = TRUE;

        LOAD_RANGE range = {
            ph.paddr & ~((UINT64)EFI_PAGE_SIZE - 1),
            (ph.paddr + ph.memsz + EFI_PAGE_SIZE - 1) &
                ~((UINT64)EFI_PAGE_SIZE - 1)
        };
        UINTN pos = count;
        while (pos > 0 && ranges[pos - 1].start > range.start) {
            ranges[pos] = ranges[pos - 1];
            --pos;
        }
        ranges[pos] = range;
        ++count;
    }
    segment = ~(UINTN)0;
    stage = L"Validate entry in executable PT_LOAD";
    if (!entry_valid) goto cleanup;

    /* Merge page overlaps, including distinct segments sharing one page. */
    UINTN merged = 0;
    for (UINTN i = 0; i < count; ++i) {
        if (merged && ranges[i].start <= ranges[merged - 1].end) {
            if (ranges[i].end > ranges[merged - 1].end)
                ranges[merged - 1].end = ranges[i].end;
        } else {
            ranges[merged++] = ranges[i];
        }
    }
    count = merged;
    for (UINTN i = 0; i < count; ++i) {
        EFI_PHYSICAL_ADDRESS address = ranges[i].start;
        UINTN pages = (UINTN)((ranges[i].end - ranges[i].start) / EFI_PAGE_SIZE);
        st->ConOut->OutputString(st->ConOut, L"AllocatePages requested=");
        print_hex(st, address);
        st->ConOut->OutputString(st->ConOut, L" pages=");
        print_hex(st, pages);
        st->ConOut->OutputString(st->ConOut, L"\r\n");
        stage = L"AllocatePages(AllocateAddress, EfiLoaderCode)";
        status = bs->AllocatePages(AllocateAddress, EfiLoaderCode, pages, &address);
        if (EFI_ERROR(status)) goto cleanup;
        ++allocated;
        stage = L"Verify allocated address";
        if (address != ranges[i].start) {
            bs->FreePages(address, pages);
            --allocated;
            status = EFI_LOAD_ERROR;
            goto cleanup;
        }
        st->ConOut->OutputString(st->ConOut, L"Allocated address=");
        print_hex(st, address);
        st->ConOut->OutputString(st->ConOut, L"\r\n");
    }

    for (UINTN i = 0; i < eh.phnum; ++i) {
        read_phdr(bs, buffer, &eh, i, &ph);
        if (ph.type != ELF_PT_LOAD || ph.memsz == 0) continue;
        if (ph.filesz)
            bs->CopyMem((VOID *)(UINTN)ph.paddr,
                        (VOID *)(buffer + ph.offset), (UINTN)ph.filesz);
        if (ph.memsz > ph.filesz)
            bs->SetMem((VOID *)(UINTN)(ph.paddr + ph.filesz),
                       (UINTN)(ph.memsz - ph.filesz), 0);
        st->ConOut->OutputString(st->ConOut, L"PT_LOAD index=");
        print_hex(st, i);
        st->ConOut->OutputString(st->ConOut, L" destination=");
        print_hex(st, ph.paddr);
        st->ConOut->OutputString(st->ConOut, L" filesz=");
        print_hex(st, ph.filesz);
        st->ConOut->OutputString(st->ConOut, L" memsz=");
        print_hex(st, ph.memsz);
        st->ConOut->OutputString(st->ConOut, L"\r\n");
    }
    st->ConOut->OutputString(st->ConOut,
                            L"Kernel segments loaded successfully\r\n");
    *entry = eh.entry;
    status = EFI_SUCCESS;

cleanup:
    if (EFI_ERROR(status)) {
        st->ConOut->OutputString(st->ConOut, stage);
        if (segment != ~(UINTN)0) {
            st->ConOut->OutputString(st->ConOut, L" segment=");
            print_hex(st, segment);
        }
        st->ConOut->OutputString(st->ConOut, L" failed status=");
        print_hex(st, status);
        st->ConOut->OutputString(st->ConOut, L"\r\n");
        while (allocated) {
            --allocated;
            EFI_STATUS release = bs->FreePages(ranges[allocated].start,
                (UINTN)((ranges[allocated].end - ranges[allocated].start) / EFI_PAGE_SIZE));
            if (EFI_ERROR(release)) {
                st->ConOut->OutputString(st->ConOut, L"FreePages rollback failed status=");
                print_hex(st, release);
                st->ConOut->OutputString(st->ConOut, L"\r\n");
            }
        }
    }
    if (ranges) bs->FreePool(ranges);
    return status;
}

static EFI_STATUS load_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                              UINT64 *entry)
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

    stage = L"Load kernel segments (see diagnostic above)";
    status = load_segments(st, buffer, file_size, entry);

cleanup:
    if (EFI_ERROR(status)) {
        st->ConOut->OutputString(st->ConOut, stage);
        st->ConOut->OutputString(st->ConOut, L" failed\r\n");
    }
    /* Loaded pages survive success; the original file buffer is temporary. */
    if (buffer != NULL) bs->FreePool(buffer);
    if (info != NULL) bs->FreePool(info);
    if (file != NULL) file->Close(file);
    if (root != NULL) root->Close(root);
    return status;
}

/* OVMF identity maps the RAM holding its page tables. Inspect the existing
 * mappings without touching framebuffer MMIO or changing CR3. */
static BOOLEAN framebuffer_is_mapped(UINT64 base, UINT64 size)
{
    UINT64 cr3, cr4;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    const UINT64 address_mask = 0x000FFFFFFFFFF000ULL;
    const UINT64 end = base + size;
    const int top_shift = (cr4 & (1ULL << 12)) ? 48 : 39;

    for (UINT64 address = base; address < end;) {
        UINT64 table_address = cr3 & address_mask;
        for (int shift = top_shift; shift >= 12; shift -= 9) {
            const volatile UINT64 *table = (const volatile UINT64 *)(UINTN)table_address;
            UINT64 entry = table[(address >> shift) & 0x1FF];
            if ((entry & 3) != 3) return FALSE; /* Present and writable. */

            if (shift == 12 || ((shift == 30 || shift == 21) && (entry & 0x80))) {
                UINT64 page_mask = (1ULL << shift) - 1;
                UINT64 page_base = address & ~page_mask;
                if ((entry & address_mask & ~page_mask) != page_base)
                    return FALSE;
                address = page_base + page_mask + 1;
                break;
            }
            table_address = entry & address_mask;
        }
    }
    return TRUE;
}

static EFI_STATUS get_framebuffer(EFI_SYSTEM_TABLE *st, FramebufferConfig *config)
{
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = st->BootServices->LocateProtocol(&guid, NULL, (VOID **)&gop);
    if (EFI_ERROR(status)) {
        st->ConOut->OutputString(st->ConOut, L"LocateProtocol(GOP) failed\r\n");
        return status;
    }

    if (!gop || !gop->Mode || !gop->Mode->Info ||
        gop->Mode->SizeOfInfo < sizeof(*gop->Mode->Info)) {
        st->ConOut->OutputString(st->ConOut, L"GOP mode information unavailable\r\n");
        return EFI_DEVICE_ERROR;
    }

    const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode = gop->Mode;
    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = mode->Info;
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
        config->pixel_format = FRAMEBUFFER_RGB;
    else if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
        config->pixel_format = FRAMEBUFFER_BGR;
    else {
        st->ConOut->OutputString(st->ConOut, L"Unsupported GOP pixel format= ");
        print_hex(st, info->PixelFormat);
        st->ConOut->OutputString(st->ConOut, L"\r\n");
        return EFI_UNSUPPORTED;
    }

    const UINT64 base = mode->FrameBufferBase;
    const UINT64 size = mode->FrameBufferSize;
    const UINT64 width = info->HorizontalResolution;
    const UINT64 height = info->VerticalResolution;
    const UINT64 stride = info->PixelsPerScanLine;
    const UINT64 lower_half_end = 1ULL << 47;
    if (!base || (base & 3) || base >= lower_half_end || !size ||
        size > lower_half_end - base || !width || !height || stride < width ||
        (height - 1) * stride + width > size / FRAMEBUFFER_BYTES_PER_PIXEL) {
        st->ConOut->OutputString(st->ConOut, L"Invalid GOP framebuffer range\r\n");
        return EFI_DEVICE_ERROR;
    }
    /* The visible span includes row padding up to the last visible pixel. */
    const UINT64 visible_size = ((height - 1) * stride + width) *
                                FRAMEBUFFER_BYTES_PER_PIXEL;
    if (!framebuffer_is_mapped(base, visible_size)) {
        st->ConOut->OutputString(st->ConOut, L"GOP framebuffer lacks writable identity mapping\r\n");
        return EFI_UNSUPPORTED;
    }

    config->frame_buffer_base = base;
    config->frame_buffer_size = size;
    config->horizontal_resolution = info->HorizontalResolution;
    config->vertical_resolution = info->VerticalResolution;
    config->pixels_per_scan_line = info->PixelsPerScanLine;
    return EFI_SUCCESS;
}

/* The loader image stays resident throughout this kernel's execution. */
static FramebufferConfig framebuffer_config;

static _Noreturn void halt_cpu(void)
{
    for (;;) __asm__ volatile("cli; hlt");
}

static EFI_STATUS boot_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                              UINT64 entry)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN capacity = 0;
    BOOLEAN exit_attempted = FALSE;
    EFI_STATUS status;

    for (;;) {
        UINTN size = capacity;
        UINTN key, descriptor_size;
        UINT32 descriptor_version;
        status = bs->GetMemoryMap(&size, map, &key, &descriptor_size,
                                  &descriptor_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            /* Allocating the buffer can itself add map descriptors. */
            if (size > ~(UINTN)0 - EFI_PAGE_SIZE) {
                status = EFI_OUT_OF_RESOURCES;
                break;
            }
            if (map != NULL) {
                status = bs->FreePool(map);
                if (EFI_ERROR(status)) break;
                map = NULL;
            }
            capacity = size + EFI_PAGE_SIZE;
            status = bs->AllocatePool(EfiLoaderData, capacity, (VOID **)&map);
            if (EFI_ERROR(status)) break;
            continue;
        }
        if (EFI_ERROR(status)) break;

        exit_attempted = TRUE;
        status = bs->ExitBootServices(image, key);
        if (status == EFI_SUCCESS) {
            /* Keep the current stack and enter the ELF function with SysV ABI. */
            __asm__ volatile("cli; cld" ::: "memory", "cc");
            ((KernelEntry)(UINTN)entry)(&framebuffer_config);
            halt_cpu();
        }
        if (status != EFI_INVALID_PARAMETER) break;
        /* Only Memory Allocation Services are safe after the first attempt. */
    }

    if (exit_attempted) halt_cpu();
    if (map != NULL) bs->FreePool(map);
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

    EFI_STATUS status = get_framebuffer(SystemTable, &framebuffer_config);
    UINT64 entry;
    if (!EFI_ERROR(status)) status = load_kernel(ImageHandle, SystemTable, &entry);
    if (EFI_ERROR(status)) {
        con->OutputString(con, L"Boot preparation failed status=");
        print_hex(SystemTable, status);
        con->OutputString(con, L"\r\n");
        SystemTable->BootServices->Stall(5000000);
        return status;
    }

    con->OutputString(con, L"Exiting boot services, kernel entry=");
    print_hex(SystemTable, entry);
    con->OutputString(con, L"\r\n");
    status = boot_kernel(ImageHandle, SystemTable, entry);
    con->OutputString(con, L"Memory map preparation failed status=");
    print_hex(SystemTable, status);
    con->OutputString(con, L"\r\n");
    return status;
}
