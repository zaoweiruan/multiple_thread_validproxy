// Minimal test: wxFrame with SetMenuBar
#include <wx/wx.h>

class TestApp : public wxApp {
public:
    bool OnInit() override {
        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "Menu Test", wxDefaultPosition, wxSize(400, 300));
        
        wxMenuBar* bar = new wxMenuBar();
        wxMenu* fileMenu = new wxMenu();
        fileMenu->Append(wxID_EXIT, "Exit\tAlt+F4");
        bar->Append(fileMenu, "&File");
        
        frame->SetMenuBar(bar);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(TestApp);
