// tests/test_toolbar_region_icon.cpp - V11.1 (spec 2026-11-11 §7):
// 「解析地区」工具栏图标的文件级硬约束回归：
//   R-a 去背景后必须带真实 alpha 通道（PNG color type 6 = RGBA）
//   R-b 输出必须 1:1 正方形（width == height）
//   R-c 尺寸 sanity（16..2048），防呆
// 纯文件 I/O 解析 PNG IHDR，不依赖 wx/GDI+；工作目录 = 仓库根
// （CMake WORKING_DIRECTORY），锚定 docs/design/ui/icon/png/parsingregion.png
// （git 跟踪源；bin/icons/ 为其运行时副本，构建后逐字节一致由处理脚本保证）。
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace {

unsigned int Be32(const unsigned char* p) {
    return (static_cast<unsigned int>(p[0]) << 24) |
           (static_cast<unsigned int>(p[1]) << 16) |
           (static_cast<unsigned int>(p[2]) << 8) |
           static_cast<unsigned int>(p[3]);
}

struct IconFile {
    std::vector<char> bytes;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned char bitDepth = 0;
    unsigned char colorType = 0;  // 6 = RGBA (truecolor + alpha)
    bool ok = false;
};

IconFile ReadIcon(const char* path) {
    IconFile f;
    std::ifstream in(path, std::ios::binary);
    if (!in) return f;
    f.bytes.assign(std::istreambuf_iterator<char>(in),
                   std::istreambuf_iterator<char>());
    // PNG signature (8) + chunk length (4) + "IHDR" (4); IHDR payload at [16..]
    static const unsigned char kSig[8] = {0x89, 0x50, 0x4E, 0x47,
                                          0x0D, 0x0A, 0x1A, 0x0A};
    if (f.bytes.size() < 8 + 4 + 4 + 13) return f;
    for (int i = 0; i < 8; ++i) {
        if (static_cast<unsigned char>(f.bytes[i]) != kSig[i]) return f;
    }
    if (f.bytes[12] != 'I' || f.bytes[13] != 'H' ||
        f.bytes[14] != 'D' || f.bytes[15] != 'R') return f;
    f.width = Be32(reinterpret_cast<const unsigned char*>(&f.bytes[16]));
    f.height = Be32(reinterpret_cast<const unsigned char*>(&f.bytes[20]));
    f.bitDepth = static_cast<unsigned char>(f.bytes[24]);
    f.colorType = static_cast<unsigned char>(f.bytes[25]);
    f.ok = true;
    return f;
}

} // namespace

class ToolbarRegionIconTest : public ::testing::Test {
protected:
    void SetUp() override {
        icon_ = ReadIcon("docs/design/ui/icon/png/parsingregion.png");
    }
    IconFile icon_;
};

TEST_F(ToolbarRegionIconTest, PngSignatureAndIhdr) {
    ASSERT_TRUE(icon_.ok) << "docs/design/ui/icon/png/parsingregion.png unreadable / not a PNG";
}

TEST_F(ToolbarRegionIconTest, IsSquareOneToOne) {
    ASSERT_TRUE(icon_.ok);
    EXPECT_EQ(icon_.width, icon_.height)
        << "toolbar icon must be 1:1 after background removal";
}

TEST_F(ToolbarRegionIconTest, HasAlphaChannelFromBackgroundRemoval) {
    ASSERT_TRUE(icon_.ok);
    EXPECT_EQ(icon_.bitDepth, 8);
    EXPECT_EQ(icon_.colorType, 6)
        << "color type 6 = RGBA; background removal must leave a real alpha channel";
}

TEST_F(ToolbarRegionIconTest, SizeSanity) {
    ASSERT_TRUE(icon_.ok);
    EXPECT_GE(icon_.width, 16);
    EXPECT_LE(icon_.width, 2048);
}
