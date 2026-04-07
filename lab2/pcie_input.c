#include <linux/init.h>      
#include <linux/module.h>   
#include <linux/pci.h>       
#include <linux/cdev.h>      // Поддержка символьных устройств (cdev)
#include <linux/device.h>    
#include <linux/kernel.h>    
#include <linux/slab.h>      
#include <linux/uaccess.h>   // копирование user↔kernel
#include <linux/fs.h>        
#include <linux/ioctl.h>     
#include <linux/spinlock.h>  
#include <linux/wait.h>      
#include <linux/sched.h>     // Планировщик (для ожиданий)
#include <linux/poll.h>      // poll/select поддержка

#define TESTDEV_DRIVER "pcie_input"   // Имя драйвера
#define TESTDEV_VENDOR_ID   0x1B36    
#define TESTDEV_PRODUCT_ID  0x0005    
#define TESTDEV_BAR_NUM     2         // Используем BAR2
#define TESTDEV_BAR_MASK    (1 << TESTDEV_BAR_NUM) // Маска для проверки BAR2

#define REG_MAX_BUFFER_SIZE    0x00
#define REG_BUFFER_SIZE        0x04
#define REG_BUFFER_ADDR        0x08
#define REG_WRITE_PTR          0x0C
#define REG_READ_PTR           0x10
#define REG_STATUS             0x14
#define REG_ACK                0x18

// Флаги статуса
#define STATUS_DATA_AVAILABLE  (1 << 0)
#define STATUS_BUFFER_FULL     (1 << 1)
#define STATUS_BUFFER_EMPTY    (1 << 2)

#define PCIE_MAGIC 'p'
#define PCIE_GET_MAX_BUFFER_SIZE _IOR(PCIE_MAGIC, 1, unsigned int)
#define PCIE_SET_BUFFER_SIZE _IOW(PCIE_MAGIC, 2, unsigned int)
#define PCIE_GET_BUFFER_ADDR _IOR(PCIE_MAGIC, 3, unsigned long)
#define PCIE_GET_WRITE_PTR _IOR(PCIE_MAGIC, 4, unsigned long)
#define PCIE_GET_READ_PTR _IOR(PCIE_MAGIC, 5, unsigned long)
#define PCIE_GET_STATUS _IOR(PCIE_MAGIC, 6, unsigned int)

// Таблица поддерживаемых PCI устройств
static struct pci_device_id testdev_id_table[] = {
    { PCI_DEVICE(TESTDEV_VENDOR_ID, TESTDEV_PRODUCT_ID) }, // Совпадение VID/PID
    { 0, }  // Конец таблицы
};

MODULE_DEVICE_TABLE(pci, testdev_id_table); // Сообщаем ядру о таблице

static int testdev_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void testdev_remove(struct pci_dev *pdev);

//  Описание PCI драйвера
static struct pci_driver testdev = {
    .name       = TESTDEV_DRIVER,
    .id_table   = testdev_id_table,
    .probe      = testdev_probe,
    .remove     = testdev_remove
};

struct testdev_data {
    struct pci_dev *pdev;        // Указатель на PCI устройство
    u8 __iomem      *hwmem;      // Отображённая BAR память 
    unsigned long bar_len;       // Длина BAR
    unsigned int buffer_size;    // Размер кольцевого буфера
    unsigned int buffer_addr;    // Смещение буфера в BAR
    wait_queue_head_t read_queue;// Очередь ожидания для poll/select
    spinlock_t lock;             // Спинлок для защиты от гонок
    struct device* char_device;  // Устройство /dev
    struct cdev cdev;            // Символьное устройство
};

int create_char_devs(struct pci_dev *pdev, struct testdev_data *drv);
void destroy_char_devs(struct testdev_data *drv);

int testdev_open(struct inode *inode, struct file *file);
int testdev_release(struct inode *inode, struct file *file);
long testdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
ssize_t testdev_read(struct file *file, char __user *buf, size_t count, loff_t *offset);
ssize_t testdev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);
unsigned int testdev_poll(struct file *file, poll_table *wait);

const struct file_operations testdev_fops = {
    .owner          = THIS_MODULE,
    .open           = testdev_open,
    .release        = testdev_release,
    .unlocked_ioctl = testdev_ioctl,
    .read           = testdev_read,
    .write          = testdev_write,
    .poll           = testdev_poll,
};

