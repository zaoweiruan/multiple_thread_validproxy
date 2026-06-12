// Minimal wxWidgets app with menu
#include <wx/wx.h>

class MyApp : public wxApp {
public:
    bool OnInit() override {
        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "Test", wxDefaultPosition, wxSize(400, 300));
        
        wxMenuBar* bar = new wxMenuBar();
        wxMenu* menu = new wxMenu();
        menu->Append(wxID_EXIT, "Exit");
        bar->Append(menu, "&File");
        
        frame->SetMenuBar(bar);
        frame->Show(true);
        // Immediately close to allow test exit
        frame->Close(true);
        return false; // Prevent entering main loop
    }
};

wxIMPLEMENT_APP(MyApp);
