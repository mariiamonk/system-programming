#include <Uefi.h>
#include <PiDxe.h>
#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/Acpi.h>

#define MULTIBOOT2_MAGIC 0xE85250D6
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8

#pragma pack(1) //просим компилятор не вставлять путсые бацты между поялми
typedef struct {
  UINT32 Magic;
  UINT32 Architecture;
  UINT32 HeaderLength;
  UINT32 Checksum;
} MULTIBOOT2_HEADER; //то что мы ищем в файле ядра

typedef struct {
  UINT16    Type;
  UINT16    Flags;
  UINT32    Size;
  UINT32    EntryAddress;
} MULTIBOOT2_HEADER_TAG_EFI64_ENTRY;

typedef struct {
  UINT32 Type;
  UINT32 Size;
  UINT64 Addr;
  UINT32 Pitch;
  UINT32 Width;
  UINT32 Height;
  UINT8  Bpp;
  UINT8  FramebufferType;
  UINT8  Reserved[5];
} MULTIBOOT2_TAG_FRAMEBUFFER;

typedef struct {
  UINT32 Type;
  UINT32 Size;
  UINT8  RsdpData[36];
} MULTIBOOT2_TAG_ACPI_RSDP;

typedef struct {
  char Signature[8];
  UINT8 Checksum;
  char OemId[6];
  UINT8 Revision;
  UINT32 RsdtAddress;
  UINT32 Length;
  UINT64 XsdtAddress;
  UINT8 ExtendedChecksum;
  UINT8 Reserved[3];
} RSDP;
#pragma pack()

VOID EFIAPI JumpToUefiKernel (VOID *KernelStart, VOID *KernelBootParams);

EFI_STATUS EFIAPI FindKernelHeader (IN VOID *Buffer, IN UINTN BufferSize, IN UINTN *KernelOffset);
VOID *EFIAPI FindKernelEntryPoint (IN VOID *Buffer, IN UINTN BufferSize);
EFI_STATUS EFIAPI MakeFbTag (IN MULTIBOOT2_TAG_FRAMEBUFFER *Tag);
//EFI_STATUS: Тип возвращаемого значения (успех или ошибка)
//EFIAPI: Правило вызова функций (соглашение), принятое в UEFI.
EFI_STATUS EFIAPI FindKernelHeader (IN VOID *Buffer, IN UINTN BufferSize, IN UINTN *KernelOffset)
{//Проходит по всему считанному файлу ядра шагами по 4 байта. Ищет магическое число. сохраняет смещение
  MULTIBOOT2_HEADER *Header;
  UINTN             Offset;

  if (Buffer == NULL || KernelOffset == NULL || BufferSize < sizeof(MULTIBOOT2_HEADER)) {
    return EFI_INVALID_PARAMETER;
  }

  for (Offset = 0; Offset <= BufferSize - sizeof(MULTIBOOT2_HEADER); Offset += 4) {
    Header = (MULTIBOOT2_HEADER *)((UINT8 *)Buffer + Offset);
    if (Header->Magic == MULTIBOOT2_MAGIC) break;
  }

  *KernelOffset = Offset;
  return EFI_SUCCESS;
}


//Buffer указатель на начало Multiboot2-заголовока
VOID *EFIAPI FindKernelEntryPoint (IN VOID *Buffer, IN UINTN BufferSize)
{//После основного заголовка в ядре идут теги. Один из них (тип 9) содержит адрес, по которому нужно прыгнуть, чтобы запустить код
  MULTIBOOT2_HEADER *Header;
  UINT8             *CurrentPtr;
  UINT8             *BufferEnd;

  if (Buffer == NULL || BufferSize < sizeof(MULTIBOOT2_HEADER)) return NULL;

  BufferEnd = (UINT8 *)Buffer + BufferSize;//Крайняя точка в памяти
  Header = (MULTIBOOT2_HEADER *)Buffer;//начало буфера структуру заголовка
  CurrentPtr = (UINT8 *)Header + sizeof(MULTIBOOT2_HEADER);//Указатель на первый тег

  while (CurrentPtr < (UINT8 *)Header + Header->HeaderLength) {//идем по памяти, пока не пройдем полный размер всех тегов заголовка
    MULTIBOOT2_HEADER_TAG_EFI64_ENTRY *Tag = (MULTIBOOT2_HEADER_TAG_EFI64_ENTRY *)CurrentPtr; //смотрим, на что указывает CurrentPtr
    if (Tag->Type == 0 && Tag->Size == 8) break;
    if (Tag->Type == 9) return (VOID *)(UINTN)Tag->EntryAddress; //тэг 9 в спецификации Multiboot2 "точка входа"

    CurrentPtr += ALIGN_VALUE(Tag->Size, 8); //В Multiboot2 каждый тег выровнен по 8 байт
    if (CurrentPtr >= BufferEnd) break;//Функция возвращает адрес на точку входа в ядро
  }
  return NULL;
}
EFI_STATUS EFIAPI MakeFbTag (IN MULTIBOOT2_TAG_FRAMEBUFFER *Tag)
{//Настройка графики
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput;
  EFI_STATUS Status;

  Status = gBS->HandleProtocol(gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&GraphicsOutput);//запрашиваем у UEFI про>
  if (EFI_ERROR(Status))
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&GraphicsOutput);//ищет любое устройство в системе, кото>
  if (EFI_ERROR(Status)) return Status;


  //получили указатель на GraphicsOutput, мы начинаем перекладывать из него данные в структуру, которую получит ядро
  Tag->Type = MULTIBOOT_TAG_TYPE_FRAMEBUFFER;
  Tag->Size = sizeof(MULTIBOOT2_TAG_FRAMEBUFFER);
  Tag->Addr = GraphicsOutput->Mode->FrameBufferBase;//физический адрес в памяти, где начинаются пиксели.
  Tag->Pitch = GraphicsOutput->Mode->Info->PixelsPerScanLine * 4;//сколько байт занимает одна строка
  Tag->Width = GraphicsOutput->Mode->Info->HorizontalResolution;
  Tag->Height = GraphicsOutput->Mode->Info->VerticalResolution;
  Tag->Bpp = 32;
  Tag->FramebufferType = 1;
  ZeroMem(Tag->Reserved, 5);

  return EFI_SUCCESS;
}


