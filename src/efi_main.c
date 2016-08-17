
// Copyright 2016 Shin'ichi Ichikawa. Released under the MIT license.

#include <efi.h>
#include <efilib.h>
#include "efi_status.h"

#define BOOT_PARTITION_FILE_ACCESS 1

static EFI_HANDLE IH = NULL;
static EFI_LOADED_IMAGE* loaded_image = NULL;

static CHAR16* path = L"\\EFI\\BOOT\\TEXT\\UCS-2.TXT";
static UINTN buffer_size = 0;
static CHAR16* buffer = NULL;
static EFI_FILE* write_file = NULL;

static void init(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table);

static void read_key(void);
static void reset_system(EFI_STATUS status);
static void error_print(CHAR16* msg, EFI_STATUS* status);

static void load_file(void);
static void print_device_path(EFI_HANDLE handle);
static EFI_STATUS load_file2(EFI_FILE_IO_INTERFACE* efi_simple_file_system);

static EFI_FILE* open_print_info(CHAR16* path);
static void convert_to_ascii(char* ascii, CHAR16* wide);
static void write_print_info(EFI_FILE* efi_file, CHAR16* fmt, CHAR16* param);
static void close_print_info(EFI_FILE* efi_file);

EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table)
{
    init(image_handle, system_table);

    write_print_info(write_file, L"Read the message from a file: %s\n", buffer);

    write_print_info(write_file, L"Error message example: %s\n", print_status_msg(EFI_SUCCESS));

    write_print_info(write_file, L"%s", L"When you press any key, the system will reboot.\n");

    close_print_info(write_file);

    FreePool(buffer);
    buffer = NULL;

    reset_system(EFI_SUCCESS);

    return EFI_SUCCESS;
}

