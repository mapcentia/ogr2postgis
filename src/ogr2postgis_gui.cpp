/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2025 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include <wx/wx.h>
#include "ogr2postgis.hpp"
#include <wx/listctrl.h>
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/config.h>
#include <wx/spinctrl.h>
#include <wx/radiobox.h>
#include <wx/statline.h>
#include <mutex>
#include <thread>

using namespace ogr2postgis;

class App : public wxApp {
public:
    bool OnInit() override;
};

// ---------------------------------------------------------------------------
// Persisted settings (wxConfig, survives restarts)
// ---------------------------------------------------------------------------

struct PgSettings {
    wxString host{"localhost"};
    int port{5432};
    wxString database;
    wxString user;
    wxString password;

    [[nodiscard]] bool isComplete() const { return !database.IsEmpty(); }
};

struct ImportOptions {
    wxString schema{"public"};
    wxString tableName; // empty = use layer names (CLI: --nln)
    int mode{0}; // 0 = create/overwrite, 1 = append, 2 = append + truncate
    bool promoteToMulti{false};
    wxString tSrs{"EPSG:4326"};
    wxString sSrs;
    wxString encoding{"LATIN1"};
    wxString timestampColumn; // empty = no timestamp column
    bool csvAutodetect{false};
    wxString xNames{"lon*,Lon*,x,X"};
    wxString yNames{"lat*,Lat*,y,Y"};
};

inline PgSettings loadPgSettings() {
    wxConfigBase *cfg = wxConfigBase::Get();
    PgSettings s;
    s.host = cfg->Read("/connection/host", "localhost");
    s.port = static_cast<int>(cfg->ReadLong("/connection/port", 5432));
    s.database = cfg->Read("/connection/database", "");
    s.user = cfg->Read("/connection/user", "");
    s.password = cfg->Read("/connection/password", "");
    return s;
}

inline void savePgSettings(const PgSettings &s) {
    wxConfigBase *cfg = wxConfigBase::Get();
    cfg->Write("/connection/host", s.host);
    cfg->Write("/connection/port", s.port);
    cfg->Write("/connection/database", s.database);
    cfg->Write("/connection/user", s.user);
    cfg->Write("/connection/password", s.password);
    cfg->Flush();
}

inline ImportOptions loadImportOptions() {
    wxConfigBase *cfg = wxConfigBase::Get();
    ImportOptions o;
    o.schema = cfg->Read("/import/schema", "public");
    o.tableName = cfg->Read("/import/tableName", "");
    o.mode = static_cast<int>(cfg->ReadLong("/import/mode", 0));
    cfg->Read("/import/promoteToMulti", &o.promoteToMulti, false);
    o.tSrs = cfg->Read("/import/t_srs", "EPSG:4326");
    o.sSrs = cfg->Read("/import/s_srs", "");
    o.encoding = cfg->Read("/import/encoding", "LATIN1");
    o.timestampColumn = cfg->Read("/import/timestampColumn", "");
    cfg->Read("/import/csvAutodetect", &o.csvAutodetect, false);
    o.xNames = cfg->Read("/import/x_possible_names", "lon*,Lon*,x,X");
    o.yNames = cfg->Read("/import/y_possible_names", "lat*,Lat*,y,Y");
    return o;
}

inline void saveImportOptions(const ImportOptions &o) {
    wxConfigBase *cfg = wxConfigBase::Get();
    cfg->Write("/import/schema", o.schema);
    cfg->Write("/import/tableName", o.tableName);
    cfg->Write("/import/mode", o.mode);
    cfg->Write("/import/promoteToMulti", o.promoteToMulti);
    cfg->Write("/import/t_srs", o.tSrs);
    cfg->Write("/import/s_srs", o.sSrs);
    cfg->Write("/import/encoding", o.encoding);
    cfg->Write("/import/timestampColumn", o.timestampColumn);
    cfg->Write("/import/csvAutodetect", o.csvAutodetect);
    cfg->Write("/import/x_possible_names", o.xNames);
    cfg->Write("/import/y_possible_names", o.yNames);
    cfg->Flush();
}

