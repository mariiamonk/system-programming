#!/usr/bin/env python3

import struct      # Для упаковки/распаковки бинарных данных 
import time        
import sys         # Для доступа к аргументам командной строки
import os          # Для работы с файловой системой
import argparse    # Для удобного парсинга аргументов

BAR2_FILE = "bar2.bin"

# Смещения регистров внутри BAR2 
REG_MAX_BUFFER_SIZE = 0x00   # Максимальный размер буфера (RO)
REG_BUFFER_SIZE     = 0x04   # Текущий размер буфера (RW)
REG_BUFFER_ADDR     = 0x08   # Адрес начала буфера данных
REG_WRITE_PTR       = 0x0C   # Указатель записи
REG_READ_PTR        = 0x10   # Указатель чтения
REG_STATUS          = 0x14   # Регистр статуса
REG_ACK             = 0x18   # Регистр подтверждения 

DEFAULT_MAX_BUFFER  = 4096
DEFAULT_BUFFER_SIZE = 1024
DEFAULT_BUFFER_ADDR = 0x1000


# Инициализация регистров устройства
def init_registers():

    # Проверяем, существует ли файл BAR2
    if not os.path.exists(BAR2_FILE):
        print(f"Файл {BAR2_FILE} не найден!")
        return False

    try:
        # Открываем файл в режиме чтения+записи
        with open(BAR2_FILE, 'r+b') as f:

            # Переходим к регистру MAX_BUFFER_SIZE
            f.seek(REG_MAX_BUFFER_SIZE)
            # Записываем 4 байта (uint32, little endian)
            f.write(struct.pack('<I', DEFAULT_MAX_BUFFER))

            # Аналогично записываем остальные регистры
            f.seek(REG_BUFFER_SIZE)
            f.write(struct.pack('<I', DEFAULT_BUFFER_SIZE))
            
            f.seek(REG_BUFFER_ADDR)
            f.write(struct.pack('<I', DEFAULT_BUFFER_ADDR))
            
            # Обнуляем указатели
            f.seek(REG_WRITE_PTR)
            f.write(struct.pack('<I', 0))
            
            f.seek(REG_READ_PTR)
            f.write(struct.pack('<I', 0))
            
            # Устанавливаем статус 
            f.seek(REG_STATUS)
            f.write(struct.pack('<I', 1 << 0))
            
            print("Регистры инициализированы")
            return True
    
    except Exception as e:
        print(f"Ошибка: {e}")
        return False


