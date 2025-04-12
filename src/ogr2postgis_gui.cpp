/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2025 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include <wx/wx.h>
#include "ogr2postgis.hpp"
#include <wx/listctrl.h>
#include <mutex>
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <thread>

using namespace ogr2postgis;

size_t i{0};

class App : public wxApp {
public:
    bool OnInit() override;
};

wxIMPLEMENT_APP(App);

class UpdateListEvent final : public wxEvent {
public:
    UpdateListEvent(wxEventType eventType, int id)
        : wxEvent(id, eventType), m_layer() {
    }

    // You *must* copy here the data to be transported
    UpdateListEvent(const UpdateListEvent &event)
        : wxEvent(event) { this->SetLayer(event.GetLayer()); }

    // Required for sending with wxPostEvent()
    [[nodiscard]] wxEvent *Clone() const override { return new UpdateListEvent(*this); }

    [[nodiscard]] layer GetLayer() const { return m_layer; }

    void SetLayer(const layer &l) { m_layer = l; }

private:
    layer m_layer;
};

wxDEFINE_EVENT(UPDATE_LIST_TYPE, UpdateListEvent);
wxDEFINE_EVENT(PROGRESS_UPDATE_EVENT, wxThreadEvent);


class Frame final : public wxFrame {
public:
    Frame();

private:
    wxListCtrl *listCtrl; // Make listCtrl a member variable
    wxGauge *progressGauge; // progress bar control
    wxStaticText *progressText; // text label for counter (n/total)
    int totalFiles = 0; // total number of files found (set in lCallback1)
    std::mutex mtx; // mutex for protecting the counter
    size_t processedCount = 0; // count of processed files

    void OnExit(wxCommandEvent &event);

    void OnStart(wxCommandEvent &event);

    void OnOpen(const UpdateListEvent &event);

    void OnProgressUpdate(wxThreadEvent &event);

    // Function to handle the size event
    void OnSize(wxSizeEvent &event) {
        std::cout << "Resize" << std::endl;
        // Resize the wxListCtrl to fill the parent
        listCtrl->SetSize(GetClientSize());
        event.Skip();
    }

    // Function to handle column click events for sorting
    void OnColumnClick(const wxListEvent &event) {
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
    auto *frame = new Frame();
    frame->Show(true);
    return true;
}


Frame::Frame()
    : wxFrame(nullptr, wxID_ANY, "ogr2postgis") {
    auto *menuFile = new wxMenu;
    menuFile->Append(ID_Start, "Start", "Help start");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    auto *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    SetMenuBar(menuBar);

    auto *mainSizer = new wxBoxSizer(wxVERTICAL);

    std::vector<struct layer> layers;

    // Create the list control and add columns
    listCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
    listCtrl->InsertColumn(0, "Driver", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(1, "Count", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(2, "Type", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(3, "Layer no,", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(4, "Name", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(5, "Proj", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(6, "Auth", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(7, "File", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(8, "Error", wxLIST_FORMAT_LEFT, 100);
    mainSizer->Add(listCtrl, 1, wxEXPAND | wxALL, 5);

    // Create a progress gauge (progress bar)
    progressGauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL);
    mainSizer->Add(progressGauge, 0, wxEXPAND | wxALL, 5);

    // Create a static text to show progress (e.g., "0/0")
    progressText = new wxStaticText(this, wxID_ANY, "0/0");
    mainSizer->Add(progressText, 0, wxALIGN_CENTER | wxALL, 5);

    SetSizerAndFit(mainSizer);


    // Bind events for menu, resize, and custom update events
    Bind(wxEVT_SIZE, &Frame::OnSize, this);
    Bind(wxEVT_MENU, &Frame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &Frame::OnStart, this, ID_Start);
    Bind(UPDATE_LIST_TYPE, &Frame::OnOpen, this, wxID_ANY);
    Bind(PROGRESS_UPDATE_EVENT, &Frame::OnProgressUpdate, this, wxID_ANY);
}

void Frame::OnProgressUpdate(wxThreadEvent &event) {
    const int newValue = event.GetInt();
    progressGauge->SetValue(newValue);
    progressText->SetLabel(wxString::Format("%d/%d", newValue, totalFiles));
}

void Frame::OnOpen(const UpdateListEvent &event) { {
        std::lock_guard<std::mutex> lock(mtx);
        i++;
        // Unlock automatically when 'lock' goes out of scope
    }
    layer l = event.GetLayer();
    const long index = listCtrl->InsertItem(i, l.driverName);
    listCtrl->SetItem(index, 1, std::to_string(l.featureCount));
    listCtrl->SetItem(index, 2, l.type + (l.singleMultiMixed ? "(m)" : ""));
    listCtrl->SetItem(index, 3, std::to_string(l.layerIndex));
    listCtrl->SetItem(index, 4, l.layerName);
    listCtrl->SetItem(index, 5, l.hasWkt);
    listCtrl->SetItem(index, 6, l.authStr);
    listCtrl->SetItem(index, 7, l.file);
    listCtrl->SetItem(index, 8, l.error);
}

void Frame::OnStart(wxCommandEvent &event) {
    // Clear the list control and reset the global index.
    listCtrl->DeleteAllItems();
    i = 0;
    processedCount = 0;

    // Ask the user to choose whether to open a folder or a file.
    wxMessageDialog chooseDlg(this,
                              "Do you want to choose a folder?\n"
                              "Click Yes for folder, No for file.",
                              "Select Type", wxYES_NO | wxCENTRE);
    bool chooseFolder = (chooseDlg.ShowModal() == wxID_YES);

    wxString path;
    if (chooseFolder) {
        // Open a directory selection dialog.
        wxDirDialog dirDlg(this, "Choose a directory", "",
                           wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dirDlg.ShowModal() == wxID_OK) {
            path = dirDlg.GetPath();
        } else {
            return; // User cancelled the dialog.
        }
    } else {
        // Open a file selection dialog.
        wxFileDialog fileDlg(this, "Choose a file", "",
                             "", "*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (fileDlg.ShowModal() == wxID_OK) {
            path = fileDlg.GetPath();
        } else {
            return; // User cancelled the dialog.
        }
    }

    // Start the processing in a separate thread using the selected path.
    std::thread([this, path]() {
        config config;
        auto lCallback1 = [this](const std::vector<std::string>& fileNames) {
            totalFiles = static_cast<int>(fileNames.size());
            progressGauge->SetRange(totalFiles);
            progressText->SetLabel(wxString::Format("%d/%d", 0, totalFiles));
        };
        auto lCallback2 = [this](const layer& l) {

            UpdateListEvent event(UPDATE_LIST_TYPE, wxID_ANY);
            event.SetLayer(l);
            wxPostEvent(this, event);

            std::lock_guard<std::mutex> lock(mtx);
            processedCount++;
            wxThreadEvent* evt = new wxThreadEvent(PROGRESS_UPDATE_EVENT);
            evt->SetInt(static_cast<int>(processedCount));
            wxQueueEvent(this, evt);
        };
        auto lCallback3 = [](std::vector<struct layer> layers) {
            // Layers callback.
        };
        auto lCallback4 = [](layer l) {
            // Error or additional info callback.
        };
        // Convert wxString to std::string.
        std::string selectedPath = path.ToStdString();
        layers = start(config, selectedPath,
                                                 lCallback1, lCallback2,
                                                 nullptr, nullptr);
    }).detach();
}

void Frame::OnExit(wxCommandEvent &event) {
    Close(true);
}
