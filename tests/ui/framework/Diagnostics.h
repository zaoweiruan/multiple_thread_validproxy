// tests/ui/framework/Diagnostics.h - failure artifact collection.
#pragma once
#include <windows.h>
#include <string>

namespace uitest {

// Ensures the artifacts directory exists (test-results/ui-artifacts).
void ensureArtifactsDir();

// Saves window_<tag>.png + tree_<tag>.txt under the artifacts dir.
// Returns the artifact base path (without extension) or empty on failure.
std::wstring saveArtifacts(HWND hwnd, const std::wstring& tag);

}