# Запись сообщения в кольцевой буфер
def write_message_buffered(message: str):

    if not os.path.exists(BAR2_FILE):
        print(f"Файл {BAR2_FILE} не найден!")
        return
    
    try:
        # Кодируем строку в байты
        data = message.encode('utf-8')
        data_len = len(data)

        total_written = 0   # Сколько всего записано
        pos = 0             # Текущая позиция в сообщении
        wait_printed = False

        # Пока не записали всё сообщение
        while pos < data_len:
            # Каждый цикл заново открываем файл
            with open(BAR2_FILE, 'r+b') as f:

                # Читаем размер буфера
                f.seek(REG_BUFFER_SIZE)
                buffer_size = struct.unpack('<I', f.read(4))[0]
                
                # Читаем адрес буфера данных
                f.seek(REG_BUFFER_ADDR)
                buffer_addr = struct.unpack('<I', f.read(4))[0]
                
                # Читаем write_ptr
                f.seek(REG_WRITE_PTR)
                write_ptr = struct.unpack('<I', f.read(4))[0]
                
                # Читаем read_ptr
                f.seek(REG_READ_PTR)
                read_ptr = struct.unpack('<I', f.read(4))[0]
                
                # Вычисляем занятое место
                if write_ptr >= read_ptr:
                    used = write_ptr - read_ptr
                else:
                    # Обработка переполнения 32-битного счетчика
                    used = (0xFFFFFFFF - read_ptr) + write_ptr + 1
                
                # Свободное место
                free_space = buffer_size - used

                # Печать статуса 
                if not wait_printed:
                    print(f"Буфер: {buffer_size} байт, свободно: {free_space} байт")
                    print(f"Write ptr: {write_ptr}, Read ptr: {read_ptr}")
                
                # Если буфер заполнен
                if free_space == 0:
                    if not wait_printed:
                        print("Буфер полон! Ждем освобождения...")
                        wait_printed = True
                    
                    f.close()
                    time.sleep(0.2)
                    continue
                
                if wait_printed:
                    print(f"Освободилось {free_space} байт")
                    wait_printed = False
                
                # Сколько реально можем записать
                bytes_to_write = min(data_len - pos, free_space)

                # Берем кусок данных
                chunk = data[pos:pos + bytes_to_write]
                
                # Записываем ПОБАЙТОВО
                for i in range(bytes_to_write):
                    # Кольцевая адресация
                    write_pos = (write_ptr + i) % buffer_size
                    f.seek(buffer_addr + write_pos)
                    f.write(bytes([chunk[i]]))
                
                # Обновляем write_ptr (32-битное переполнение)
                write_ptr = (write_ptr + bytes_to_write) & 0xFFFFFFFF
                f.seek(REG_WRITE_PTR)
                f.write(struct.pack('<I', write_ptr))
                
                # Устанавливаем бит "данные доступны"
                f.seek(REG_STATUS)
                current_status = struct.unpack('<I', f.read(4))[0]
                f.seek(REG_STATUS)
                f.write(struct.pack('<I', current_status | (1 << 0)))
                
                print(f"Записано {bytes_to_write} байт, всего: {total_written + bytes_to_write}/{data_len}")
            
            # Обновляем глобальные счетчики
            total_written += bytes_to_write
            pos += bytes_to_write
            
            # Небольшая пауза при большой записи
            if bytes_to_write > buffer_size * 0.8:
                time.sleep(0.1)
        
        print(f"✓ Сообщение полностью записано ({total_written} байт)")
        
    except KeyboardInterrupt:
        print("\nПрервано пользователем")
    except Exception as e:
        print(f"Ошибка: {e}")
        import traceback
        traceback.print_exc()


def show_status():

    if not os.path.exists(BAR2_FILE):
        print(f"Файл {BAR2_FILE} не найден!")
        return
    
    try:
        with open(BAR2_FILE, 'rb') as f:

            print("=== BAR2 Status ===")

            # Чтение всех регистров
            f.seek(REG_MAX_BUFFER_SIZE)
            max_size = struct.unpack('<I', f.read(4))[0]
            print(f"Max buffer: {max_size}")
            
            f.seek(REG_BUFFER_SIZE)
            buf_size = struct.unpack('<I', f.read(4))[0]
            print(f"Buffer size: {buf_size}")
            
            f.seek(REG_BUFFER_ADDR)
            buf_addr = struct.unpack('<I', f.read(4))[0]
            print(f"Buffer addr: 0x{buf_addr:x}")
            
            f.seek(REG_WRITE_PTR)
            write_ptr = struct.unpack('<I', f.read(4))[0]
            print(f"Write pointer: {write_ptr} (0x{write_ptr:x})")
            
            f.seek(REG_READ_PTR)
            read_ptr = struct.unpack('<I', f.read(4))[0]
            print(f"Read pointer: {read_ptr} (0x{read_ptr:x})")
            
            f.seek(REG_STATUS)
            status = struct.unpack('<I', f.read(4))[0]
            print(f"Status: 0x{status:x}")
            
            # Вычисляем занятое место
            if write_ptr >= read_ptr:
                used = write_ptr - read_ptr
            else:
                used = (0xFFFFFFFF - read_ptr) + write_ptr + 1
            
            free = buf_size - used
            print(f"Использовано: {used} байт")
            print(f"Свободно: {free} байт")
            
            # Дамп первых 128 байт буфера
            print(f"\nДанные в буфере (первые 128 байт):")
            f.seek(buf_addr)
            buffer_data = f.read(min(128, buf_size))
            
            # Печать hex + ASCII как в hexdump
            for i in range(0, len(buffer_data), 16):
                hex_line = ' '.join(f'{buffer_data[j]:02x}'
                                    for j in range(i, min(i+16, len(buffer_data))))
                text_line = ''.join(chr(buffer_data[j]) if 32 <= buffer_data[j] < 127 else '.'
                                    for j in range(i, min(i+16, len(buffer_data))))
                print(f"  {buf_addr+i:04x}: {hex_line:<48}  {text_line}")
    
    except Exception as e:
        print(f"Ошибка: {e}")


