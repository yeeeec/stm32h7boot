import struct
import shutil

def stm32_crc32_manual(data):
    crc = 0xFFFFFFFF 
    poly = 0x04C11DB7 

    for i in range(0, len(data), 4):
        
        word = int.from_bytes(data[i:i+4], byteorder='little', signed=False)

        crc ^= word

        for _ in range(32):
            if crc & 0x80000000:
                crc = (crc << 1) ^ poly
            else:
                crc <<= 1
            crc &= 0xFFFFFFFF 

    return crc

def append_crc_to_bin(bin_file, output_file):

    shutil.copy(bin_file, output_file)

    with open(output_file, 'rb+') as f:
        data = f.read()

        initial_crc32 = stm32_crc32_manual(data)
        # print(f"Initial CRC32: {hex(initial_crc32)}")

        check_id = "Like"
        check_id_value = struct.unpack('<I', check_id.encode('utf-8'))[0]
        final_crc32 = ~(initial_crc32 ^ check_id_value) & 0xFFFFFFFF  # 先异或再取反
        # print(f"Final CRC32 after XOR and inversion: {hex(final_crc32)}")

        crc_bytes = final_crc32.to_bytes(4, 'little')  # 将CRC32值转换为4字节的小端格式

        # 将CRC附加到新文件末尾
        f.write(crc_bytes)

    print(f"CRC32 {hex(final_crc32)} has been appended to {output_file}")

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("Usage: python append_crc.py <path_to_bin_file> <output_bin_file>")
    else:
        append_crc_to_bin(sys.argv[1], sys.argv[2])
