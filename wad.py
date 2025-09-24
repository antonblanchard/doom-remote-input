#!/usr/bin/python3

import struct
import os

class Wad:
    def __init__(self, filename):
        self.lumps = dict()
        with open(filename, 'rb') as f:
            # Read and Parse the WAD Header (12 bytes)
            # The header contains the WAD type, number of lumps, and the
            # location of the directory (the "infotable").
            header_data = f.read(12)
            if len(header_data) < 12:
                raise ValueError("Invalid WAD file: Header is too short.")

            # Unpack the header using struct.
            # '<' specifies little-endian byte order.
            # '4s' is a 4-byte string (the WAD type).
            # 'i' is a 4-byte signed integer.
            wad_type, num_lumps, directory_offset = struct.unpack('<4sii', header_data)

            # Validate Header 
            if wad_type not in (b'IWAD', b'PWAD'):
                raise ValueError(f"Not a valid WAD file. Identification is {wad_type!r}.")

            # Iterate through all wads
            for i in range(num_lumps):
                # Locate and Read the Directory Entry (16 bytes) ---
                # Each entry in the directory describes one lump.
                entry_size = 16
                entry_offset = directory_offset + (i * entry_size)
                f.seek(entry_offset)

                entry_data = f.read(entry_size)
                if len(entry_data) < entry_size:
                    raise ValueError("Invalid WAD file: Directory entry is incomplete.")

                # Unpack the directory entry to get the lump's location and size.
                # '8s' is an 8-byte string (the lump's name).
                lump_offset, lump_size, lump_name_raw = struct.unpack('<ii8s', entry_data)
            
                # Clean the lump name by removing null padding and decoding it.
                lump_name = lump_name_raw.strip(b'\x00').decode('ascii', errors='ignore')

                # Seek to and Read the Lump Data
                f.seek(lump_offset)
                lump_data = f.read(lump_size)

                if len(lump_data) < lump_size:
                     raise ValueError(
                         f"Incomplete lump data for '{lump_name}'. "
                         f"Expected {lump_size} bytes, got {len(lump_data)}."
                         )
                self.lumps[i] = lump_data


if __name__ == '__main__':
    wad_filepath = 'DOOM.WAD'
    w = Wad(wad_filepath)
    print(w.lumps[576])