def clear_buffer():

    if not os.path.exists(BAR2_FILE):
        print(f"Файл {BAR2_FILE} не найден!")
        return
    
    try:
        with open(BAR2_FILE, 'r+b') as f:

            # Читаем размер и адрес
            f.seek(REG_BUFFER_SIZE)
            buf_size = struct.unpack('<I', f.read(4))[0]
            
            f.seek(REG_BUFFER_ADDR)
            buf_addr = struct.unpack('<I', f.read(4))[0]
            
            # Сбрасываем указатели
            f.seek(REG_WRITE_PTR)
            f.write(struct.pack('<I', 0))
            
            f.seek(REG_READ_PTR)
            f.write(struct.pack('<I', 0))
            
            # Обнуляем память буфера
            f.seek(buf_addr)
            f.write(b'\x00' * buf_size)
            
            print(f"Буфер очищен: указатели сброшены, {buf_size} байт занулено")
    
    except Exception as e:
        print(f"Ошибка: {e}")


# Потоковая запись сообщений
def stream_messages(count: int, interval: float):

    print(f"Поток: {count} сообщений, интервал {interval} сек")
    
    for i in range(count):
        # Формируем сообщение со временем
        msg = f"[{i+1}/{count}] {time.strftime('%H:%M:%S')}"
        write_message_buffered(msg)
        
        if i < count - 1:
            time.sleep(interval)


# Тест переполнения
def test_overflow():

    print("=== Тест буферизации ===")
    
    buffer_size = 1024
    large_message = "A" * (buffer_size - 100)
    
    print(f"1. Заполняем {len(large_message)} байт из {buffer_size}")
    write_message_buffered(large_message)
    
    print(f"\n2. Пытаемся записать еще 200 байт...")
    
    extra_message = "B" * 200
    write_message_buffered(extra_message)
    
    print("\n3. Проверяем статус...")
    show_status()


def main():

    parser = argparse.ArgumentParser(description='BAR2 Manager')

    # Определяем поддерживаемые аргументы
    parser.add_argument('--init', action='store_true', help='Инициализировать регистры')
    parser.add_argument('--write', type=str, help='Записать сообщение')
    parser.add_argument('--status', action='store_true', help='Показать статус')
    parser.add_argument('--clear', action='store_true', help='Очистить буфер')
    parser.add_argument('--stream', action='store_true', help='Потоковый режим')
    parser.add_argument('--count', type=int, default=5, help='Количество сообщений')
    parser.add_argument('--interval', type=float, default=1.0, help='Интервал')
    parser.add_argument('--test-overflow', action='store_true', help='Тест буферизации')

    args = parser.parse_args()

    # Если аргументов нет — показать help
    if len(sys.argv) == 1:
        parser.print_help()
        return

    # Вызов соответствующих функций
    if args.init:
        init_registers()

    if args.status:
        show_status()

    if args.write:
        write_message_buffered(args.write)

    if args.clear:
        clear_buffer()

    if args.stream:
        stream_messages(args.count, args.interval)

    if args.test_overflow:
        test_overflow()


if __name__ == "__main__":
    main()