static void init(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table)
{
    InitializeLib(image_handle, system_table);

    IH = image_handle;

    EFI_STATUS status = EFI_SUCCESS;

    if ((NULL == ST->ConIn) || (EFI_SUCCESS != (status = ST->ConIn->Reset(ST->ConIn, 0)))){

        error_print(L"Input device unavailable.\n", ST->ConIn ? &status : NULL);
    }

    status = BS->OpenProtocol(
        image_handle, &LoadedImageProtocol, &loaded_image,
        image_handle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (EFI_ERROR(status)){

        error_print(L"OpenProtocol() LoadedImageProtocol failed.\n", &status);
    }

    write_file = open_print_info(L"\\result.txt");

    load_file();
}

static void read_key(void)
{
    if (ST->ConIn){

        EFI_STATUS local_status = EFI_SUCCESS;

        do{
            EFI_INPUT_KEY key;

            local_status = ST->ConIn->ReadKeyStroke(ST->ConIn, &key);

        } while (EFI_SUCCESS != local_status);
    }
}

static void reset_system(EFI_STATUS status)
{
    read_key();

    RT->ResetSystem(EfiResetCold, status, 0, NULL);
}

static void error_print(CHAR16* msg, EFI_STATUS* status)
{
    Print(msg);

    if (status){

        Print(L"EFI_STATUS = %d, %s\n", *status, print_status_msg(*status));
    }

    reset_system(EFI_SUCCESS);
}

static void load_file(void)
{
#if BOOT_PARTITION_FILE_ACCESS

    print_device_path(loaded_image->DeviceHandle);

    EFI_FILE_IO_INTERFACE* efi_simple_file_system = NULL;

    EFI_STATUS status = BS->OpenProtocol(
        loaded_image->DeviceHandle,
        &FileSystemProtocol,
        &efi_simple_file_system,
        IH,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (EFI_ERROR(status)){

        error_print(L"OpenProtocol() FileSystemProtocol failed.\n", &status);
    }

    status = load_file2(efi_simple_file_system);

    if (EFI_ERROR(status)){

        error_print(L"load_file2() failed.\n", &status);
    }
#else
    UINTN number_file_system_handles = 0;

    EFI_HANDLE* file_system_handles = NULL;

    EFI_STATUS status = BS->LocateHandleBuffer(
        ByProtocol,
        &FileSystemProtocol,
        NULL,
        &number_file_system_handles,
        &file_system_handles
    );

    if (EFI_ERROR(status)){

        error_print(L"LocateHandleBuffer() FileSystemProtocol failed.\n", &status);
    }

    for (UINTN i = 0; i < number_file_system_handles; ++i){

        print_device_path(file_system_handles[i]);

        EFI_FILE_IO_INTERFACE* efi_simple_file_system = NULL;

        status = BS->OpenProtocol(
            file_system_handles[i],
            &FileSystemProtocol,
            &efi_simple_file_system,
            IH,
            NULL,
            EFI_OPEN_PROTOCOL_GET_PROTOCOL
        );

        if (EFI_ERROR(status)) {

            error_print(L"OpenProtocol() FileSystemProtocol failed.\n", &status);
        }

        status = load_file2(efi_simple_file_system);

        if (EFI_SUCCESS == status){

            FreePool(file_system_handles);
            file_system_handles = NULL;

            return;
        }
    }

    FreePool(file_system_handles);
    file_system_handles = NULL;

    error_print(L"File not found.\n", &status);
#endif
}

static void print_device_path(EFI_HANDLE handle)
{
    EFI_DEVICE_PATH* device_path = NULL;

    EFI_STATUS status = BS->OpenProtocol(
        handle,
        &DevicePathProtocol,
        &device_path,
        IH,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (EFI_ERROR(status)){

        error_print(L"OpenProtocol() DevicePathProtocol failed.\n", &status);
    }

    CHAR16* p = DevicePathToStr(device_path);

    if (NULL == p){

        error_print(L"DevicePathToStr() failed.\n", NULL);
    }

    write_print_info(write_file, L"DevicePath: %s\n", p);

    FreePool(p);
    p = NULL;
}

static EFI_STATUS load_file2(EFI_FILE_IO_INTERFACE* efi_simple_file_system)
{
    EFI_FILE* efi_file_root = NULL;
    EFI_FILE* efi_file = NULL;

    EFI_STATUS status = efi_simple_file_system->OpenVolume(
        efi_simple_file_system, &efi_file_root
    );

    if (EFI_ERROR(status)){

        return status;
    }

    status = efi_file_root->Open(
        efi_file_root, &efi_file, path,
        EFI_FILE_MODE_READ, 0
    );

    if (EFI_ERROR(status)){

        return status;
    }

    UINTN info_size = 0;
    EFI_FILE_INFO* info = NULL;

    status = efi_file->GetInfo(efi_file, &GenericFileInfo, &info_size, info);

    if (EFI_BUFFER_TOO_SMALL != status){
        
        if (EFI_ERROR(status)){

            return status;
        }
    }

    info = (EFI_FILE_INFO*)AllocatePool(info_size);

    if (NULL == info){

        error_print(L"AllocatePool() failed.\n", NULL);
    }

    status = efi_file->GetInfo(efi_file, &GenericFileInfo, &info_size, info);

    if (EFI_ERROR(status)){

        FreePool(info);
        info = NULL;

        return status;
    }

    buffer_size = info->FileSize;

    FreePool(info);
    info = NULL;

    CHAR16* p = (CHAR16*)AllocatePool(buffer_size + 1);

    if (NULL == p){

        error_print(L"AllocatePool() failed.\n", NULL);
    }

    buffer = p;

    buffer[buffer_size / sizeof(CHAR16)] = 0;

    status = efi_file->Read(efi_file, &buffer_size, buffer);

    if (EFI_ERROR(status)){

        FreePool(p);
        p = NULL;
        buffer = NULL;

        return status;
    }

    status = efi_file->Close(efi_file);

    if (EFI_ERROR(status)){

        FreePool(p);
        p = NULL;
        buffer = NULL;

        return status;
    }

    return EFI_SUCCESS;
}

static EFI_FILE* open_print_info(CHAR16* path)
{
    EFI_FILE_IO_INTERFACE* efi_simple_file_system = NULL;
    EFI_FILE* efi_file_root = NULL;
    EFI_FILE* efi_file = NULL;

    EFI_STATUS status = BS->OpenProtocol(
        loaded_image->DeviceHandle,
        &FileSystemProtocol,
        &efi_simple_file_system,
        IH,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (EFI_ERROR(status)){

        error_print(L"OpenProtocol() FileSystemProtocol failed.\n", &status);
    }

    status = efi_simple_file_system->OpenVolume(
        efi_simple_file_system, &efi_file_root
    );

    if (EFI_ERROR(status)){

        error_print(L"OpenVolume() failed.\n", &status);
    }

    status = efi_file_root->Open(
        efi_file_root, &efi_file, path,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
        EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE
    );

    if (EFI_ERROR(status)){

        error_print(L"Open() failed.\n", &status);
    }

    status = efi_file_root->Close(efi_file_root);

    if (EFI_ERROR(status)){

        error_print(L"Close() failed.\n", &status);
    }

    return efi_file;
}

static void convert_to_ascii(char* ascii, CHAR16* wide)
{
    UINTN size = StrLen(wide);

    for (UINTN i = 0; size > i; ++i){

        ascii[i] = (char)(wide[i] & 0xff);
    }

    ascii[size] = '\0';
}

static void write_print_info(EFI_FILE* efi_file, CHAR16* fmt, CHAR16* param)
{
    if (efi_file && fmt && param){

        CHAR16 buffer[256];
        SPrint(buffer, sizeof(buffer), fmt, param);
        StrCat(buffer, L"\n");

        Print(buffer);

        SPrint(buffer, sizeof(buffer), fmt, param);
        StrCat(buffer, L"\r\n");

        char buffer_a[256];
        convert_to_ascii(buffer_a, buffer);

        UINTN size = (UINTN)strlena(buffer_a);

        EFI_STATUS status = efi_file->Write(efi_file, &size, buffer_a);

        if (EFI_ERROR(status)){

            error_print(L"Write() failed.\n", &status);
        }
    }
}

static void close_print_info(EFI_FILE* efi_file)
{
    if (efi_file){

        EFI_STATUS status = efi_file->Close(efi_file);

        if (EFI_ERROR(status)){

            error_print(L"Close() failed.\n", &status);
        }
    }
}

