from PIL import Image
from pathlib import Path
import sys
import glob


def png_to_ico(png_path, ico_path=None):

    try:
        png_path = Path(png_path).expanduser().resolve()

        if not png_path.exists():
            print(f"[错误] PNG 文件不存在: {png_path}")
            return False

        if png_path.suffix.lower() != ".png":
            print(f"[错误] 不是 PNG 文件: {png_path}")
            return False

        # 自动生成 ico 文件名
        if ico_path is None:
            ico_path = png_path.with_suffix(".ico")
        else:
            ico_path = Path(ico_path).expanduser().resolve()

        # 自动创建目录
        ico_path.parent.mkdir(parents=True, exist_ok=True)

        # 打开 PNG
        img = Image.open(png_path)

        # Windows 标准图标尺寸
        icon_sizes = [
            (16, 16),
            (24, 24),
            (32, 32),
            (48, 48),
            (64, 64),
            (128, 128),
            (256, 256)
        ]

        # 保存 ICO
        img.save(
            ico_path,
            format="ICO",
            sizes=icon_sizes
        )

        print(f"[成功] {png_path.name} -> {ico_path.name}")
        return True

    except Exception as e:
        print(f"[异常] 转换失败: {e}")
        return False


def expand_patterns(args):
    """
    展开通配符
    支持:
        *.png
        icon/*.png
        **/*.png
    """

    files = []

    for pattern in args:

        # recursive=True 支持 **
        matched = glob.glob(pattern, recursive=True)

        if not matched:
            print(f"[警告] 未匹配到文件: {pattern}")
            continue

        files.extend(matched)

    # 去重
    return list(dict.fromkeys(files))


if __name__ == "__main__":

    if len(sys.argv) < 2:
        print("用法:")
        print("    py pngtoico.py *.png")
        print("    py pngtoico.py icon/*.png")
        print("    py pngtoico.py **/*.png")
        sys.exit(1)

    # 展开通配符
    png_files = expand_patterns(sys.argv[1:])

    if not png_files:
        print("[错误] 没有找到任何 PNG 文件")
        sys.exit(1)

    success = 0

    for file in png_files:
        if png_to_ico(file):
            success += 1

    print()
    print(f"转换完成: {success}/{len(png_files)}")