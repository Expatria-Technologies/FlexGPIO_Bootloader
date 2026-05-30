import os

Import("env")

project_dir = env.subst("$PROJECT_DIR")
bin_path = os.path.join(project_dir, os.pardir, os.pardir,
                        "FlexGPIO", "build", "FlexGPIO_ram.bin")

if not os.path.exists(bin_path):
    print("WARNING: flexgpio_ram.bin not found at", bin_path)
else:
    with open(bin_path, "rb") as f:
        data = f.read()

    c_path = os.path.join(env.subst("$PROJECTSRC_DIR"), "flexgpio_ram.c")
    with open(c_path, "w") as f:
        f.write("// Auto-generated — do not edit\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"const uint8_t flexgpio_ram_bin[{len(data)}] = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")
        f.write(f"const uint32_t flexgpio_ram_bin_len = {len(data)};\n")
        print(f"Generated {c_path} ({len(data)} bytes)")
