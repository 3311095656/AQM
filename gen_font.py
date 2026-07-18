from PIL import Image, ImageDraw, ImageFont
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(SCRIPT_DIR, "libraries", "Board_Drivers", "lcd")

CN_CHARS = "室内温湿度浓一二氧化碳天然气煤空调加除机开关外有害安全高低正常℃光照强度燃人无"

FONT_SEARCH_PATHS = [
    "C:/Windows/Fonts/simhei.ttf",
    "C:/Windows/Fonts/simsun.ttc",
    "C:/Windows/Fonts/msyh.ttc",
]

RESOLUTIONS = [
    {"size": 16, "ascii_var": "asc2_1608", "cn_var": "cn_font_1616",
     "cn_macro": "CN_FONT_1616", "ascii_macro": "ASC2_1608",
     "header": "drv_lcd_font_16.h", "guard": "__DRV_LCD_FONT_16_H__"},
    {"size": 24, "ascii_var": "asc2_2412", "cn_var": "cn_font_2424",
     "cn_macro": "CN_FONT_2424", "ascii_macro": "ASC2_2412",
     "header": "drv_lcd_font_24.h", "guard": "__DRV_LCD_FONT_24_H__"},
    {"size": 32, "ascii_var": "asc2_3216", "cn_var": "cn_font_3232",
     "cn_macro": "CN_FONT_3232", "ascii_macro": "ASC2_3216",
     "header": "drv_lcd_font_32.h", "guard": "__DRV_LCD_FONT_32_H__"},
]


def load_font(size):
    for fp in FONT_SEARCH_PATHS:
        if os.path.exists(fp):
            try:
                font = ImageFont.truetype(fp, size)
                print(f"  Loaded font: {fp} (size={size})")
                return font
            except Exception:
                continue
    return None


def char_to_bitmap(ch, font, size):
    img = Image.new('1', (size, size), 0)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), ch, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    x = (size - tw) // 2 - bbox[0]
    y = (size - th) // 2 - bbox[1]
    draw.text((x, y), ch, fill=1, font=font)

    bitmap = []
    for row in range(size):
        row_bytes = []
        byte_val = 0
        bit_count = 0
        for col in range(size):
            pixel = img.getpixel((col, row))
            byte_val = (byte_val << 1) | (1 if pixel else 0)
            bit_count += 1
            if bit_count == 8:
                row_bytes.append(byte_val)
                byte_val = 0
                bit_count = 0
        if bit_count > 0:
            byte_val = byte_val << (8 - bit_count)
            row_bytes.append(byte_val)
        bitmap.extend(row_bytes)
    return bitmap


def generate_cn_font_data(chars, font, size, var_name, macro_name):
    bytes_per_char = (size * size) // 8
    all_data = []
    lines = []
    lines.append(f"#define {macro_name}")
    lines.append(f"#ifdef {macro_name}")
    lines.append(f"/* Chinese font {size}x{size} */")
    lines.append(f"/* Characters: {' '.join(chars)} */")
    for i, ch in enumerate(chars):
        code = hex(ord(ch))
        lines.append(f"/* \"{ch}\" {code} offset:{i * bytes_per_char} */")
    lines.append(f"const uint8_t {var_name}[{len(chars) * bytes_per_char}]={{")
    for i, ch in enumerate(chars):
        bm = char_to_bitmap(ch, font, size)
        all_data.extend(bm)
        hex_str = ",".join(f"0x{b:02X}" for b in bm)
        lines.append(f"{hex_str},/*\"{ch}\",{i}*/")
    lines.append("};")
    lines.append("#endif")
    return "\n".join(lines), len(all_data), bytes_per_char


def generate_cn_index_array(chars):
    entries = []
    for i in range(0, len(chars), 8):
        chunk = chars[i:i+8]
        hex_vals = ", ".join(f"0x{ord(ch):04X}" for ch in chunk)
        entries.append(f"    {hex_vals},")
    lines = ["static const uint16_t cn_font_index[] = {"]
    lines.extend(entries)
    lines.append("};")
    lines.append("#define CN_FONT_CHAR_NUM (sizeof(cn_font_index) / sizeof(cn_font_index[0]))")
    return "\n".join(lines)


def generate_header(res, cn_data_str):
    lines = []
    lines.append(f"#ifndef {res['guard']}")
    lines.append(f"#define {res['guard']}")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"/* {res['size']}-pixel resolution font data */")
    lines.append(f"/* ASCII: {res['size']}x{res['size']//2}, Chinese: {res['size']}x{res['size']} */")
    lines.append("")
    lines.append(f"#define {res['ascii_macro']}")
    lines.append("")
    lines.append(f"#ifdef {res['ascii_macro']}")
    lines.append(f"const uint8_t {res['ascii_var']}[]={{")
    lines.append("/* ASCII font data - generated separately, keep existing data */")
    lines.append("/* This placeholder should be replaced with actual ASCII bitmap data */")
    lines.append("};")
    lines.append("#endif")
    lines.append("")
    lines.append(cn_data_str)
    lines.append("")
    lines.append("#endif")
    return "\n".join(lines)


