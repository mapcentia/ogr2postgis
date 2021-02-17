#include "ogrsf_frmts.h"
#include "gdal_utils.h"
#include <getopt.h>
#include <iostream>
#include <vector>
#include <list>
#include <experimental/filesystem>

using namespace std;

namespace fs = experimental::filesystem;

inline bool caseInsCharCompareN(char a, char b);

bool caseInsCompare(const string &s1, vector<string> s2);

bool translate(const char *pszFilename, const char *encoding, const char *layerName, int featureCount, char *wktString,
               const char *type, char string1[100]);

void start(char const *path);

void open(basic_string<char> file);

static void help(const char *programName);

const char *connection;
const char *t_srs{nullptr};
const char *s_srs{nullptr};
const char *schema{"public"};
int countf{0};
bool import{false};
const int maxFeatures{1};

int main(int argc, char *argv[]) {
    int opt;
    int long_index{0};
    const char *programName{argv[0]};
    const char *path;

    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0) {
            help(programName);
            exit(0);
        }
    }

    //Specifying the expected options
    static struct option long_options[] = {
            {"connection", required_argument, nullptr, 'c'},
            {"schema",     required_argument, nullptr, 'o'},
            {"t_srs",      required_argument, nullptr, 't'},
            {"s_srs",      required_argument, nullptr, 's'},
            {"import",     no_argument,       nullptr, 'i'},
            {"help",       no_argument,       nullptr, '?'},
            {nullptr, 0,                      nullptr, 0}
    };
    while ((opt = getopt_long(argc, argv, "c:o:t:s:i:", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'c':
                printf("connection: %s\n", optarg);
                connection = optarg;
                break;
            case 'o':
                printf("schema: %s\n", optarg);
                schema = optarg;
                break;
            case 't':
                printf("t_srs: %s\n", optarg);
                t_srs = optarg;
                break;
            case 's':
                printf("s_srs: %s\n", optarg);
                s_srs = optarg;
                break;
            case 'i':
                printf("import\n");
                import = true;
                optind--;
                break;
            default:
                help(programName);
                exit(1);
        }
    }
    if (argc < 2) {
        help(programName);
        exit(1);
    }

    // optind is for the extra arguments
    // which are not parsed
    for (; optind < argc; optind++) {
        path = argv[optind];
    }

    GDALAllRegister();
    start(path);
}

void help(const char *programName) {
    printf("%s Iterate recursive through a directory and prints info found geo-spatial vector file formats. Optional import files into to PostGIS\n\n",
           programName);
    printf("Usage:\n");
    printf("  %s [OPTION]... [DIRECTORY]\n", programName);

    printf("\nGeneral options:\n");
    printf("  -?, --help                    show this help, then exit\n");

    printf("\nOptions controlling the import to postgis:\n");
    printf("  -i, --import                  do import into postgis\n");
    printf("  -o, --schema                  output schema, Defaults to public\n");
    printf("  -s, --s_srs                   fallback source SRS.\n");
    printf("  -t, --t_srs                   fallback target SRS. Defaults to EPSG:4326\n");

    printf("\nConnection options:\n");
    printf("  -c, --connection=PGDATASOURCE postgres datasource. E.g.\"dbname='databasename' host='addr' port='5432' user='x' password='y'\"\n");

//    printf("\nIf no database name is supplied, then the PGDATABASE environment\n"
//           "variable value is used.\n\n");
//    printf("Report bugs to <%s>.\n"), PACKAGE_BUGREPORT;
//    printf("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL;
}

void start(const char *path) {
    string file;
    string fileExtension;
    vector<string> extensions{{".tab", ".shp", ".gml", ".geojson", ".json"}};
    try {
        for (auto &p: fs::recursive_directory_iterator(path)) {
            file = p.path().string();
            fileExtension = p.path().extension();
            if (caseInsCompare(fileExtension, extensions)) {
                open(file);
            }
        }
    } catch (const std::exception &e) {
        if (!std::experimental::filesystem::exists(path)) {
            printf("ERROR: Could not open directory or file.\n");
            exit(1);
        };
        open(path);
    }
    printf("Total %i\n", countf);
    printf("%s\n", GDALVersionInfo("--version"));
}

