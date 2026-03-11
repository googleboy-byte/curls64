import struct
import os

def make_initrd():
    # Files to include: (name in initrd, path on disk or literal bytes)
    file_specs = [
        ("PROGRAM.BIN", b"\xBB\x13\x00\x00\x00\xB8\x00\x00\x00\x00\xCD\x80\xB8\x01\x00\x00\x00\xCD\x80Hello Binary!\n\0"),
        ("LOOP.BIN", 
         b"\xBB\x23\x00\x00\x00"  # mov ebx, 0x23 (string offset, will be patched)
         b"\xB8\x00\x00\x00\x00"  # mov eax, 0 (SYS_PRINT)
         b"\xCD\x80"              # int 0x80
         b"\xBA\x08\x00\x00\x00"  # mov edx, 0x08 (8 outer loops)
         b"\xB9\x00\x00\x00\x02"  # mov ecx, 0x02000000 (~32M inner)
         b"\x49"                  # dec ecx
         b"\x75\xFD"              # jnz inner_loop (-3 bytes)
         b"\x4A"                  # dec edx
         b"\x75\xF5"              # jnz outer_loop (-11 bytes)
         b"\xB8\x01\x00\x00\x00"  # mov eax, 1 (SYS_EXIT)
         b"\xCD\x80"              # int 0x80
         b"Long running...\n\0"),
        ("HELLO.ELF", "user/hello/hello.elf"),
        ("ARGTEST.ELF", "user/argtest/argtest.elf"),
    ]
    
    files = []
    for name, spec in file_specs:
        if isinstance(spec, str):
            with open(spec, "rb") as f:
                data = f.read()
            files.append((name, data))
        else:
            files.append((name, spec))

    nfiles = len(files)
    # Header: number of files (4 bytes)
    header = struct.pack("<I", nfiles)
    
    file_headers = b""
    file_data = b""
    
    # Calculate initial offset for the first file
    # offset = sizeof(header) + nfiles * sizeof(file_header)
    # Each file header: magic(1), name(64), offset(4), length(4) = 73 bytes
    current_offset = 4 + nfiles * 73
    
    for name, data in files:
        fname = name.ljust(64, '\x00').encode('ascii')
        file_headers += struct.pack("<B64sII", 0xBF, fname, current_offset, len(data))
        file_data += data
        current_offset += len(data)
        
    initrd_path = os.path.join(os.path.dirname(__file__), "initrd.bin")
    with open(initrd_path, "wb") as f:
        f.write(header)
        f.write(file_headers)
        f.write(file_data)
    
    print(f"initrd.bin created successfully with {nfiles} files.")

if __name__ == "__main__":
    make_initrd()
