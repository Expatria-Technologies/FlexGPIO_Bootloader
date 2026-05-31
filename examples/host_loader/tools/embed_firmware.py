import sys

def main():
    bin_path = sys.argv[1]
    out_path = sys.argv[2]

    with open(bin_path, "rb") as f:
        data = f.read()

    with open(out_path, "w") as f:
        f.write("// Auto-generated — do not edit\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"const uint8_t firmware_bin[{len(data)}] = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")
        f.write(f"const uint32_t firmware_bin_len = {len(data)};\n")

    print(f"Embedded {len(data)} bytes into {out_path}")

if __name__ == "__main__":
    main()