// Escape a value for use inside single quotes in a PG connection string.
inline std::string pgEscape(const wxString &v) {
    std::string out;
    for (const char c: v.ToStdString()) {
        if (c == '\\' || c == '\'') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

inline std::string buildConnectionString(const PgSettings &s) {
    std::string conn{"PG:"};
    if (!s.host.IsEmpty()) {
        conn += "host='" + pgEscape(s.host) + "' ";
    }
    conn += "port='" + std::to_string(s.port) + "' ";
    conn += "dbname='" + pgEscape(s.database) + "'";
    if (!s.user.IsEmpty()) {
        conn += " user='" + pgEscape(s.user) + "'";
    }
    if (!s.password.IsEmpty()) {
        conn += " password='" + pgEscape(s.password) + "'";
    }
    return conn;
}

inline config makeConfig(const PgSettings &pg, const ImportOptions &o) {
    config c;
    c.connection = buildConnectionString(pg);
    c.schema = o.schema.IsEmpty() ? "public" : o.schema.ToStdString();
    c.nln = o.tableName.ToStdString();
    c.t_srs = o.tSrs.IsEmpty() ? "EPSG:4326" : o.tSrs.ToStdString();
    c.s_srs = o.sSrs.ToStdString();
    c.fallbackEncoding = o.encoding.IsEmpty() ? "LATIN1" : o.encoding.ToStdString();
    c.import = true;
    c.p_multi = o.promoteToMulti;
    c.append = o.mode >= 1;
    c.truncate = o.mode == 2;
    c.json = false;
    c.autodetect = o.csvAutodetect;
    c.timestamp = o.timestampColumn.ToStdString();
    c.x_possible_names = o.xNames.ToStdString();
    c.y_possible_names = o.yNames.ToStdString();
    return c;
}

// ---------------------------------------------------------------------------
// Settings dialog (PostgreSQL connection, persisted across restarts)
// ---------------------------------------------------------------------------

class SettingsDialog final : public wxDialog {
public:
    explicit SettingsDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, "PostgreSQL Connection") {
        const PgSettings s = loadPgSettings();

        auto *grid = new wxFlexGridSizer(2, wxSize(8, 8));
        grid->AddGrowableCol(1, 1);
        const wxSize fieldSize(280, -1);

        grid->Add(new wxStaticText(this, wxID_ANY, "Host"), 0, wxALIGN_CENTER_VERTICAL);
        hostCtrl = new wxTextCtrl(this, wxID_ANY, s.host, wxDefaultPosition, fieldSize);
        grid->Add(hostCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Port"), 0, wxALIGN_CENTER_VERTICAL);
        portCtrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, s.port);
        grid->Add(portCtrl, 0);

        grid->Add(new wxStaticText(this, wxID_ANY, "Database"), 0, wxALIGN_CENTER_VERTICAL);
        databaseCtrl = new wxTextCtrl(this, wxID_ANY, s.database, wxDefaultPosition, fieldSize);
        grid->Add(databaseCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "User"), 0, wxALIGN_CENTER_VERTICAL);
        userCtrl = new wxTextCtrl(this, wxID_ANY, s.user, wxDefaultPosition, fieldSize);
        grid->Add(userCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Password"), 0, wxALIGN_CENTER_VERTICAL);
        passwordCtrl = new wxTextCtrl(this, wxID_ANY, s.password, wxDefaultPosition, fieldSize,
                                      wxTE_PASSWORD);
        grid->Add(passwordCtrl, 1, wxEXPAND);

        auto *mainSizer = new wxBoxSizer(wxVERTICAL);
        mainSizer->Add(grid, 1, wxEXPAND | wxALL, 12);

        auto *testBtn = new wxButton(this, wxID_ANY, "Test Connection");
        mainSizer->Add(testBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

        mainSizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0,
                       wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        SetSizerAndFit(mainSizer);
        CentreOnParent();

        Bind(wxEVT_BUTTON, &SettingsDialog::OnTest, this, testBtn->GetId());
        Bind(wxEVT_BUTTON, &SettingsDialog::OnOk, this, wxID_OK);
    }

    [[nodiscard]] PgSettings current() const {
        PgSettings s;
        s.host = hostCtrl->GetValue();
        s.port = portCtrl->GetValue();
        s.database = databaseCtrl->GetValue();
        s.user = userCtrl->GetValue();
        s.password = passwordCtrl->GetValue();
        return s;
    }

private:
    wxTextCtrl *hostCtrl;
    wxSpinCtrl *portCtrl;
    wxTextCtrl *databaseCtrl;
    wxTextCtrl *userCtrl;
    wxTextCtrl *passwordCtrl;

    void OnTest(wxCommandEvent &) {
        const PgSettings s = current();
        if (!s.isComplete()) {
            wxMessageBox("Enter at least a database name.", "Test Connection",
                         wxOK | wxICON_WARNING, this);
            return;
        }
        wxBusyCursor busy;
        GDALAllRegister();
        const std::string conn = buildConnectionString(s);
        CPLPushErrorHandler(CPLQuietErrorHandler);
        GDALDatasetH ds = GDALOpenEx(conn.c_str(),
                                     GDAL_OF_VECTOR | GDAL_OF_UPDATE,
                                     nullptr, nullptr, nullptr);
        CPLPopErrorHandler();
        if (ds != nullptr) {
            GDALClose(ds);
            wxMessageBox("Connection succeeded.", "Test Connection",
                         wxOK | wxICON_INFORMATION, this);
        } else {
            const char *msg = CPLGetLastErrorMsg();
            wxMessageBox(wxString("Connection failed.\n\n") +
                         (msg != nullptr ? msg : ""), "Test Connection",
                         wxOK | wxICON_ERROR, this);
        }
    }

    void OnOk(wxCommandEvent &event) {
        savePgSettings(current());
        event.Skip(); // let the default handler close the dialog
    }
};

// ---------------------------------------------------------------------------
// Import options dialog (GUI counterpart of the CLI flags)
// ---------------------------------------------------------------------------

class ImportOptionsDialog final : public wxDialog {
public:
    ImportOptionsDialog(wxWindow *parent, const size_t layerCount)
        : wxDialog(parent, wxID_ANY,
                   wxString::Format("Import %zu Layer(s) to PostGIS", layerCount)) {
        const ImportOptions o = loadImportOptions();
        const wxSize fieldSize(240, -1);

        auto *grid = new wxFlexGridSizer(2, wxSize(8, 8));
        grid->AddGrowableCol(1, 1);

        grid->Add(new wxStaticText(this, wxID_ANY, "Schema"), 0, wxALIGN_CENTER_VERTICAL);
        schemaCtrl = new wxTextCtrl(this, wxID_ANY, o.schema, wxDefaultPosition, fieldSize);
        grid->Add(schemaCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Table name"), 0, wxALIGN_CENTER_VERTICAL);
        tableNameCtrl = new wxTextCtrl(this, wxID_ANY, o.tableName, wxDefaultPosition, fieldSize);
        tableNameCtrl->SetHint("Empty = use layer names");
        grid->Add(tableNameCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Target SRS"), 0, wxALIGN_CENTER_VERTICAL);
        tSrsCtrl = new wxTextCtrl(this, wxID_ANY, o.tSrs, wxDefaultPosition, fieldSize);
        tSrsCtrl->SetToolTip("Fallback target SRS. Used if no authority name/code is available.");
        grid->Add(tSrsCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Source SRS"), 0, wxALIGN_CENTER_VERTICAL);
        sSrsCtrl = new wxTextCtrl(this, wxID_ANY, o.sSrs, wxDefaultPosition, fieldSize);
        sSrsCtrl->SetHint("Empty = from file");
        sSrsCtrl->SetToolTip("Fallback source SRS. Used if the file doesn't contain projection information.");
        grid->Add(sSrsCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Fallback encoding"), 0, wxALIGN_CENTER_VERTICAL);
        encodingCtrl = new wxTextCtrl(this, wxID_ANY, o.encoding, wxDefaultPosition, fieldSize);
        encodingCtrl->SetToolTip("Used if UTF8 fails.");
        grid->Add(encodingCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Timestamp column"), 0, wxALIGN_CENTER_VERTICAL);
        timestampCtrl = new wxTextCtrl(this, wxID_ANY, o.timestampColumn, wxDefaultPosition, fieldSize);
        timestampCtrl->SetHint("Empty = no timestamp column");
        timestampCtrl->SetToolTip("Adds a column with the import date/time to the table.");
        grid->Add(timestampCtrl, 1, wxEXPAND);

        const wxString modes[] = {
            "Create / overwrite table",
            "Append to existing table",
            "Append, truncate table first"
        };
        modeBox = new wxRadioBox(this, wxID_ANY, "Table mode", wxDefaultPosition,
                                 wxDefaultSize, 3, modes, 1, wxRA_SPECIFY_COLS);
        modeBox->SetSelection(o.mode >= 0 && o.mode <= 2 ? o.mode : 0);

        promoteCtrl = new wxCheckBox(this, wxID_ANY, "Promote single geometries to multi part");
        promoteCtrl->SetValue(o.promoteToMulti);

        auto *csvBox = new wxStaticBoxSizer(wxVERTICAL, this, "CSV files");
        csvAutodetectCtrl = new wxCheckBox(csvBox->GetStaticBox(), wxID_ANY, "Auto detect column types");
        csvAutodetectCtrl->SetValue(o.csvAutodetect);
        csvBox->Add(csvAutodetectCtrl, 0, wxALL, 4);

        auto *csvGrid = new wxFlexGridSizer(2, wxSize(8, 8));
        csvGrid->AddGrowableCol(1, 1);
        csvGrid->Add(new wxStaticText(csvBox->GetStaticBox(), wxID_ANY, "X/longitude columns"),
                     0, wxALIGN_CENTER_VERTICAL);
        xNamesCtrl = new wxTextCtrl(csvBox->GetStaticBox(), wxID_ANY, o.xNames,
                                    wxDefaultPosition, fieldSize);
        csvGrid->Add(xNamesCtrl, 1, wxEXPAND);
        csvGrid->Add(new wxStaticText(csvBox->GetStaticBox(), wxID_ANY, "Y/latitude columns"),
                     0, wxALIGN_CENTER_VERTICAL);
        yNamesCtrl = new wxTextCtrl(csvBox->GetStaticBox(), wxID_ANY, o.yNames,
                                    wxDefaultPosition, fieldSize);
        csvGrid->Add(yNamesCtrl, 1, wxEXPAND);
        csvBox->Add(csvGrid, 0, wxEXPAND | wxALL, 4);

        auto *mainSizer = new wxBoxSizer(wxVERTICAL);
        mainSizer->Add(grid, 0, wxEXPAND | wxALL, 12);
        mainSizer->Add(modeBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        mainSizer->Add(promoteCtrl, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        mainSizer->Add(csvBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        mainSizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0,
                       wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        SetSizerAndFit(mainSizer);
        CentreOnParent();

        auto *okButton = FindWindow(wxID_OK);
        if (okButton != nullptr) {
            static_cast<wxButton *>(okButton)->SetLabel("Import");
        }
        Bind(wxEVT_BUTTON, &ImportOptionsDialog::OnOk, this, wxID_OK);
    }

    [[nodiscard]] ImportOptions current() const {
        ImportOptions o;
        o.schema = schemaCtrl->GetValue();
        o.tableName = tableNameCtrl->GetValue();
        o.mode = modeBox->GetSelection();
        o.promoteToMulti = promoteCtrl->GetValue();
        o.tSrs = tSrsCtrl->GetValue();
        o.sSrs = sSrsCtrl->GetValue();
        o.encoding = encodingCtrl->GetValue();
        o.timestampColumn = timestampCtrl->GetValue();
        o.csvAutodetect = csvAutodetectCtrl->GetValue();
        o.xNames = xNamesCtrl->GetValue();
        o.yNames = yNamesCtrl->GetValue();
        return o;
    }

private:
    wxTextCtrl *schemaCtrl;
    wxTextCtrl *tableNameCtrl;
    wxRadioBox *modeBox;
    wxCheckBox *promoteCtrl;
    wxTextCtrl *tSrsCtrl;
    wxTextCtrl *sSrsCtrl;
    wxTextCtrl *encodingCtrl;
    wxTextCtrl *timestampCtrl;
    wxCheckBox *csvAutodetectCtrl;
    wxTextCtrl *xNamesCtrl;
    wxTextCtrl *yNamesCtrl;

    void OnOk(wxCommandEvent &event) {
        saveImportOptions(current());
        event.Skip(); // let the default handler close the dialog
    }
};

// ---------------------------------------------------------------------------
// Main frame
// ---------------------------------------------------------------------------

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

enum {
    ID_OpenFiles = wxID_HIGHEST + 1,
    ID_OpenFolder,
    ID_Settings,
    ID_ImportSelected,
    ID_SelectAll,
    ID_SelectNone
};

enum {
    COL_DRIVER = 0,
    COL_COUNT,
    COL_TYPE,
    COL_LAYER_NO,
    COL_NAME,
    COL_PROJ,
    COL_AUTH,
    COL_FILE,
    COL_ERROR,
    COL_STATUS
};

class Frame final : public wxFrame {
public:
    Frame();

private:
    wxListCtrl *listCtrl;
    wxGauge *progressGauge;
    wxStaticText *progressText;
    wxButton *importButton;
    wxButton *selectAllButton;
    wxButton *selectNoneButton;
    std::vector<layer> analyzedLayers; // one entry per list row, same order
    int totalFiles = 0;
    std::mutex mtx; // protects processedCount (written from pool threads)
    size_t processedCount = 0;
    bool busy = false;

    void OnExit(wxCommandEvent &event);

    void OnOpenFiles(wxCommandEvent &event);

    void OnOpenFolder(wxCommandEvent &event);

    void OnSettings(wxCommandEvent &event);

    void OnImportSelected(wxCommandEvent &event);

    void OnSelectAll(wxCommandEvent &event) { checkAll(true); }

    void OnSelectNone(wxCommandEvent &event) { checkAll(false); }

    void OnOpen(const UpdateListEvent &event);

    void OnProgressUpdate(wxThreadEvent &event);

    void checkAll(bool check) {
        for (long row = 0; row < listCtrl->GetItemCount(); row++) {
            listCtrl->CheckItem(row, check && analyzedLayers[row].error.empty());
        }
    }

    void setBusy(bool value) {
        busy = value;
        const bool enabled = !value;
        GetMenuBar()->Enable(ID_OpenFiles, enabled);
        GetMenuBar()->Enable(ID_OpenFolder, enabled);
        GetMenuBar()->Enable(ID_ImportSelected, enabled);
        importButton->Enable(enabled);
        selectAllButton->Enable(enabled);
        selectNoneButton->Enable(enabled);
    }

    void resetProgress(int total) {
        totalFiles = total;
        processedCount = 0;
        progressGauge->SetRange(total > 0 ? total : 1);
        progressGauge->SetValue(0);
        progressText->SetLabel(wxString::Format("0/%d", total));
    }

    void startAnalysis(std::vector<std::string> paths);

    void runImport(config cfg, std::vector<layer> selection, std::vector<long> rows);
};

bool App::OnInit() {
    SetAppName("ogr2postgis");
    auto *frame = new Frame();
    frame->Show(true);
    return true;
}

wxIMPLEMENT_APP(App);

Frame::Frame()
    : wxFrame(nullptr, wxID_ANY, "ogr2postgis", wxDefaultPosition, wxSize(1100, 600)) {
    auto *menuFile = new wxMenu;
    menuFile->Append(ID_OpenFiles, "Open &Files...\tCtrl+O", "Analyze one or more files");
    menuFile->Append(ID_OpenFolder, "Open F&older...\tCtrl+Shift+O", "Analyze all files in a folder");
    menuFile->AppendSeparator();
    menuFile->Append(ID_ImportSelected, "&Import Selected...\tCtrl+I", "Import checked layers to PostGIS");
    menuFile->AppendSeparator();
    menuFile->Append(ID_Settings, "&Settings...", "Configure the PostgreSQL connection");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    auto *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    SetMenuBar(menuBar);
    CreateStatusBar();

    auto *mainSizer = new wxBoxSizer(wxVERTICAL);

    // Create the list control and add columns
    listCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
    listCtrl->EnableCheckBoxes(true);
    listCtrl->InsertColumn(COL_DRIVER, "Driver", wxLIST_FORMAT_LEFT, 110);
    listCtrl->InsertColumn(COL_COUNT, "Count", wxLIST_FORMAT_LEFT, 70);
    listCtrl->InsertColumn(COL_TYPE, "Type", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(COL_LAYER_NO, "Layer no.", wxLIST_FORMAT_LEFT, 70);
    listCtrl->InsertColumn(COL_NAME, "Name", wxLIST_FORMAT_LEFT, 150);
    listCtrl->InsertColumn(COL_PROJ, "Proj", wxLIST_FORMAT_LEFT, 60);
    listCtrl->InsertColumn(COL_AUTH, "Auth", wxLIST_FORMAT_LEFT, 100);
    listCtrl->InsertColumn(COL_FILE, "File", wxLIST_FORMAT_LEFT, 250);
    listCtrl->InsertColumn(COL_ERROR, "Error", wxLIST_FORMAT_LEFT, 150);
    listCtrl->InsertColumn(COL_STATUS, "Status", wxLIST_FORMAT_LEFT, 100);
    mainSizer->Add(listCtrl, 1, wxEXPAND | wxALL, 5);

    // Create a progress gauge (progress bar)
    progressGauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL);
    mainSizer->Add(progressGauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

    auto *bottomSizer = new wxBoxSizer(wxHORIZONTAL);
    selectAllButton = new wxButton(this, ID_SelectAll, "Select All");
    selectNoneButton = new wxButton(this, ID_SelectNone, "Select None");
    bottomSizer->Add(selectAllButton, 0, wxRIGHT, 5);
    bottomSizer->Add(selectNoneButton, 0, wxRIGHT, 5);
    progressText = new wxStaticText(this, wxID_ANY, "0/0");
    bottomSizer->AddStretchSpacer();
    bottomSizer->Add(progressText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    bottomSizer->AddStretchSpacer();
    importButton = new wxButton(this, ID_ImportSelected, "Import Selected...");
    bottomSizer->Add(importButton, 0);
    mainSizer->Add(bottomSizer, 0, wxEXPAND | wxALL, 5);

    SetSizer(mainSizer);

    // Bind events for menu and custom update events
    Bind(wxEVT_MENU, &Frame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &Frame::OnOpenFiles, this, ID_OpenFiles);
    Bind(wxEVT_MENU, &Frame::OnOpenFolder, this, ID_OpenFolder);
    Bind(wxEVT_MENU, &Frame::OnSettings, this, ID_Settings);
    Bind(wxEVT_MENU, &Frame::OnImportSelected, this, ID_ImportSelected);
    Bind(wxEVT_BUTTON, &Frame::OnImportSelected, this, ID_ImportSelected);
    Bind(wxEVT_BUTTON, &Frame::OnSelectAll, this, ID_SelectAll);
    Bind(wxEVT_BUTTON, &Frame::OnSelectNone, this, ID_SelectNone);
    Bind(UPDATE_LIST_TYPE, &Frame::OnOpen, this, wxID_ANY);
    Bind(PROGRESS_UPDATE_EVENT, &Frame::OnProgressUpdate, this, wxID_ANY);
}

void Frame::OnProgressUpdate(wxThreadEvent &event) {
    const int newValue = event.GetInt();
    if (newValue <= progressGauge->GetRange()) {
        progressGauge->SetValue(newValue);
    }
    progressText->SetLabel(wxString::Format("%d/%d", newValue, totalFiles));
}

void Frame::OnOpen(const UpdateListEvent &event) {
    const layer l = event.GetLayer();
    const long row = listCtrl->GetItemCount();
    const long index = listCtrl->InsertItem(row, l.driverName);
    listCtrl->SetItem(index, COL_COUNT, std::to_string(l.featureCount));
    listCtrl->SetItem(index, COL_TYPE, l.type + (l.singleMultiMixed ? "(m)" : ""));
    listCtrl->SetItem(index, COL_LAYER_NO, std::to_string(l.layerIndex));
    listCtrl->SetItem(index, COL_NAME, l.layerName);
    listCtrl->SetItem(index, COL_PROJ, l.hasWkt);
    listCtrl->SetItem(index, COL_AUTH, l.authStr);
    listCtrl->SetItem(index, COL_FILE, l.file);
    listCtrl->SetItem(index, COL_ERROR, l.error);
    analyzedLayers.push_back(l);
    if (l.error.empty()) {
        listCtrl->CheckItem(index, true);
    } else {
        listCtrl->SetItemTextColour(index, *wxRED);
    }
}

void Frame::OnOpenFiles(wxCommandEvent &) {
    if (busy) return;
    wxFileDialog fileDlg(this, "Choose files", "", "",
                         "Geodata files (*.shp;*.tab;*.gml;*.geojson;*.gpkg;*.fgb;*.csv;*.txt)|"
                         "*.shp;*.tab;*.gml;*.geojson;*.gpkg;*.fgb;*.csv;*.txt|All files|*.*",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
    if (fileDlg.ShowModal() != wxID_OK) {
        return;
    }
    wxArrayString wxPaths;
    fileDlg.GetPaths(wxPaths);
    std::vector<std::string> paths;
    paths.reserve(wxPaths.GetCount());
    for (const wxString &p: wxPaths) {
        paths.push_back(p.ToStdString());
    }
    startAnalysis(std::move(paths));
}

void Frame::OnOpenFolder(wxCommandEvent &) {
    if (busy) return;
    wxDirDialog dirDlg(this, "Choose a directory", "",
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dirDlg.ShowModal() != wxID_OK) {
        return;
    }
    startAnalysis({dirDlg.GetPath().ToStdString()});
}

void Frame::OnSettings(wxCommandEvent &) {
    SettingsDialog dlg(this);
    dlg.ShowModal(); // settings are saved by the dialog on OK
}

void Frame::startAnalysis(std::vector<std::string> paths) {
    listCtrl->DeleteAllItems();
    analyzedLayers.clear();
    resetProgress(0);
    setBusy(true);
    SetStatusText("Analyzing files...");

    std::thread([this, paths = std::move(paths)]() {
        config cfg; // import stays false: analysis only
        auto lCallback1 = [this](const std::vector<std::string> &fileNames) {
            CallAfter([this, total = static_cast<int>(fileNames.size())]() {
                resetProgress(total);
            });
        };
        auto lCallback2 = [this](const layer &l) {
            UpdateListEvent event(UPDATE_LIST_TYPE, wxID_ANY);
            event.SetLayer(l);
            wxPostEvent(this, event);

            std::lock_guard<std::mutex> lock(mtx);
            processedCount++;
            auto *evt = new wxThreadEvent(PROGRESS_UPDATE_EVENT);
            evt->SetInt(static_cast<int>(processedCount));
            wxQueueEvent(this, evt);
        };
        std::string errorMsg;
        try {
            start(cfg, paths, lCallback1, lCallback2, nullptr, nullptr);
        } catch (const std::exception &e) {
            errorMsg = e.what();
        }
        CallAfter([this, errorMsg]() {
            setBusy(false);
            SetStatusText(wxString::Format("%zu layer(s) found", analyzedLayers.size()));
            if (!errorMsg.empty()) {
                wxMessageBox("Analysis failed:\n" + errorMsg, "Error", wxOK | wxICON_ERROR, this);
            }
        });
    }).detach();
}

void Frame::OnImportSelected(wxCommandEvent &) {
    if (busy) return;

    // Collect the checked rows
    std::vector<long> rows;
    std::vector<layer> selection;
    for (long row = 0; row < listCtrl->GetItemCount(); row++) {
        if (listCtrl->IsItemChecked(row) && analyzedLayers[row].error.empty()) {
            rows.push_back(row);
            selection.push_back(analyzedLayers[row]);
        }
    }
    if (selection.empty()) {
        wxMessageBox("No layers are checked. Open files and check the layers to import.",
                     "Import", wxOK | wxICON_INFORMATION, this);
        return;
    }

    PgSettings pg = loadPgSettings();
    if (!pg.isComplete()) {
        SettingsDialog dlg(this);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }
        pg = loadPgSettings();
        if (!pg.isComplete()) {
            wxMessageBox("A database name is required.", "Import", wxOK | wxICON_WARNING, this);
            return;
        }
    }

    ImportOptionsDialog dlg(this, selection.size());
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }
    const config cfg = makeConfig(pg, dlg.current());

    runImport(cfg, std::move(selection), std::move(rows));
}

void Frame::runImport(config cfg, std::vector<layer> selection, std::vector<long> rows) {
    for (const long row: rows) {
        listCtrl->SetItem(row, COL_STATUS, "Importing...");
    }
    resetProgress(static_cast<int>(selection.size()));
    setBusy(true);
    SetStatusText("Importing to PostGIS...");

    std::thread([this, cfg = std::move(cfg), selection = std::move(selection),
                rows = std::move(rows)]() mutable {
        auto lCallback4 = [this](const layer &) {
            std::lock_guard<std::mutex> lock(mtx);
            processedCount++;
            auto *evt = new wxThreadEvent(PROGRESS_UPDATE_EVENT);
            evt->SetInt(static_cast<int>(processedCount));
            wxQueueEvent(this, evt);
        };
        std::vector<layer> result;
        std::string errorMsg;
        try {
            result = importLayers(cfg, std::move(selection), nullptr, lCallback4);
        } catch (const std::exception &e) {
            errorMsg = e.what();
        }
        CallAfter([this, result = std::move(result), rows = std::move(rows), errorMsg]() {
            setBusy(false);
            if (!errorMsg.empty()) {
                for (const long row: rows) {
                    listCtrl->SetItem(row, COL_STATUS, "");
                }
                SetStatusText("Import failed");
                wxMessageBox("Could not connect to PostgreSQL:\n" + errorMsg,
                             "Import failed", wxOK | wxICON_ERROR, this);
                return;
            }
            size_t failed = 0;
            for (size_t k = 0; k < rows.size() && k < result.size(); k++) {
                const layer &l = result[k];
                const long row = rows[k];
                listCtrl->SetItem(row, COL_ERROR, l.error);
                if (l.error.empty()) {
                    listCtrl->SetItem(row, COL_STATUS, "Imported");
                    listCtrl->SetItemTextColour(row, wxNullColour);
                } else {
                    failed++;
                    listCtrl->SetItem(row, COL_STATUS, "Failed");
                    listCtrl->SetItemTextColour(row, *wxRED);
                }
            }
            SetStatusText(wxString::Format("Imported %zu layer(s), %zu failed",
                                           rows.size() - failed, failed));
        });
    }).detach();
}

void Frame::OnExit(wxCommandEvent &) {
    Close(true);
}