int dev_major = 0;
struct class *testdevclass = NULL;

inline u32 read_reg32(struct testdev_data *dev, unsigned int offset)
{
    return ioread32(dev->hwmem + offset);
}

inline void write_reg32(struct testdev_data *dev, unsigned int offset, u32 value)
{
    iowrite32(value, dev->hwmem + offset);
}

inline u8 read_reg8(struct testdev_data *dev, unsigned int offset)
{
    return ioread8(dev->hwmem + offset);
}

ssize_t max_buffer_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct testdev_data *drv = dev_get_drvdata(dev);
    unsigned int max_size = read_reg32(drv, REG_MAX_BUFFER_SIZE);
    return sprintf(buf, "%u\n", max_size);
}
DEVICE_ATTR(max_buffer_size, 0444, max_buffer_size_show, NULL);

ssize_t buffer_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct testdev_data *drv = dev_get_drvdata(dev);
    return sprintf(buf, "%u\n", drv->buffer_size);
}

ssize_t buffer_size_store(struct device *dev, struct device_attribute *attr, 
                                const char *buf, size_t count)
{
    struct testdev_data *drv = dev_get_drvdata(dev);
    unsigned int new_size;
    unsigned long flags;
    
    if (kstrtouint(buf, 0, &new_size) != 0)
        return -EINVAL;
    
    spin_lock_irqsave(&drv->lock, flags);
    
    unsigned int max_size = read_reg32(drv, REG_MAX_BUFFER_SIZE);
    if (new_size > max_size) {
        spin_unlock_irqrestore(&drv->lock, flags);
        return -EINVAL;
    }
    
    write_reg32(drv, REG_BUFFER_SIZE, new_size);
    drv->buffer_size = new_size;
    
    spin_unlock_irqrestore(&drv->lock, flags);
    
    return count;
}
DEVICE_ATTR(buffer_size, 0644, buffer_size_show, buffer_size_store);

ssize_t buffer_addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct testdev_data *drv = dev_get_drvdata(dev);
    return sprintf(buf, "0x%x\n", drv->buffer_addr);
}
DEVICE_ATTR(buffer_addr, 0444, buffer_addr_show, NULL);

ssize_t write_ptr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct testdev_data *drv = dev_get_drvdata(dev);
    unsigned long ptr = read_reg32(drv, REG_WRITE_PTR);
    return sprintf(buf, "0x%lx\n", ptr);
}
DEVICE_ATTR(write_ptr, 0444, write_ptr_show, NULL);

ssize_t read_ptr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct testdev_data *drv = dev_get_drvdata(dev);
    unsigned long ptr = read_reg32(drv, REG_READ_PTR);
    return sprintf(buf, "0x%lx\n", ptr);
}
DEVICE_ATTR(read_ptr, 0444, read_ptr_show, NULL);

ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct testdev_data *drv = dev_get_drvdata(dev);
    unsigned int status = read_reg32(drv, REG_STATUS);
    return sprintf(buf, "0x%x\n", status);
}
DEVICE_ATTR(status, 0444, status_show, NULL);