//ImageHandle: Уникальный номер запущенного загрузчика в памяти.
//SystemTable: Указатель на главную таблицу UEFI, через которую доступны все функции

EFI_STATUS EFIAPI UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_DEVICE_PATH_PROTOCOL *DevPath;//адрес файла в дереве оборудования UEFI
  VOID *FileBuffer;//Сюда мы временно считаем файл kernel.bin
  UINTN FileSize;
  UINT32 AuthenticationStatus;//Флаг проверки подлинности файла
  UINTN KernelOffset;
  UINTN KernelSize;//адрес, куда мы окончательно переложим ядро
  EFI_PHYSICAL_ADDRESS KernelAddress = 0;
  UINTN Pages;//Количество «страниц» памяти, необходимых для ядра
  VOID *KernelStart;
  VOID *Mbi = NULL;//Указатель на структуру Multiboot Information
  RSDP *Rsdp = NULL;

  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage); //Получение информации о носителе
  if (EFI_ERROR(Status)) goto Error;

  // Загрузка файла с диска
  DevPath = FileDevicePath(LoadedImage->DeviceHandle, L"\\kernel.bin");//путь к файлу ядра
  FileBuffer = GetFileBufferByFilePath(FALSE, DevPath, &FileSize, &AuthenticationStatus);//Читаем весь файл в оперативную память
  if (FileBuffer == NULL) goto Error;

  Status = FindKernelHeader(FileBuffer, FileSize, &KernelOffset);
  if (EFI_ERROR(Status)) goto Error;
  //Подготовка памяти для ядра
  KernelSize = FileSize - KernelOffset;
  Pages = KernelSize / 4096 + 1;

  KernelAddress = 0x100000;
  Status = gBS->AllocatePages(AllocateAddress, EfiLoaderCode, Pages, &KernelAddress); //Просим UEFI зарезервировать физическую память по >
  if (EFI_ERROR(Status)) goto Error;

  gBS->CopyMem((VOID *)(UINTN)KernelAddress, (UINT8*)FileBuffer + KernelOffset, KernelSize);//Копируем содержимое файла из временного буф>

  KernelStart = FindKernelEntryPoint((VOID *)(UINTN)KernelAddress, KernelSize);
  if (KernelStart == NULL) goto Error;


  //ормирование Multiboot Information
  Status = gBS->AllocatePool(EfiLoaderCode, 256, &Mbi);//Выделяем 256 байт под эту структуру
  if (EFI_ERROR(Status)) goto Error;

  UINT8 *MbiPtr = (UINT8 *)Mbi; //Записываем общий размер и зарезервированный ноль
  ((UINT32*)MbiPtr)[0] = 8;
  ((UINT32*)MbiPtr)[1] = 0;
  UINTN Offset = 8;

  MULTIBOOT2_TAG_FRAMEBUFFER FbTag = {0};
  Status = MakeFbTag(&FbTag);
  if (EFI_ERROR(Status)) goto Error;
  CopyMem(MbiPtr + Offset, &FbTag, FbTag.Size);
  Offset += (FbTag.Size + 7) & ~7U;//выравнивания на 8 байт

  //gST->ConfigurationTable: Это массив в системной таблице UEFI
  for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid(&gST->ConfigurationTable[i].VendorGuid, &gEfiAcpi20TableGuid) ||
        CompareGuid(&gST->ConfigurationTable[i].VendorGuid, &gEfiAcpi10TableGuid)) {
      Rsdp = (RSDP *)gST->ConfigurationTable[i].VendorTable; //Мы перебираем системные таблицы UEFI
      break;
    }
  }

  if (Rsdp) { //Если нашли: Создаем AcpiTag, копируем туда данные RSDP и добавляем этот тег в нашу общую структуру Mbi
    MULTIBOOT2_TAG_ACPI_RSDP AcpiTag = {0};
    AcpiTag.Type = 14;
    AcpiTag.Size = 8 + 36; //служебный заголовок самого тега + размер структуры RSDP
    CopyMem(AcpiTag.RsdpData, Rsdp, 36);
    CopyMem(MbiPtr + Offset, &AcpiTag, AcpiTag.Size);
    Offset += 48;
    Print(L"RSDP найден и передан ядру (revision=%d)\n", Rsdp->Revision);
  } else {
    Print(L"RSDP не найден!\n");
  }

  UINT32 EndTag[2] = {0, 8};
  CopyMem(MbiPtr + Offset, EndTag, 8);
  Offset += 8;
  ((UINT32*)MbiPtr)[0] = (UINT32)Offset;//аписан итоговый размер

  DEBUG((DEBUG_ERROR, "KernelStart: 0x%X, BootInfoPtr: 0x%X\n", KernelStart, Mbi));
  JumpToUefiKernel(KernelStart, Mbi);

Error:
  if (KernelAddress != 0) gBS->FreePages(KernelAddress, Pages);
  if (Mbi != NULL) gBS->FreePool(Mbi);
  return Status;
}
