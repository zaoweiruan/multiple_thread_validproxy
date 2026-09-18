// Unit tests for ProcessInspector standalone-config command-line extraction.
// Covers both the existing bare-name extraction and the new full-path fallback
// used to resolve the listening port of proxies launched by external programs
// from a non-standard config directory.

#include <gtest/gtest.h>

#include <string>

#include "ProcessInspector.h"

using proc::ProcessInspector;

// ---------------------------------------------------------------------------
// extractConfigFileName (bare name, existing behavior)
// ---------------------------------------------------------------------------

TEST(ProcessInspectorTest, ExtractConfigFileName_QuotedFullPath) {
    // The full path is passed on the command line; only the bare name is returned.
    std::wstring cmd =
        L"\"C:\\exe\\xray.exe\" run -c \"C:\\exe\\config\\standalone_abc123-xray.json\"";
    EXPECT_EQ(ProcessInspector::extractConfigFileName(cmd),
              "standalone_abc123-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFileName_SingboxBare) {
    std::wstring cmd = L"sing-box run -c standalone_myid-singbox.json";
    EXPECT_EQ(ProcessInspector::extractConfigFileName(cmd),
              "standalone_myid-singbox.json");
}

TEST(ProcessInspectorTest, ExtractConfigFileName_None) {
    std::wstring cmd = L"xray.exe run";
    EXPECT_EQ(ProcessInspector::extractConfigFileName(cmd), "");
}

// ---------------------------------------------------------------------------
// extractConfigFullPath (full path, new fallback)
// ---------------------------------------------------------------------------

TEST(ProcessInspectorTest, ExtractConfigFullPath_AbsoluteQuoted) {
    std::wstring cmd =
        L"\"D:\\other\\xray.exe\" -c \"D:\\other\\standalone_abc-xray.json\"";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "D:\\other\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_AbsoluteUnquoted) {
    std::wstring cmd = L"xray.exe -c D:\\other\\standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "D:\\other\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_Relative) {
    std::wstring cmd = L"xray.exe -c config\\standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "config\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_AssignmentWithEquals) {
    std::wstring cmd = L"xray.exe config=D:\\other\\standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd),
              "D:\\other\\standalone_abc-xray.json");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_BareNameReturnsEmpty) {
    // No directory component -> caller's directory probe handles it.
    std::wstring cmd = L"xray.exe -c standalone_abc-xray.json";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd), "");
}

TEST(ProcessInspectorTest, ExtractConfigFullPath_None) {
    std::wstring cmd = L"xray.exe run";
    EXPECT_EQ(ProcessInspector::extractConfigFullPath(cmd), "");
}