int create_char_devs(struct pci_dev *pdev, struct testdev_data *drv)
{
    int err;
    dev_t dev;
    
    err = alloc_chrdev_region(&dev, 0, 1, "pcie_input");
    if (err < 0) {
        printk(KERN_ERR "PCIe Input: Failed to allocate chrdev region\n");
        return err;
    }
    
    dev_major = MAJOR(dev);
    
    testdevclass = class_create(THIS_MODULE, "pcie_input");
    if (IS_ERR(testdevclass)) {
        err = PTR_ERR(testdevclass);
        unregister_chrdev_region(dev, 1);
        return err;
    }
    
    cdev_init(&drv->cdev, &testdev_fops);
    drv->cdev.owner = THIS_MODULE;
    
    err = cdev_add(&drv->cdev, MKDEV(dev_major, 0), 1);
    if (err) {
        class_destroy(testdevclass);
        unregister_chrdev_region(dev, 1);
        return err;
    }
    
    drv->char_device = device_create(testdevclass, NULL, 
                                    MKDEV(dev_major, 0), drv, 
                                    "pcie_input");
    if (IS_ERR(drv->char_device)) {
        err = PTR_ERR(drv->char_device);
        cdev_del(&drv->cdev);
        class_destroy(testdevclass);
        unregister_chrdev_region(dev, 1);
        return err;
    }
    
    device_create_file(drv->char_device, &dev_attr_max_buffer_size);
    device_create_file(drv->char_device, &dev_attr_buffer_size);
    device_create_file(drv->char_device, &dev_attr_buffer_addr);
    device_create_file(drv->char_device, &dev_attr_write_ptr);
    device_create_file(drv->char_device, &dev_attr_read_ptr);
    device_create_file(drv->char_device, &dev_attr_status);
    
    drv->buffer_size = read_reg32(drv, REG_BUFFER_SIZE);
    if (drv->buffer_size == 0) {
        drv->buffer_size = 1024;
        write_reg32(drv, REG_BUFFER_SIZE, 1024);
    }
    
    drv->buffer_addr = read_reg32(drv, REG_BUFFER_ADDR);
    init_waitqueue_head(&drv->read_queue);
    spin_lock_init(&drv->lock);
    
    printk(KERN_INFO "PCIe Input: Initialized. Buffer: 0x%x, Size: %u\n",
           drv->buffer_addr, drv->buffer_size);
    
    return 0;
}

void destroy_char_devs(struct testdev_data *drv)
{
    if (drv && drv->char_device) {
        device_remove_file(drv->char_device, &dev_attr_max_buffer_size);
        device_remove_file(drv->char_device, &dev_attr_buffer_size);
        device_remove_file(drv->char_device, &dev_attr_buffer_addr);
        device_remove_file(drv->char_device, &dev_attr_write_ptr);
        device_remove_file(drv->char_device, &dev_attr_read_ptr);
        device_remove_file(drv->char_device, &dev_attr_status);
        
        device_destroy(testdevclass, MKDEV(dev_major, 0));
        drv->char_device = NULL;
    }
    
    if (testdevclass) {
        class_destroy(testdevclass);
        testdevclass = NULL;
    }
    
    if (dev_major) {
        unregister_chrdev_region(MKDEV(dev_major, 0), 1);
        dev_major = 0;
    }
    
    if (drv && drv->cdev.owner) {
        cdev_del(&drv->cdev);
    }
}

int testdev_open(struct inode *inode, struct file *file)
{
    struct testdev_data *dev_priv;
    
    dev_priv = container_of(inode->i_cdev, struct testdev_data, cdev);
    file->private_data = dev_priv;
    return 0;
}

int testdev_release(struct inode *inode, struct file *file)
{
    file->private_data = NULL;
    return 0;
}

unsigned int testdev_poll(struct file *file, poll_table *wait)
{
    struct testdev_data *drv = file->private_data;  // Получаем приватные данные драйвера
    unsigned int mask = 0;                          
    unsigned long flags;                            // Для сохранения состояния прерываний
    
    // Регистрируем процесс в очереди ожидания 
    poll_wait(file, &drv->read_queue, wait);
    
    // отключаем прерывания
    spin_lock_irqsave(&drv->lock, flags);
    
    // Читаем указатели кольцевого буфера
    unsigned long write_ptr = read_reg32(drv, REG_WRITE_PTR);
    unsigned long read_ptr  = read_reg32(drv, REG_READ_PTR);
    
    unsigned long available;
    
    // Вычисляем количество доступных данных
    if (write_ptr >= read_ptr) {
        available = write_ptr - read_ptr;
    } else {
        // Переполнение 32-битного счетчика
        available = (0xFFFFFFFF - read_ptr) + write_ptr + 1;
    }
    
    // Если есть данные — сообщаем что устройство готово к чтению
    if (available > 0) {
        mask |= POLLIN | POLLRDNORM;
    }
    
    // Разблокируем спинлок
    spin_unlock_irqrestore(&drv->lock, flags);
    
    return mask; // Возвращаем флаги готовности
}

