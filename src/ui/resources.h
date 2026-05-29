#ifndef UI_RESOURCES_H
#define UI_RESOURCES_H

// ICO file data embedded as arrays
// Generated from docs/design/ui/icon.ico and clear.ico

// icon.ico - 16x16 application icon (ICO format)
// This is a minimal ICO file structure header + PNG data
// For simplicity, we'll load from file path in bin/ after build

namespace IconResources {
    // ICO files will be copied to bin/docs/design/ui/ by CMakeLists.txt
    // and loaded at runtime from the executable directory
}

#endif // UI_RESOURCES_H