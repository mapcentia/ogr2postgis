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

bool caseInsCompare(const string &s1, const vector<string> &s2);

bool
translate(const char *pszFilename, const char *encoding, const char *layerName, int featureCount, const char *wktString,
          std::string type, char authStr[100], int leyerIndex);

void start(char const *path);

void open(const basic_string<char> &file);

static void help(const char *programName);

const char *connection;
const char *t_srs{nullptr};
const char *s_srs{nullptr};
const char *nln{nullptr};
const char *schema{"public"};
int countf{0};
bool import{false};
bool p_multi{false};
bool append{false};
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
            {"nln",        required_argument, nullptr, 'n'},
            {"import",     no_argument,       nullptr, 'i'},
            {"p_multi",    no_argument,       nullptr, 'm'},
            {"append",     no_argument,       nullptr, 'a'},
            {"help",       no_argument,       nullptr, '?'},
            {nullptr, 0,                      nullptr, 0}
    };
    while ((opt = getopt_long(argc, argv, "c:o:t:s:n:i:p:a:", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'c':
                connection = optarg;
                break;
            case 'o':
                schema = optarg;
                break;
            case 't':
                t_srs = optarg;
                break;
            case 's':
                s_srs = optarg;
                break;
            case 'n':
                nln = optarg;
                break;
            case 'i':
                import = true;
                optind--;
                break;
            case 'p':
                p_multi = true;
                optind--;
                break;
            case 'a':
                append = true;
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
    printf("%*s %*s %*s %*s %*s %*s %*s %s", -14, "Driver", 8, "Count", -9, "Type", 3, "Layer no.",
           -30, "Name", 10, "Proj", -12, "Auth", "File");

    GDALAllRegister();
    start(path);
}

void help(const char *programName) {
    printf("%s iterate recursive through a directory tree and prints info about found geo-spatial vector file formats. Optional import files into to a PostGIS database.\n",
           programName);
    printf(" Will only read files with these extensions (case insensitive) .tab, .shp, .gml, .geojson .json, .gpkg, .gdb\n\n");
    printf("Usage:\n");
    printf("  %s [OPTION]... [DIRECTORY|FILE]\n", programName);

    printf("\nGeneral options:\n");
    printf("  -?, --help                    Show this help, then exit\n");

    printf("\nOptions controlling the import to postgis:\n");
    printf("  -i, --import                  Optional. Import found files into PostgreSQL/PostGIS.\n");
    printf("  -o, --schema                  Optional. Output PostgreSQL schema, Defaults to public.\n");
    printf("  -s, --s_srs                   Optional. Fallback source SRS. Will be used if file doesn't contain projection information.\n");
    printf("  -t, --t_srs                   Optianal. Fallback target SRS. Will be used if no authority name/code is available. Defaults to EPSG:4326.\n");
    printf("  -n, --nln                     Optional. Alternative table name. Can only be used when importing single file - not directories unless --append is used.\n");
    printf("  -p, --p_multi                 Optional. Promote single geometries to multi part.\n");
    printf("  -a, --append                  Optional. Append to existing layer instead of creating new.\n");

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
    vector<string> extensions{{".tab", ".shp", ".gml", ".geojson", ".json", ".gpkg", ".gdb"}};
    std::string s(path);
    if (s.find(".gdb") != string::npos) {
        open(path);
    } else {
        try {
            for (auto &p: fs::recursive_directory_iterator(path)) {
                if (nln && import && !append) {
                    printf("ERROR: Can't use alternative table name for importing directories. All tables will be named alike.\n");
                    exit(1);
                }
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
    }
    printf("\nTotal %i\n", countf);
    printf("%s\n", GDALVersionInfo("--version"));
}

void open(const basic_string<char> &file) {
    countf++;
    auto *poDS = (GDALDataset *) GDALOpenEx(file.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (poDS == nullptr) {
        printf("Open failed.\n");
        exit(1);
    }
    OGRSpatialReference *projection;
    char *wktString{nullptr};
    const char *authorityName;
    const char *authorityCode;
    const char *hasWkt{"True"};
    const char *layerName;
    const char *driverName{poDS->GetDriverName()};
    char authStr[100];
    char layerNameBuf[100];
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
        std::string type;
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

        printf("%*s %*llu %*s %*i %*s %*s %*s %s", -14, i == 0 ? driverName : "", 8, featureCount, -15, type.c_str(), 3,
               i + 1,
               -30, poDS->GetLayer(i)->GetName(), 10, hasWkt, -12, authStr, i == 0 ? file.c_str() : "");
        if (import) {
            layerName = poDS->GetLayer(i)->GetName();
            if (translate(file.c_str(), "UTF8", layerName, featureCount, wktString, type, authStr, i) ==
                FALSE) {
                translate(file.c_str(), "LATIN1", layerName, featureCount, wktString, type, authStr, i);
            };
        }

        OGRFeature::DestroyFeature(poFeature);

    }
    GDALClose(poDS);
}

bool caseInsCompare(const string &s1, const vector<string> &s2) {
    for (string text : s2) {
        if ((s1.size() == text.size()) && equal(s1.begin(), s1.end(), text.begin(), caseInsCharCompareN))
            return true;
    }
    return false;
}

bool caseInsCharCompareN(char a, char b) {
    return (toupper(a) == toupper(b));
}

bool
translate(const char *pszFilename, const char *encoding, const char *layerName, int featureCount, const char *wktString,
          std::string type, char authStr[100], int layerIndex) {
    // Set client encoding with a env
    const char *altName{layerName};
    const char *one{"PGCLIENTENCODING="};
    char buf[100];
    char altNameBuf[100];
    char nameBuf[200];
    strcpy(buf, one);
    strcat(buf, encoding);
    putenv(buf);


    strcat(altNameBuf, schema);
    strcat(altNameBuf, ".");

    if (nln) {
        altName = nln;
        if (layerIndex > 0) {
            strcpy(altNameBuf, altName);
            strcat(altNameBuf, "_");
            strcat(altNameBuf, to_string(layerIndex).c_str());
            altName = altNameBuf;
        }
    }

    strcpy(nameBuf, schema);
    strcat(nameBuf, ".");
    strcat(nameBuf, altName);
    altName = nameBuf;

    if ((type == "point" || type == "linestring" || type == "polygon") && p_multi) {
        type = "multi" + type;
    }

    char **papszOptions = nullptr;
    char **argv{nullptr};

    const char *targetSrs = reinterpret_cast<const char *>(wktString != nullptr ? wktString : s_srs);
    if (targetSrs == nullptr) {
        std::cout << "   Can't impoort without source srs" << std::endl;
        CSLDestroy(argv);
        return TRUE;
    }
    argv = CSLAddString(argv, "-f");
    argv = CSLAddString(argv, "PostgreSQL");
    if (append) {
        argv = CSLAddString(argv, "-update");
        argv = CSLAddString(argv, "-append");
    }
    argv = CSLAddString(argv, "-lco");
    argv = CSLAddString(argv, "GEOMETRY_NAME=the_geom");
    argv = CSLAddString(argv, "-lco");
    argv = CSLAddString(argv, "FID=gid");
    argv = CSLAddString(argv, "-lco");
    argv = CSLAddString(argv, "PRECISION=NO");

    argv = CSLAddString(argv, "-nlt");
    argv = CSLAddString(argv, type.c_str());
    argv = CSLAddString(argv, "-s_srs"); // source projection
    argv = CSLAddString(argv, targetSrs);
    argv = CSLAddString(argv, "-t_srs");
    argv = CSLAddString(argv,
                        reinterpret_cast<const char *>(strcmp(authStr, "-") != 0 ? authStr : t_srs != nullptr ? t_srs
                                                                                                              : "EPSG:4326")); // Convert to this
    argv = CSLAddString(argv, "-nln");
    argv = CSLAddString(argv, altName);


    argv = CSLAddString(argv, layerName);

    GDALDatasetH sourceDs = GDALOpenEx(pszFilename, GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    GDALDatasetH pgDs = GDALOpenEx(connection, GDAL_OF_UPDATE | GDAL_OF_VECTOR,
                                   nullptr, papszOptions, nullptr);
    CSLDestroy(papszOptions);

    if (pgDs == nullptr) {
        exit(1);
    }

    int bUsageError{FALSE};
    GDALVectorTranslateOptions *opt = GDALVectorTranslateOptionsNew(argv, nullptr);
    auto *dst = (GDALDataset *) GDALVectorTranslate(nullptr, pgDs, 1, &sourceDs, opt, &bUsageError);

    if (dst == nullptr) {
        std::cout << "ERROR!" << std::endl;
        GDALVectorTranslateOptionsFree(opt);
        GDALClose(dst);
        return TRUE;
    } else {
        OGRLayer *layer = dst->GetLayerByName(altName);
        if (layer->GetFeatureCount() == 0) {
            std::cout << "Try again" << std::endl;
            GDALVectorTranslateOptionsFree(opt);
            GDALClose(dst);
            return FALSE;
        }
        std::cout << "   Imported";
        GDALVectorTranslateOptionsFree(opt);
        GDALClose(dst);
        CSLDestroy(argv);
        return TRUE;
    }
}