//чтение из кольцевого буфера
ssize_t testdev_read(struct file *file, char __user *buf,
                            size_t count, loff_t *offset)
{
    struct testdev_data* drv = file->private_data; // Получаем структуру драйвера
    unsigned long flags;
    ssize_t bytes_read = 0; // Сколько реально прочитали
    
    if (count == 0)      // Если запросили 0 байт — сразу выходим
        return 0;
    
    if (*offset > 0)     // Поддерживаем только однократное чтение
        return 0;
    
    spin_lock_irqsave(&drv->lock, flags); // Блокируем доступ
    
    // Читаем указатели
    unsigned long write_ptr = read_reg32(drv, REG_WRITE_PTR);
    unsigned long read_ptr  = read_reg32(drv, REG_READ_PTR);
    unsigned int buffer_size = drv->buffer_size;
    
    unsigned long available;
    
    // Вычисляем сколько данных доступно
    if (write_ptr > read_ptr) {
        available = write_ptr - read_ptr;
    } else if (write_ptr < read_ptr) {
        available = (0xFFFFFFFF - read_ptr) + write_ptr + 1;
    } else {
        available = 0;
    }
    
    // Сколько реально будем читать
    unsigned long to_read = min((unsigned long)count, available);
    
    // Читаем байт за байтом
    for (unsigned long i = 0; i < to_read; i++) {
        
        // Позиция внутри кольцевого буфера
        unsigned long buffer_pos = (read_ptr + i) % buffer_size;
        
        // Смещение внутри BAR
        unsigned long bar_offset = drv->buffer_addr + buffer_pos;
        
        // Проверка выхода за границы BAR
        if (bar_offset >= drv->bar_len) {
            spin_unlock_irqrestore(&drv->lock, flags);
            return -EIO;
        }
        
        // Читаем байт из MMIO
        char data = read_reg8(drv, bar_offset);
        
        // Разблокируем перед копированием в userspace
        spin_unlock_irqrestore(&drv->lock, flags);
        
        // Копируем байт пользователю
        if (put_user(data, buf + bytes_read + i)) {
            return -EFAULT;
        }
        
        // Снова блокируем
        spin_lock_irqsave(&drv->lock, flags);
    }
    
    // Обновляем указатель чтения
    read_ptr += to_read;
    write_reg32(drv, REG_READ_PTR, read_ptr);
    
    bytes_read = to_read;
    
    spin_unlock_irqrestore(&drv->lock, flags);
    
    if (bytes_read > 0)
        *offset += bytes_read;
    
    return bytes_read;
}


ssize_t testdev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
    return count;
}


// управление устройством
long testdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct testdev_data* drv = file->private_data;
    unsigned long flags;
    int ret = 0;
    
    switch (cmd) {

    // Получить максимальный размер буфера
    case PCIE_GET_MAX_BUFFER_SIZE: {
        unsigned int max_size = read_reg32(drv, REG_MAX_BUFFER_SIZE);
        if (put_user(max_size, (unsigned int __user *)arg))
            return -EFAULT;
        break;
    }

    // Установить размер буфера    
    case PCIE_SET_BUFFER_SIZE: {
        unsigned int new_size;
        if (get_user(new_size, (unsigned int __user *)arg))
            return -EFAULT;
        
        spin_lock_irqsave(&drv->lock, flags);
        
        unsigned int max_size = read_reg32(drv, REG_MAX_BUFFER_SIZE);
        if (new_size > max_size) {
            spin_unlock_irqrestore(&drv->lock, flags);
            return -EINVAL;
        }
        
        write_reg32(drv, REG_BUFFER_SIZE, new_size);
        drv->buffer_size = new_size;
        
        spin_unlock_irqrestore(&drv->lock, flags);
        break;
    }
    
    case PCIE_GET_BUFFER_ADDR: {
        unsigned int addr = drv->buffer_addr;
        if (put_user(addr, (unsigned int __user *)arg))
            return -EFAULT;
        break;
    }
    
    case PCIE_GET_WRITE_PTR: {
        unsigned long ptr = read_reg32(drv, REG_WRITE_PTR);
        if (put_user(ptr, (unsigned long __user *)arg))
            return -EFAULT;
        break;
    }
    
    case PCIE_GET_READ_PTR: {
        unsigned long ptr = read_reg32(drv, REG_READ_PTR);
        if (put_user(ptr, (unsigned long __user *)arg))
            return -EFAULT;
        break;
    }
    
    case PCIE_GET_STATUS: {
        unsigned int status = read_reg32(drv, REG_STATUS);
        if (put_user(status, (unsigned int __user *)arg))
            return -EFAULT;
        break;
    }
    
    default:
        return -ENOTTY;
    }
    
    return ret;
}

