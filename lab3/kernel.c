#include <stdint.h>
#include <stddef.h>

#define MULTIBOOT_MAGIC          0x36D76289
#define MULTIBOOT_TAG_END        0
#define MULTIBOOT_TAG_ACPI_RSDP  14 //Тег, в котором лежит RSDP
#define MULTIBOOT_TAG_FRAMEBUFFER 8 // Тег с информацией о графическом буфере

// Общая структура любого тега
struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

// RSDP — корневая структура, которую передаёт GRUB
struct rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
};

struct acpi_table_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
};

struct multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t framebuffer_type;
    uint8_t reserved[5];
};



static inline void outb(uint16_t port, uint8_t val) {// Запись байта в порт
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void serial_print(const char *s) {
    while (*s) outb(0x3F8, *s++);
}

void serial_hex(uint64_t n) {
    char buf[17] = {0};
    for (int i = 15; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[n & 0xF];
        n >>= 4;
    }
    serial_print("0x"); serial_print(buf);
}

void serial_dec(uint32_t n) {
    if (n == 0) { serial_print("0"); return; }
    char buf[11] = {0};
    int i = 10;
    while (n) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    serial_print(&buf[i]);
}

void kernel_main(uint32_t magic, uintptr_t mb_addr) {
    if (magic != MULTIBOOT_MAGIC) return;

    serial_print("ACPI таблицы: название, размер, адрес\n\n");

    struct multiboot_tag *tag = (struct multiboot_tag*)(mb_addr + 8);
    struct rsdp *rsdp = NULL;
    struct multiboot_tag_framebuffer *fb_tag = NULL;

    while (tag->type != MULTIBOOT_TAG_END) {//Поиск тегов
        if (tag->type == MULTIBOOT_TAG_ACPI_RSDP)
            rsdp = (struct rsdp*)((uint8_t*)tag + 8);// +8 — пропускаем type и size
        if (tag->type == MULTIBOOT_TAG_FRAMEBUFFER)
            fb_tag = (struct multiboot_tag_framebuffer*)tag;
        tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7));//переходим к следующему тегу(выравнивание по 8 байт)
    }

    if (fb_tag) {
        uint32_t *fb = (uint32_t*)(uintptr_t)fb_tag->addr;
        uint32_t w = fb_tag->width;
        uint32_t h = fb_tag->height;
        for (uint32_t i = 0; i < w*h; i++) fb[i] = 0x0000AA;
        for (uint32_t y = 100; y < 250; y++)
            for (uint32_t x = 100; x < 300; x++)
                fb[y*w + x] = 0xFF0000;
    }



    if (!rsdp) {
        serial_print("RSDP не найден!\n");
        while(1);
    }

    serial_print("RSDP revision: "); serial_dec(rsdp->revision); serial_print("\n\n");

    uint64_t root_addr = (rsdp->revision >= 2 && rsdp->xsdt_address) ? rsdp->xsdt_address : rsdp->rsdt_address;//выбора между двумя стандартами
    uint32_t entry_size = (rsdp->revision >= 2 && rsdp->xsdt_address) ? 8 : 4;

    struct acpi_table_header *root = (struct acpi_table_header*)(uintptr_t)root_addr;
    uint32_t entries = (root->length - 36) / entry_size;

    serial_print("=== СПИСОК ТАБЛИЦ ===\n");
    serial_print("Всего таблиц: "); serial_dec(entries); serial_print("\n\n");

    for (uint32_t i = 0; i < entries; i++) {
        uint64_t addr = (entry_size == 8) ?
            *(uint64_t*)((uint8_t*)root + 36 + i*8) :
            *(uint32_t*)((uint8_t*)root + 36 + i*4);

        struct acpi_table_header *t = (struct acpi_table_header*)(uintptr_t)addr;

        char sig[5] = {t->signature[0], t->signature[1], t->signature[2], t->signature[3], '\0'};

        serial_print("Таблица ");
        serial_dec(i);
        serial_print(": ");
        serial_print(sig);
        serial_print("   addr=");
        serial_hex(addr);
        serial_print("   size=");
        serial_dec(t->length);
        serial_print(" bytes");

        if (sig[0]=='F' && sig[1]=='A' && sig[2]=='C' && sig[3]=='P')
            serial_print("FACP --  главная таблица ACPI, power management, sleep, reset");
        else if (sig[0]=='A' && sig[1]=='P' && sig[2]=='I' && sig[3]=='C')
            serial_print("APIC -- процессоры, локальные APIC и прерывания");
        else if (sig[0]=='H' && sig[1]=='P' && sig[2]=='E' && sig[3]=='T')
            serial_print("HPET -- высокоточный таймер");
        else if (sig[0]=='W' && sig[1]=='A' && sig[2]=='E' && sig[3]=='T')
            serial_print("WAET -- таблица эмулируемых устройств Windows");
        else if (sig[0]=='B' && sig[1]=='G' && sig[2]=='R' && sig[3]=='T')
            serial_print("BGRT -- таблица логотипа при загрузке");
        else
            serial_print("Неизвестная таблица");

        serial_print("\n");
    }

    while(1);
}
