/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2022 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

using namespace std;
auto startTime = chrono::high_resolution_clock::now();
inline bool caseInsCharCompareN(char a, char b);
bool caseInsCompare(const string &s1, const vector<string> &s2) {
    for (string text: s2) {
        if ((s1.size() == text.size()) && equal(s1.begin(), s1.end(), text.begin(), caseInsCharCompareN))
            return true;
    }
    return false;
}
bool caseInsCharCompareN(char a, char b) {
    return (toupper(a) == toupper(b));
}
thread_pool pool;
void start(string path);
void openSource(string file);
static void help(const char *programName);
string connection;
string t_srs;
string s_srs;
string nln;
string schema;
bool import{false};
bool p_multi{false};
bool append{false};
const int maxFeatures{1000};
struct layer {
    string driverName;
    GIntBig featureCount;
    string type;
    string layerName;
    string hasWkt;
    string file;
    char *wktString;
    string authStr;
    int layerIndex;
    string error;
    bool singleMultiMixed;
};
vector<struct layer> layers;
struct ctx {
    int layerIndex;
    bool error{false};
};
void
translate(layer l, const string &encoding, int index, bool first);
static void pgErrorHandler(CPLErr e, CPLErrorNum n, const char *msg) {
    string str(msg);
    ctx *myctx = (ctx *) CPLGetErrorHandlerUserData();
    layers[myctx->layerIndex].error = str;
    myctx->error = true;
}
static void openErrorHandler(CPLErr e, CPLErrorNum n, const char *msg) {
    string str(msg);
    layer *l = (layer *) CPLGetErrorHandlerUserData();
    l->error = str;
}
indicators::BlockProgressBar readBar{
        indicators::option::BarWidth{50},
        indicators::option::ForegroundColor{indicators::Color::white},
        indicators::option::FontStyles{
                vector<indicators::FontStyle>{indicators::FontStyle::bold}
        },
        indicators::option::PostfixText{"Analyzing files"},
};
indicators::BlockProgressBar importBar{
        indicators::option::BarWidth{50},
        indicators::option::ForegroundColor{indicators::Color::white},
        indicators::option::FontStyles{
                vector<indicators::FontStyle>{indicators::FontStyle::bold}
        },
        indicators::option::PostfixText{"Importing to PostgreSQL"},
};