int __init testdev_driver_init(void)
{
    printk(KERN_INFO "PCIe Input Driver: Initializing\n");
    return pci_register_driver(&testdev);
}

void __exit testdev_driver_exit(void)
{
    pci_unregister_driver(&testdev);
    printk(KERN_INFO "PCIe Input Driver: Exiting\n");
}


// инициализация устройства
int testdev_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int bar, err;
    u16 vendor, device;
    unsigned long mmio_start, mmio_len;
    struct testdev_data *drv_data;
    
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device);
    
    printk(KERN_INFO "PCIe Input: Device found vid: 0x%X pid: 0x%X\n", vendor, device);
    
    // Проверяем есть ли BAR2
    bar = pci_select_bars(pdev, IORESOURCE_MEM);
    if (!(bar & TESTDEV_BAR_MASK)) {
        printk(KERN_ERR "PCIe Input: BAR2 not found\n");
        return -ENODEV;
    }
    
    // Включаем устройство
    err = pci_enable_device_mem(pdev);
    if (err) {
        printk(KERN_ERR "PCIe Input: Failed to enable device\n");
        return err;
    }
    
    err = pci_request_region(pdev, TESTDEV_BAR_NUM, TESTDEV_DRIVER);
    if (err) {
        printk(KERN_ERR "PCIe Input: Failed to request region\n");
        pci_disable_device(pdev);
        return err;
    }
    
    mmio_start = pci_resource_start(pdev, TESTDEV_BAR_NUM);
    mmio_len = pci_resource_len(pdev, TESTDEV_BAR_NUM);
    
    // Выделяем память под структуру драйвера
    drv_data = kzalloc(sizeof(struct testdev_data), GFP_KERNEL);
    if (!drv_data) {
        printk(KERN_ERR "PCIe Input: Failed to allocate memory\n");
        pci_release_regions(pdev);
        pci_disable_device(pdev);
        return -ENOMEM;
    }
    
    drv_data->pdev = pdev;
    drv_data->bar_len = mmio_len;
    
    // Отображаем BAR в виртуальную память ядра
    drv_data->hwmem = ioremap(mmio_start, mmio_len);
    if (!drv_data->hwmem) {
        printk(KERN_ERR "PCIe Input: Failed to ioremap\n");
        kfree(drv_data);
        pci_release_regions(pdev);
        pci_disable_device(pdev);
        return -EIO;
    }
    
    printk(KERN_INFO "PCIe Input: BAR2 mapped at 0x%lx to 0x%p, size: 0x%lx\n", 
           mmio_start, drv_data->hwmem, mmio_len);
    
    err = create_char_devs(pdev, drv_data);
    if (err) {
        printk(KERN_ERR "PCIe Input: Failed to create char devices\n");
        iounmap(drv_data->hwmem);
        kfree(drv_data);
        pci_release_regions(pdev);
        pci_disable_device(pdev);
        return err;
    }
    
    // Привязываем структуру к PCI устройству
    pci_set_drvdata(pdev, drv_data);
    
    printk(KERN_INFO "PCIe Input: Driver loaded successfully\n");
    return 0;
}


//удаление устройства
void testdev_remove(struct pci_dev *pdev)
{
    struct testdev_data *drv_data = pci_get_drvdata(pdev);
    
    printk(KERN_INFO "PCIe Input: Removing device\n");
    
    destroy_char_devs(drv_data);
    
    if (drv_data) {
        if (drv_data->hwmem)
            iounmap(drv_data->hwmem);
        
        kfree(drv_data);
    }
    
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    
    printk(KERN_INFO "PCIe Input: Device removed\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mariia");
MODULE_DESCRIPTION("PCIe Input Device Driver for QEMU pci-testdev");
MODULE_VERSION("1.0");

module_init(testdev_driver_init);
module_exit(testdev_driver_exit);
