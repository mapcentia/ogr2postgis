/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2023 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include <wx/wx.h>
#include "ogr2postgis.hpp"
#include <wx/listctrl.h>
#include <mutex>

using namespace ogr2postgis;

size_t i{0};

class App : public wxApp {
public:
    bool OnInit() override;
};

wxIMPLEMENT_APP(App);

class UpdateListEvent : public wxEvent {
public:
    UpdateListEvent(wxEventType eventType, int id)
            : wxEvent(id, eventType) {
    }

    // You *must* copy here the data to be transported
    UpdateListEvent(const UpdateListEvent &event)
            : wxEvent(event) { this->SetLayer(event.GetLayer()); }

    // Required for sending with wxPostEvent()
    wxEvent *Clone() const { return new UpdateListEvent(*this); }

    layer GetLayer() const { return m_layer; }

    void SetLayer(const layer &l) { m_layer = l; }

private:
    layer m_layer;
};

wxDEFINE_EVENT(UPDATE_LIST_TYPE, UpdateListEvent);

class Frame : public wxFrame {
public:
    Frame();

private:
    wxListCtrl *listCtrl; // Make listCtrl a member variable
    void OnExit(wxCommandEvent &event);

    void OnStart(wxCommandEvent &event);

    void OnOpen(UpdateListEvent &event);

    // Function to handle the size event
    void OnSize(wxSizeEvent &event) {
        std::cout << "Resize" << std::endl;
        // Resize the wxListCtrl to fill the parent
        listCtrl->SetSize(GetClientSize());
        event.Skip();
    }

    // Function to handle column click events for sorting
    void OnColumnClick(wxListEvent &event) {
        std::cout << "Click" << std::endl;
        int col = event.GetColumn();
        // You can implement your own sorting logic here based on the clicked column
        listCtrl->SortItems(&CompareFunction, col);
        // Update the list control
        listCtrl->Refresh();
    }

    // Comparison function for sorting
    static int CompareFunction(long item1, long item2, long col) {
        std::cout << "Compare" << std::endl;
        // Implement your own comparison logic here
        // You can retrieve item data and compare based on the specified column
//        wxString text1 = wxGetApp().GetTopWindow()->listCtrl->GetItemText(item1, col);
//        wxString text2 = wxGetApp().GetTopWindow()->listCtrl->GetItemText(item2, col);
//
//        return text1.CmpNoCase(text2); // Case-insensitive comparison for strings
    }
//DECLARE_EVENT_TABLE()
};

// Event table for Frame
//BEGIN_EVENT_TABLE(Frame, wxFrame)
//                EVT_SIZE(Frame::OnSize)
//END_EVENT_TABLE()

enum {
    ID_Start
};


bool App::OnInit() {
    Frame *frame = new Frame();
    frame->Show(true);
    return true;
}

Frame::Frame()
        : wxFrame(nullptr, wxID_ANY, "ogr2postgis") {
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(ID_Start, "Start",
                     "Help start");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    SetMenuBar(menuBar);

    wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

    listCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
    // Add columns to the list control
    listCtrl->InsertColumn(0, "Driver", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(1, "Count", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(2, "Type", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(3, "Layer no,", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(4, "Name", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(5, "Proj", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(6, "Auth", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(7, "File", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(8, "Error", wxLIST_FORMAT_LEFT, 100);

    // Enable column clicks for sorting
//    listCtrl->Bind(wxEVT_LIST_COL_CLICK, &Frame::OnColumnClick, this);

    Bind(wxEVT_SIZE, &Frame::OnSize, this);
    // Add the wxListCtrl to the main sizer
    mainSizer->Add(listCtrl, 1, wxEXPAND | wxALL, 5);
    // Set the main sizer for the frame
    SetSizerAndFit(mainSizer);

    Bind(wxEVT_MENU, &Frame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &Frame::OnStart, this, ID_Start);
    Bind(UPDATE_LIST_TYPE, &Frame::OnOpen, this, wxID_ANY);
}

void Frame::OnOpen(UpdateListEvent &event) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        i++;
        // Unlock automatically when 'lock' goes out of scope
    }
    layer l = event.GetLayer();
    long index = listCtrl->InsertItem(i, l.driverName);
    listCtrl->SetItem(index, 1, to_string(l.featureCount));
    listCtrl->SetItem(index, 2, l.type + (l.singleMultiMixed ? "(m)" : ""));
    listCtrl->SetItem(index, 3, to_string(l.layerIndex));
    listCtrl->SetItem(index, 4, l.layerName);
    listCtrl->SetItem(index, 5, l.hasWkt);
    listCtrl->SetItem(index, 6, l.authStr);
    listCtrl->SetItem(index, 7, l.file);
    listCtrl->SetItem(index, 8, l.error);
}

void Frame::OnStart(wxCommandEvent &event) {
    listCtrl->DeleteAllItems();
    i = 0;
    std::thread([this]() {
        config config;
        auto lCallback1 = [this](vector<string> fileNames) {
        };
        auto lCallback2 = [this](layer l) {
            UpdateListEvent event(UPDATE_LIST_TYPE, wxID_ANY);
            event.SetLayer(l);
            wxPostEvent(this, event);
        };
        auto lCallback3 = [](vector<struct layer> layers) {
        };
        auto lCallback4 = [](layer l) {
        };
        vector<struct layer> layers = start(config, "/home/mh/Documents/Backup/mh/Data", lCallback1, lCallback2,
                                            lCallback3,
                                            lCallback4);
    }).detach();

}

void Frame::OnExit(wxCommandEvent &event) {
    Close(true);
}