def generate_cn_only_header(res, cn_data_str):
    lines = []
    lines.append(f"#ifndef {res['guard']}")
    lines.append(f"#define {res['guard']}")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"/* {res['size']}-pixel resolution font data */")
    lines.append(f"/* ASCII: {res['size']}x{res['size']//2}, Chinese: {res['size']}x{res['size']} */")
    lines.append("")
    lines.append(cn_data_str)
    lines.append("")
    lines.append("#endif")
    return "\n".join(lines)


def cmd_generate(args):
    print("=== Generating all Chinese font data ===\n")

    chars = CN_CHARS
    if args and len(args) > 0:
        chars = args[0]

    print(f"Characters ({len(chars)}): {chars}\n")

    for res in RESOLUTIONS:
        print(f"--- {res['size']}x{res['size']} ---")
        font = load_font(res["size"])
        if font is None:
            print(f"  ERROR: No font found for size {res['size']}")
            continue

        cn_data_str, total_bytes, bpc = generate_cn_font_data(
            chars, font, res["size"], res["cn_var"], res["cn_macro"]
        )
        print(f"  {res['cn_var']}: {total_bytes} bytes ({bpc} bytes/char, {len(chars)} chars)")

        header_path = os.path.join(FONT_DIR, res["header"])

        if os.path.exists(header_path):
            with open(header_path, "r", encoding="utf-8") as f:
                content = f.read()

            ascii_section = _extract_ascii_section(content, res)

            lines = []
            lines.append(f"#ifndef {res['guard']}")
            lines.append(f"#define {res['guard']}")
            lines.append("")
            lines.append("#include <stdint.h>")
            lines.append("")
            lines.append(f"/* {res['size']}-pixel resolution font data */")
            lines.append(f"/* ASCII: {res['size']}x{res['size']//2}, Chinese: {res['size']}x{res['size']} */")
            lines.append("")
            if ascii_section:
                lines.append(ascii_section)
                lines.append("")
            lines.append(cn_data_str)
            lines.append("")
            lines.append("#endif")
            new_content = "\n".join(lines)
        else:
            new_content = generate_cn_only_header(res, cn_data_str)

        with open(header_path, "w", encoding="utf-8") as f:
            f.write(new_content)
        print(f"  Written: {header_path}")

    print(f"\n--- cn_font_index for drv_lcd.c ---")
    print(generate_cn_index_array(chars))
    print("\nDone!")


def _extract_ascii_section(content, res):
    macro = res["ascii_macro"]
    if f"#ifdef {macro}" not in content:
        return None

    start_idx = content.find(f"#define {macro}")
    if start_idx < 0:
        start_idx = content.find(f"#ifdef {macro}")
    if start_idx < 0:
        return None

    end_marker = "#endif"
    depth = 0
    search_from = start_idx
    while True:
        ifdef_pos = content.find("#ifdef", search_from)
        endif_pos = content.find(end_marker, search_from)
        if endif_pos < 0:
            break

        if ifdef_pos >= 0 and ifdef_pos < endif_pos:
            depth += 1
            search_from = ifdef_pos + len("#ifdef")
        else:
            if depth == 0:
                section = content[start_idx:endif_pos + len(end_marker)]
                return section
            depth -= 1
            search_from = endif_pos + len(end_marker)

    return None


def cmd_add(args):
    if not args:
        print("Usage: python gen_font.py add <characters>")
        print("Example: python gen_font.py add 新字")
        return

    new_chars = args[0]
    existing_chars = CN_CHARS

    combined = existing_chars
    for ch in new_chars:
        if ch not in combined:
            combined += ch

    print(f"Existing characters: {existing_chars} ({len(existing_chars)})")
    print(f"New characters to add: {new_chars}")
    print(f"Combined characters: {combined} ({len(combined)})")

    cmd_generate([combined])


def cmd_index(args):
    chars = CN_CHARS
    if args and len(args) > 0:
        chars = args[0]
    print(generate_cn_index_array(chars))


def cmd_help():
    print("Font Generation Tool for project_aqm")
    print("")
    print("Usage: python gen_font.py <command> [arguments]")
    print("")
    print("Commands:")
    print("  generate [chars]  Generate all font header files (default: use CN_CHARS)")
    print("  add <chars>       Add new characters to existing font data")
    print("  index [chars]     Print cn_font_index array for drv_lcd.c")
    print("  help              Show this help message")
    print("")
    print("Examples:")
    print("  python gen_font.py generate")
    print('  python gen_font.py generate "室内温湿度"')
    print('  python gen_font.py add "新字库"')
    print('  python gen_font.py index')


def main():
    if len(sys.argv) < 2:
        cmd_help()
        return

    command = sys.argv[1].lower()
    args = sys.argv[2:]

    if command == "generate" or command == "gen":
        cmd_generate(args)
    elif command == "add":
        cmd_add(args)
    elif command == "index":
        cmd_index(args)
    elif command == "help":
        cmd_help()
    else:
        print(f"Unknown command: {command}")
        cmd_help()


if __name__ == "__main__":
    main()
