"""生成中文字体子集需要的字符表：ASCII + 常用标点 + GB2312 一级汉字 + 源码里用到的所有字符。

用法: python3 font_chars.py <源码目录> <输出文件>
"""
import pathlib
import sys

src_dir = pathlib.Path(sys.argv[1])
out_file = pathlib.Path(sys.argv[2])

chars = set(chr(c) for c in range(0x20, 0x7F))
chars.update("，。、；：？！“”‘’（）《》【】—…·￥％＋－×÷→←↑↓●○■□★☆")

# GB2312 一级汉字（3755 个常用字），区位 16-55 区
for hi in range(0xB0, 0xD8):
    for lo in range(0xA1, 0xFF):
        try:
            chars.add(bytes([hi, lo]).decode("gb2312"))
        except UnicodeDecodeError:
            pass

for path in src_dir.rglob("*"):
    if path.suffix in {".cpp", ".h", ".qml"}:
        chars.update(path.read_text(encoding="utf-8"))

chars = sorted(c for c in chars if c.isprintable())
out_file.write_text("".join(chars), encoding="utf-8")
print(f"{len(chars)} chars -> {out_file}")