void open(basic_string<char> file) {
    countf++;
    GDALDataset *poDS = (GDALDataset *) GDALOpenEx(file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (poDS == nullptr) {
        printf("Open failed.\n");
        exit(1);
    }
    OGRSpatialReference *projection;
    char *wktString{nullptr};
    const char *authorityName;
    const char *authorityCode;
    const char *hasWkt{"True"};
    const char *driverName{poDS->GetDriverName()};
    char authStr[100];
    int layerCount{poDS->GetLayerCount()};

    for (int i = 0; i < layerCount; i++) {
        OGRLayer *layer{poDS->GetLayer(i)};
        const OGRSpatialReference *reference = layer->GetSpatialRef();
        if (reference != nullptr) {
            projection = layer->GetLayerDefn()->OGRFeatureDefn::GetGeomFieldDefn(0)->GetSpatialRef();
            projection->exportToWkt(&wktString);
            authorityName = projection->GetAuthorityName(nullptr);
            authorityCode = projection->GetAuthorityCode(nullptr);
            if (authorityName != nullptr && authorityCode != nullptr) {
                const char *sep{":"};
                strcpy(authStr, authorityName);
                strcat(authStr, sep);
                strcat(authStr, authorityCode);
            } else {
                strcpy(authStr, "-");
            }
        } else {
            authorityName = "";
            authorityCode = "Na";
            hasWkt = "False";
            strcpy(authStr, "-");
        }

        // Count features
        GIntBig featureCount = layer->GetFeatureCount(1);

        int count{0};
        const char *type;
        OGRFeature *poFeature;
        while ((poFeature = layer->GetNextFeature()) != nullptr) {
            OGRGeometry *poGeometry = poFeature->GetGeometryRef();
            if (poGeometry != nullptr) {
                switch (wkbFlatten(poGeometry->getGeometryType())) {
                    case 1:
                        type = "point";
                        break;
                    case 2:
                        type = "linestring";
                        break;
                    case 3:
                        type = "polygon";
                        break;
                    case 4:
                        type = "multipoint";
                        break;
                    case 5:
                        type = "multilinestring";
                        break;
                    case 6:
                        type = "multipolygon";
                }
            }
            count++;

            if (count == maxFeatures || count == featureCount) {
                std::cout << std::endl;
                break;
            } else {
                continue;
            }
        }

        printf("%*s %*llu %*s %*i %*s %*s %*s %s", -14, driverName, 8, featureCount, -14, type, 3,
               layerCount,
               -30, poDS->GetLayer(i)->GetName(), 10, hasWkt, -12, authStr, file.c_str());

        OGRFeature::DestroyFeature(poFeature);

        if (import) {
            if (translate(file.c_str(), "UTF8", layer->GetName(), featureCount, wktString, type, authStr) ==
                FALSE) {
                translate(file.c_str(), "LATIN1", layer->GetName(), featureCount, wktString, type, authStr);
            };
        }
    }
    GDALClose(poDS);
}

bool caseInsCompare(const string &s1, vector<string> s2) {
    for (string text : s2) {
        if ((s1.size() == text.size()) && equal(s1.begin(), s1.end(), text.begin(), caseInsCharCompareN))
            return true;
    }
    return false;
}

bool caseInsCharCompareN(char a, char b) {
    return (toupper(a) == toupper(b));
}

bool translate(const char *pszFilename, const char *encoding, const char *layerName, int featureCount, char *wktString,
               const char *type, char authStr[100]) {

    // Set client encoding with a env
    const char *one{"PGCLIENTENCODING="};
    char buf[100];
    strcpy(buf, one);
    strcat(buf, encoding);
    putenv(buf);

    char **argv{nullptr};
    const char *targetSrs = reinterpret_cast<const char *>(wktString != nullptr ? wktString : s_srs);
    if (targetSrs == nullptr) {
        std::cout << "   Can't impoort without source srs" << std::endl;
        CSLDestroy(argv);
        return TRUE;
    }
    argv = CSLAddString(argv, "-f");
    argv = CSLAddString(argv, "PostgreSQL");
    argv = CSLAddString(argv, "-lco");
    argv = CSLAddString(argv, "GEOMETRY_NAME=the_geom");
    argv = CSLAddString(argv, "-lco");
    argv = CSLAddString(argv, "FID=gid");
    argv = CSLAddString(argv, "-lco");
    argv = CSLAddString(argv, "PRECISION=NO");
    argv = CSLAddString(argv, "-nlt");
    argv = CSLAddString(argv, type);
    argv = CSLAddString(argv, "-s_srs"); // source projection
    argv = CSLAddString(argv, targetSrs);
    argv = CSLAddString(argv, "-t_srs");
    argv = CSLAddString(argv,
                        reinterpret_cast<const char *>(strcmp(authStr, "-") != 0 ? authStr : t_srs != nullptr ? t_srs
                                                                                                              : "EPSG:4326")); // Convert to this
    argv = CSLAddString(argv, "-nln");

    char schemaQualifiedName[100];   // array to hold the result.
    strcpy(schemaQualifiedName, schema); // copy string one into the result.
    strcat(schemaQualifiedName, "."); // copy string one into the result.
    strcat(schemaQualifiedName, layerName); // append string two to the result.

    argv = CSLAddString(argv, schemaQualifiedName);
    GDALDatasetH TestDs = GDALOpenEx(pszFilename, GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    GDALDatasetH pgDs = GDALOpenEx(connection, GDAL_OF_VECTOR,
                                   nullptr, nullptr, nullptr);
    if (pgDs == nullptr) {
        exit(1);
    }

    int bUsageError{FALSE};
    GDALVectorTranslateOptions *opt = GDALVectorTranslateOptionsNew(argv, nullptr);
    auto *dst = (GDALDataset *) GDALVectorTranslate(nullptr, pgDs, 1, &TestDs, opt, &bUsageError);

    if (dst == nullptr) {
        std::cout << "ERROR!" << std::endl;
        GDALVectorTranslateOptionsFree(opt);
        GDALClose(dst);
        return TRUE;
    } else {
        OGRLayer *layer = dst->GetLayerByName(schemaQualifiedName);

        if (layer->GetFeatureCount() != featureCount) {
            std::cout << "Try again" << std::endl;
            GDALVectorTranslateOptionsFree(opt);
            GDALClose(dst);
            return FALSE;
        }
        std::cout << "   Imported" << std::endl;
        GDALVectorTranslateOptionsFree(opt);
        GDALClose(dst);
        CSLDestroy(argv);
        return TRUE;
    }
}
